/**
 * @file spi_manager.hpp
 * @brief Unified SPI bus manager — arbitration, locking, CS control
 *
 * Manages two SPI buses:
 *   - SPI1: Shared between Motor (powerSTEP01) and LCD (ST7789)
 *   - SPI2: NOR Flash only
 *
 * All transactions are atomic - complete read/write finishes before
 * the bus lock is released.
 *
 * Board-specific pin mappings come from L5_board/spi/spi_board_config.hpp.
 * Locking is delegated to the ISPIBus implementations via injected ILock.
 * No RTOS or CMSIS headers appear in this file.
 */

#ifndef SPI_MANAGER_HPP
#define SPI_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "L5_board/spi/spi_board_config.hpp"
#include "L5_board/spi/spi_hardware.hpp"
#include "L5_board/board_pins.hpp"
#include "harness/pins/ilock.hpp"

namespace Board {

/**
 * @brief Maximum transaction data size
 */
constexpr size_t SPI_MAX_TXN_SIZE = 64;

/**
 * @brief SPI transaction descriptor
 */
struct SPITransaction {
    SPIDevice device;
    Harness::SPIMode mode;
    uint8_t txData[SPI_MAX_TXN_SIZE];
    uint8_t rxData[SPI_MAX_TXN_SIZE];
    size_t length;
    bool success;

    void init(SPIDevice dev, const uint8_t* tx, size_t len) {
        device = dev;
        mode = getDeviceMode(dev);
        length = (len <= SPI_MAX_TXN_SIZE) ? len : SPI_MAX_TXN_SIZE;
        if (tx != nullptr) {
            memcpy(txData, tx, length);
        } else {
            memset(txData, 0xFF, length);
        }
        memset(rxData, 0, length);
        success = false;
    }

    void initWithMode(SPIDevice dev, Harness::SPIMode m, const uint8_t* tx, size_t len) {
        init(dev, tx, len);
        mode = m;
    }
};

/**
 * @brief Unified SPI Manager
 *
 * Manages both SPI1 and SPI2 buses with proper lock protection.
 * Locking is delegated to the ISPIBus implementations.
 */
class SPIManager {
public:
    SPIManager()
        : m_spi1(nullptr), m_spi2(nullptr), m_initialized(false)
    {
    }

    /**
     * @brief Initialize the SPI manager
     *
     * Creates hardware SPI for SPI1 and SPI2.
     * Must be called before scheduler starts.
     *
     * @param spi1Lock Lock for SPI1 bus arbitration (shared by Motor + LCD)
     * @param spi2Lock Lock for SPI2 bus arbitration (NOR Flash)
     * @return true if successful
     */
    bool init(Harness::ILock& spi1Lock, Harness::ILock& spi2Lock) {
        // SPI1: Hardware SPI peripheral on PA5 (SCK), PA6 (MISO), PA7 (MOSI)
        // Prescaler 4 = /32 → 84 MHz / 32 = 2.625 MHz
        // Safe for both ST7789 LCD (max ~62 MHz) and powerSTEP01 (max 5 MHz)
        m_spi1 = new SPIHardware(
            SPI1,
            Pins::SPI1_Bus::PORT,
            Pins::SPI1_Bus::SCK_PIN,
            Pins::SPI1_Bus::MISO_PIN,
            Pins::SPI1_Bus::MOSI_PIN,
            Pins::SPI1_Bus::AF,
            Harness::SPIMode::Mode3,
            Harness::SPIPrescaler::Div32,  // 84 MHz / 32 = 2.625 MHz
            spi1Lock
        );

        // SPI2: Hardware SPI on PB13 (SCK), PC2 (MISO), PC3 (MOSI)
        // Prescaler 0 = /2 → 42 MHz / 2 = 21 MHz (MX25L6433F supports 86 MHz normal read)
        m_spi2 = new SPIHardware(
            SPI2,
            Pins::SPI2_Bus::SCK_PORT,  Pins::SPI2_Bus::SCK_PIN,
            Pins::SPI2_Bus::MISO_PORT, Pins::SPI2_Bus::MISO_PIN,
            Pins::SPI2_Bus::MOSI_PORT, Pins::SPI2_Bus::MOSI_PIN,
            Pins::SPI2_Bus::AF,
            Harness::SPIMode::Mode0,
            Harness::SPIPrescaler::Div2,  // 42 MHz / 2 = 21 MHz
            spi2Lock
        );

        if (m_spi1 == nullptr || m_spi2 == nullptr) {
            return false;
        }

        // Initialize CS pins via spi_board_config
        initAllCSPins();

        // PB15: formerly SPI2 MOSI, now unused — force input, no pull
        static GPIO pb15(Pins::SPI2_Bus::SCK_PORT, 15);  // GPIOB pin 15
        pb15.setMode(GPIOMode::Input);
        pb15.setPull(GPIOPull::None);

        m_initialized = true;
        return true;
    }

    /**
     * @brief Execute transaction immediately (blocking, synchronous)
     *
     * This is the primary API for most use cases. The entire transaction
     * completes atomically before returning.
     *
     * @param txn Transaction to execute (rxData filled on return)
     * @return true if successful
     */
    bool execute(SPITransaction& txn) {
        if (!m_initialized) {
            txn.success = false;
            return false;
        }

        uint8_t bus = getDeviceBus(txn.device);
        Harness::ISPIBus* spi = (bus == 1) ? m_spi1 : m_spi2;

        // Acquire bus lock (blocks until available)
        spi->lock();

        // Set mode for this device
        spi->setMode(txn.mode);

        // Assert CS
        GPIO& cs = getDeviceCS(txn.device);
        cs.write(false);  // Low = selected

        // Setup delay
        for (volatile int i = 0; i < 20; i++) {}

        // Transfer all bytes
        for (size_t i = 0; i < txn.length; i++) {
            txn.rxData[i] = spi->transfer(txn.txData[i]);
        }

        // Hold delay
        for (volatile int i = 0; i < 20; i++) {}

        // Deassert CS
        cs.write(true);  // High = deselected

        // Release bus lock
        spi->unlock();

        txn.success = true;
        return true;
    }

    // ========================================================================
    // Convenience methods for specific devices
    // ========================================================================

    bool motorTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::Motor, tx, len);
        if (!execute(txn)) return false;
        if (rx != nullptr) memcpy(rx, txn.rxData, len);
        return true;
    }

    bool lcdTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::LCD, tx, len);
        if (!execute(txn)) return false;
        if (rx != nullptr) memcpy(rx, txn.rxData, len);
        return true;
    }

    bool flashTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::NORFlash, tx, len);
        if (!execute(txn)) return false;
        if (rx != nullptr) memcpy(rx, txn.rxData, len);
        return true;
    }

    // ========================================================================
    // Debug methods
    // ========================================================================

    bool readMISO1() const {
        return (Pins::SPI1_Bus::PORT->IDR & (1UL << Pins::SPI1_Bus::MISO_PIN)) != 0;
    }

    bool readMISO2() const {
        return (Pins::SPI2_Bus::MISO_PORT->IDR & (1UL << Pins::SPI2_Bus::MISO_PIN)) != 0;
    }

    bool isInitialized() const { return m_initialized; }

    Harness::ISPIBus* getSPI1() { return m_spi1; }
    Harness::ISPIBus* getSPI2() { return m_spi2; }

private:
    Harness::ISPIBus* m_spi1;
    Harness::ISPIBus* m_spi2;
    bool m_initialized;
};

// Global SPI manager instance
extern SPIManager g_spiManager;

} // namespace Board

#endif // SPI_MANAGER_HPP
