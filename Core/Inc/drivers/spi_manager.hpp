/**
 * @file spi_manager.hpp
 * @brief Unified SPI manager for multiple buses with transaction queuing
 *
 * Manages two SPI buses:
 *   - SPI1: Shared between Motor (powerSTEP01) and LCD (ST7789)
 *   - SPI2: NOR Flash only
 *
 * All transactions are atomic - complete read/write finishes before
 * mutex is released. Uses bit-banged SPI for debugging flexibility.
 */

#ifndef SPI_MANAGER_HPP
#define SPI_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "stm32f401xe.h"
#include "drivers/spi_bitbang.hpp"
#include "board/board_pins.hpp"

/**
 * @brief Maximum transaction data size
 */
constexpr size_t SPI_MAX_TXN_SIZE = 64;

/**
 * @brief SPI device identifier (determines bus and CS)
 */
enum class SPIDevice : uint8_t {
    Motor,      // SPI1, Mode 3, CS=PB6
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
    SemaphoreHandle_t completionSem;
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
        completionSem = nullptr;
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
 * Manages both SPI1 and SPI2 buses with proper mutex protection.
 * Each bus has its own bit-banged SPI instance and mutex.
 */
class SPIManager {
public:
    SPIManager()
        : m_spi1(nullptr), m_spi2(nullptr),
          m_mutex1(nullptr), m_mutex2(nullptr),
          m_queue(nullptr), m_initialized(false)
    {
    }

    /**
     * @brief Initialize the SPI manager
     *
     * Creates bit-banged SPI instances for both buses.
     * Must be called before scheduler starts.
     *
     * @param spi1ClockHz Target clock for SPI1 (default 1 MHz)
     * @param spi2ClockHz Target clock for SPI2 (default 1 MHz)
     * @return true if successful
     */
    bool init(uint32_t spi1ClockHz = 1000000, uint32_t spi2ClockHz = 1000000) {
        // Disable hardware SPI peripherals - we're using bit-bang
        RCC->APB2ENR &= ~RCC_APB2ENR_SPI1EN;
        RCC->APB1ENR &= ~RCC_APB1ENR_SPI2EN;

        // Create mutexes
        m_mutex1 = xSemaphoreCreateMutex();
        m_mutex2 = xSemaphoreCreateMutex();
        if (m_mutex1 == nullptr || m_mutex2 == nullptr) {
            return false;
        }

        // Create transaction queue
        m_queue = xQueueCreate(16, sizeof(SPITransaction));
        if (m_queue == nullptr) {
            return false;
        }

        // Create bit-banged SPI instances
        // SPI1: PA5 (SCK), PA6 (MISO), PA7 (MOSI)
        m_spi1 = new SPIBitBang(
            Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::SCK_PIN,
            Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::MISO_PIN,
            Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::MOSI_PIN,
            SPIMode::Mode3, spi1ClockHz
        );

        // SPI2: PB13 (SCK), PB14 (MISO), PB15 (MOSI)
        m_spi2 = new SPIBitBang(
            Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::SCK_PIN,
            Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::MISO_PIN,
            Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::MOSI_PIN,
            SPIMode::Mode0, spi2ClockHz
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
     * @param txn Transaction to execute (rxData filled on return)
     * @return true if successful
     */
    bool execute(SPITransaction& txn) {
        if (!m_initialized) {
            txn.success = false;
            return false;
        }

        uint8_t bus = getDeviceBus(txn.device);
        SemaphoreHandle_t mutex = (bus == 1) ? m_mutex1 : m_mutex2;
        SPIBitBang* spi = (bus == 1) ? m_spi1 : m_spi2;

        // Acquire bus mutex
        if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
            txn.success = false;
            return false;
        }

        // Execute atomically in critical section
        taskENTER_CRITICAL();

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

        taskEXIT_CRITICAL();

        // Release bus mutex
        xSemaphoreGive(mutex);

        txn.success = true;
        return true;
    }

    /**
     * @brief Queue transaction for background processing
     *
     * @param txn Transaction to queue
     * @param timeout Ticks to wait if queue full
     * @return true if queued
     */
    bool submit(const SPITransaction& txn, TickType_t timeout = 0) {
        if (m_queue == nullptr) {
            return false;
        }
        return xQueueSend(m_queue, &txn, timeout) == pdTRUE;
    }

    /**
     * @brief Process one queued transaction
     *
     * Call from a dedicated SPI task.
     *
     * @param timeout Ticks to wait for transaction
     * @return true if processed
     */
    bool processOne(TickType_t timeout = portMAX_DELAY) {
        SPITransaction txn;
        if (xQueueReceive(m_queue, &txn, timeout) != pdTRUE) {
            return false;
        }

        execute(txn);

        if (txn.completionSem != nullptr) {
            xSemaphoreGive(txn.completionSem);
        }

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
        return (Pins::SPI2_Bus::PORT->IDR & (1UL << Pins::SPI2_Bus::MISO_PIN)) != 0;
    }

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief Get SPI1 instance for direct access (use with care)
     */
    SPIBitBang* getSPI1() { return m_spi1; }

    /**
     * @brief Get SPI2 instance for direct access (use with care)
     */
    SPIBitBang* getSPI2() { return m_spi2; }

private:
    SPIBitBang* m_spi1;
    SPIBitBang* m_spi2;
    SemaphoreHandle_t m_mutex1;
    SemaphoreHandle_t m_mutex2;
    QueueHandle_t m_queue;
    bool m_initialized;

    void initCSPins() {
        // Enable GPIO clocks
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // Motor CS (PB6)
        configureCS(getDeviceCS(SPIDevice::Motor));
        // LCD CS (PC6)
        configureCS(getDeviceCS(SPIDevice::LCD));
        // NOR Flash CS (PA8)
        configureCS(getDeviceCS(SPIDevice::NORFlash));
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
