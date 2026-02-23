/**
 * @file spi_manager.hpp
 * @brief Unified SPI manager for multiple buses
 *
 * Manages two SPI buses:
 *   - SPI1: Shared between Motor (powerSTEP01) and LCD (ST7789)
 *   - SPI2: NOR Flash only
 *
 * All transactions are atomic - complete read/write finishes before
 * the bus lock is released. SPI1 uses hardware peripheral, SPI2 uses bit-bang.
 *
 * Locking is delegated to the ISPIBus implementations via injected ILock.
 * No RTOS headers appear in this file.
 */

#ifndef SPI_MANAGER_HPP
#define SPI_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "stm32f401xe.h"
#include "drivers/spi_hardware.hpp"
#include "board/board_pins.hpp"
#include "platform/ilock.hpp"

/**
 * @brief Maximum transaction data size
 */
constexpr size_t SPI_MAX_TXN_SIZE = 64;

/**
 * @brief SPI device identifier (determines bus and CS)
 */
enum class SPIDevice : uint8_t {
    Motor,      // SPI1, Mode 3, CS=PC8
    LCD,        // SPI1, Mode 0, CS=PC6
    NORFlash    // SPI2, Mode 0, CS=PA8
};

/**
 * @brief Get the SPI bus for a device
 */
inline uint8_t getDeviceBus(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return 1;
        case SPIDevice::LCD:      return 1;
        case SPIDevice::NORFlash: return 2;
        default:                  return 1;
    }
}

/**
 * @brief Get the default SPI mode for a device
 */
inline SPIMode getDeviceMode(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return SPIMode::Mode3;
        case SPIDevice::LCD:      return SPIMode::Mode0;
        case SPIDevice::NORFlash: return SPIMode::Mode0;
        default:                  return SPIMode::Mode0;
    }
}

/**
 * @brief CS pin descriptor
 */
struct CSPin {
    GPIO_TypeDef* port;
    uint8_t pin;
};

/**
 * @brief Get CS pin for a device
 */
inline CSPin getDeviceCS(SPIDevice device) {
    switch (device) {
        case SPIDevice::Motor:    return {Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN};
        case SPIDevice::LCD:      return {Pins::GFX_LCD::CS_PORT, Pins::GFX_LCD::CS_PIN};
        case SPIDevice::NORFlash: return {Pins::GFX_Flash::CS_PORT, Pins::GFX_Flash::CS_PIN};
        default:                  return {Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN};
    }
}

/**
 * @brief SPI transaction descriptor
 */
struct SPITransaction {
    SPIDevice device;
    SPIMode mode;
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

    void initWithMode(SPIDevice dev, SPIMode m, const uint8_t* tx, size_t len) {
        init(dev, tx, len);
        mode = m;
    }
};

/**
 * @brief Unified SPI Manager
 *
 * Manages both SPI1 and SPI2 buses with proper lock protection.
 * SPI1 uses hardware peripheral, SPI2 uses bit-bang.
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
     * Creates hardware SPI for SPI1 and bit-banged SPI for SPI2.
     * Must be called before scheduler starts.
     *
     * @param spi1Lock Lock for SPI1 bus arbitration (shared by Motor + LCD)
     * @param spi2Lock Lock for SPI2 bus arbitration (NOR Flash)
     * @return true if successful
     */
    bool init(ILock& spi1Lock, ILock& spi2Lock) {
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
            SPIMode::Mode3,
            4,  // BR = /32 (2.625 MHz)
            spi1Lock
        );

        // SPI2: Hardware SPI on PB13 (SCK), PB14 (MISO), PC3 (MOSI)
        // Prescaler 4 = /32 → 42 MHz / 32 = 1.3 MHz (conservative, proven reliable)
        m_spi2 = new SPIHardware(
            SPI2,
            Pins::SPI2_Bus::SCK_PORT,  Pins::SPI2_Bus::SCK_PIN,
            Pins::SPI2_Bus::MISO_PORT, Pins::SPI2_Bus::MISO_PIN,
            Pins::SPI2_Bus::MOSI_PORT, Pins::SPI2_Bus::MOSI_PIN,
            Pins::SPI2_Bus::AF,
            SPIMode::Mode0,
            4,  // BR = /32 (1.3 MHz)
            spi2Lock
        );

        if (m_spi1 == nullptr || m_spi2 == nullptr) {
            return false;
        }

        // Initialize CS pins
        initCSPins();

        m_initialized = true;
        return true;
    }

    /**
     * @brief Execute transaction immediately (blocking, synchronous)
     *
     * This is the primary API for most use cases. The entire transaction
     * completes atomically before returning.
     *
     * Locking is handled by the ISPIBus::lock()/unlock() which delegates
     * to the injected ILock. No RTOS calls here.
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
        ISPIBus* spi = (bus == 1) ? m_spi1 : m_spi2;

        // Acquire bus lock (blocks until available)
        spi->lock();

        // Set mode for this device
        spi->setMode(txn.mode);

        // Assert CS
        CSPin cs = getDeviceCS(txn.device);
        cs.port->BSRR = (1UL << (cs.pin + 16));  // Low = selected

        // Setup delay
        for (volatile int i = 0; i < 20; i++) { __NOP(); }

        // Transfer all bytes
        for (size_t i = 0; i < txn.length; i++) {
            txn.rxData[i] = spi->transfer(txn.txData[i]);
        }

        // Hold delay
        for (volatile int i = 0; i < 20; i++) { __NOP(); }

        // Deassert CS
        cs.port->BSRR = (1UL << cs.pin);  // High = deselected

        // Release bus lock
        spi->unlock();

        txn.success = true;
        return true;
    }

    // ========================================================================
    // Convenience methods for specific devices
    // ========================================================================

    /**
     * @brief Execute motor (powerSTEP01) transaction
     */
    bool motorTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::Motor, tx, len);
        if (!execute(txn)) {
            return false;
        }
        if (rx != nullptr) {
            memcpy(rx, txn.rxData, len);
        }
        return true;
    }

    /**
     * @brief Execute LCD transaction
     */
    bool lcdTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::LCD, tx, len);
        if (!execute(txn)) {
            return false;
        }
        if (rx != nullptr) {
            memcpy(rx, txn.rxData, len);
        }
        return true;
    }

    /**
     * @brief Execute NOR Flash transaction
     */
    bool flashTransfer(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::NORFlash, tx, len);
        if (!execute(txn)) {
            return false;
        }
        if (rx != nullptr) {
            memcpy(rx, txn.rxData, len);
        }
        return true;
    }

    // ========================================================================
    // Debug methods
    // ========================================================================

    /**
     * @brief Read MISO pin state for SPI1
     */
    bool readMISO1() const {
        return (Pins::SPI1_Bus::PORT->IDR & (1UL << Pins::SPI1_Bus::MISO_PIN)) != 0;
    }

    /**
     * @brief Read MISO pin state for SPI2
     */
    bool readMISO2() const {
        return (Pins::SPI2_Bus::MISO_PORT->IDR & (1UL << Pins::SPI2_Bus::MISO_PIN)) != 0;
    }

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get SPI1 instance for direct access (use with care)
     */
    ISPIBus* getSPI1() { return m_spi1; }

    /**
     * @brief Get SPI2 instance for direct access (use with care)
     */
    ISPIBus* getSPI2() { return m_spi2; }

private:
    ISPIBus* m_spi1;
    ISPIBus* m_spi2;
    bool m_initialized;

    void initCSPins() {
        // Enable GPIO clocks
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // Motor CS (PC8)
        configureCS(getDeviceCS(SPIDevice::Motor));
        // LCD CS (PC6)
        configureCS(getDeviceCS(SPIDevice::LCD));
        // NOR Flash CS (PA8)
        configureCS(getDeviceCS(SPIDevice::NORFlash));

        // PB15: formerly SPI2 MOSI, now unused — force input, no pull
        GPIOB->MODER &= ~(0x3UL << (15 * 2));   // Input mode (00)
        GPIOB->PUPDR &= ~(0x3UL << (15 * 2));   // No pull-up/pull-down (00)
    }

    void configureCS(const CSPin& cs) {
        // Output mode
        cs.port->MODER &= ~(0x3UL << (cs.pin * 2));
        cs.port->MODER |= (0x1UL << (cs.pin * 2));
        // Push-pull
        cs.port->OTYPER &= ~(1UL << cs.pin);
        // High speed
        cs.port->OSPEEDR |= (0x3UL << (cs.pin * 2));
        // Start high (deselected)
        cs.port->BSRR = (1UL << cs.pin);
    }
};

// Global SPI manager instance
extern SPIManager g_spiManager;

#endif // SPI_MANAGER_HPP
