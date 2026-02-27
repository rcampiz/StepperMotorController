/**
 * @file spi_transaction.hpp
 * @brief Transaction-based SPI manager with FreeRTOS queue
 *
 * Ensures complete SPI transactions are atomic - the entire read/write
 * operation completes before the mutex is released and next transaction starts.
 *
 * Usage:
 *   1. Create SPITransactionManager with underlying SPI implementation
 *   2. Submit transactions via submitTransaction() or submitTransactionBlocking()
 *   3. Each transaction specifies: device CS, SPI mode, TX/RX data, length
 *   4. Manager handles CS assertion, mode switching, data transfer, CS release
 */

#ifndef SPI_TRANSACTION_HPP
#define SPI_TRANSACTION_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/queue.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/semphr.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "L4_drivers/spi/ispi_bus.hpp"

/**
 * @brief Maximum transaction data size
 *
 * Transactions larger than this must be split or use streaming API.
 * Sized for typical powerSTEP01/LCD commands (usually < 16 bytes).
 */
constexpr size_t SPI_MAX_TRANSACTION_SIZE = 32;

/**
 * @brief SPI device identifier
 */
enum class SPIDevice : uint8_t {
    Motor,      // powerSTEP01 - Mode 3, CS on PB6
    LCD,        // ST7789 LCD - Mode 0, CS on PC6
    NORFlash    // NOR Flash - Mode 0, CS on PA8 (SPI2)
};

/**
 * @brief Complete SPI transaction descriptor
 *
 * Contains all information needed to execute a complete SPI transaction:
 * - Which device (determines CS pin and default mode)
 * - SPI mode override (optional)
 * - TX data to send
 * - RX buffer for received data
 * - Completion semaphore for blocking callers
 */
struct SPITransaction {
    // Device selection
    SPIDevice device;

    // SPI mode (can override device default)
    SPIMode mode;

    // Transaction data
    uint8_t txData[SPI_MAX_TRANSACTION_SIZE];
    uint8_t rxData[SPI_MAX_TRANSACTION_SIZE];
    size_t length;

    // Completion signaling (for blocking calls)
    SemaphoreHandle_t completionSem;

    // Result status
    bool success;

    /**
     * @brief Initialize a transaction
     */
    void init(SPIDevice dev, SPIMode m, const uint8_t* tx, size_t len) {
        device = dev;
        mode = m;
        length = (len <= SPI_MAX_TRANSACTION_SIZE) ? len : SPI_MAX_TRANSACTION_SIZE;
        if (tx != nullptr) {
            memcpy(txData, tx, length);
        } else {
            memset(txData, 0xFF, length);  // Default to 0xFF for reads
        }
        memset(rxData, 0, length);
        completionSem = nullptr;
        success = false;
    }
};

/**
 * @brief SPI Transaction Manager
 *
 * Manages a queue of SPI transactions and executes them atomically.
 * Ensures each transaction completes fully before starting the next.
 */
class SPITransactionManager {
public:
    /**
     * @brief Construct transaction manager
     *
     * @param spi Underlying SPI implementation (hardware or bit-banged)
     * @param queueDepth Number of transactions that can be queued
     */
    SPITransactionManager(ISPIBus& spi, size_t queueDepth = 8)
        : m_spi(spi), m_queue(nullptr), m_mutex(nullptr), m_running(false)
    {
        m_queue = xQueueCreate(queueDepth, sizeof(SPITransaction));
        configASSERT(m_queue != nullptr);

        m_mutex = xSemaphoreCreateMutex();
        configASSERT(m_mutex != nullptr);

        // Initialize CS pins
        initCSPins();
    }

    ~SPITransactionManager() {
        if (m_queue != nullptr) {
            vQueueDelete(m_queue);
        }
        if (m_mutex != nullptr) {
            vSemaphoreDelete(m_mutex);
        }
    }

    /**
     * @brief Submit transaction (non-blocking)
     *
     * Queues the transaction for processing. Returns immediately.
     * Use submitTransactionBlocking() if you need the result.
     *
     * @param txn Transaction to submit (copied into queue)
     * @param timeout Ticks to wait if queue is full
     * @return true if queued successfully
     */
    bool submitTransaction(const SPITransaction& txn, TickType_t timeout = 0) {
        if (m_queue == nullptr) {
            return false;
        }
        return xQueueSend(m_queue, &txn, timeout) == pdTRUE;
    }

    /**
     * @brief Submit transaction and wait for completion (blocking)
     *
     * Submits transaction and blocks until it completes.
     * RX data is copied back to the provided transaction.
     *
     * @param txn Transaction to execute (modified with RX data on return)
     * @param timeout Max ticks to wait for completion
     * @return true if transaction completed successfully
     */
    bool submitTransactionBlocking(SPITransaction& txn, TickType_t timeout = portMAX_DELAY) {
        // Create completion semaphore
        SemaphoreHandle_t sem = xSemaphoreCreateBinary();
        if (sem == nullptr) {
            return false;
        }

        txn.completionSem = sem;

        // Submit to queue
        if (xQueueSend(m_queue, &txn, timeout) != pdTRUE) {
            vSemaphoreDelete(sem);
            return false;
        }

        // Wait for completion
        bool completed = (xSemaphoreTake(sem, timeout) == pdTRUE);

        // Clean up semaphore
        vSemaphoreDelete(sem);

        return completed && txn.success;
    }

    /**
     * @brief Execute transaction immediately (synchronous, direct call)
     *
     * Bypasses the queue and executes immediately. Caller must ensure
     * no other task is using the SPI bus (or this will block on mutex).
     *
     * @param txn Transaction to execute (modified with RX data)
     * @return true if successful
     */
    bool executeImmediate(SPITransaction& txn) {
        return executeTransaction(txn);
    }

    /**
     * @brief Process pending transactions (call from SPI task)
     *
     * Blocks waiting for transactions, executes them atomically.
     * Call this in a loop from a dedicated SPI processing task.
     *
     * @param timeout Ticks to wait for next transaction
     * @return true if a transaction was processed
     */
    bool processPending(TickType_t timeout = portMAX_DELAY) {
        SPITransaction txn;

        if (xQueueReceive(m_queue, &txn, timeout) != pdTRUE) {
            return false;
        }

        // Execute the transaction
        executeTransaction(txn);

        // Signal completion if caller is waiting
        if (txn.completionSem != nullptr) {
            xSemaphoreGive(txn.completionSem);
        }

        return true;
    }

    /**
     * @brief Get number of pending transactions
     */
    size_t getPendingCount() const {
        if (m_queue == nullptr) {
            return 0;
        }
        return uxQueueMessagesWaiting(m_queue);
    }

    /**
     * @brief Convenience: Execute motor transaction
     */
    bool motorTransaction(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::Motor, SPIMode::Mode3, tx, len);

        if (!executeImmediate(txn)) {
            return false;
        }

        if (rx != nullptr) {
            memcpy(rx, txn.rxData, len);
        }
        return txn.success;
    }

    /**
     * @brief Convenience: Execute LCD transaction
     */
    bool lcdTransaction(const uint8_t* tx, uint8_t* rx, size_t len) {
        SPITransaction txn;
        txn.init(SPIDevice::LCD, SPIMode::Mode0, tx, len);

        if (!executeImmediate(txn)) {
            return false;
        }

        if (rx != nullptr) {
            memcpy(rx, txn.rxData, len);
        }
        return txn.success;
    }

private:
    ISPIBus& m_spi;
    QueueHandle_t m_queue;
    SemaphoreHandle_t m_mutex;
    bool m_running;

    // CS pin definitions (from board_pins.hpp)
    struct CSPin {
        GPIO_TypeDef* port;
        uint8_t pin;
    };

    static constexpr CSPin MOTOR_CS = {GPIOC, 8};   // PC8 (CN10-P2)
    static constexpr CSPin LCD_CS = {GPIOC, 6};     // PC6
    static constexpr CSPin FLASH_CS = {GPIOA, 8};   // PA8

    /**
     * @brief Initialize all CS pins as outputs, high (deselected)
     */
    void initCSPins() {
        // Enable GPIO clocks
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // Configure each CS as output, initially high
        configureCSPin(MOTOR_CS);
        configureCSPin(LCD_CS);
        configureCSPin(FLASH_CS);
    }

    void configureCSPin(const CSPin& cs) {
        // Output mode
        cs.port->MODER &= ~(0x3UL << (cs.pin * 2));
        cs.port->MODER |= (0x1UL << (cs.pin * 2));
        // Push-pull
        cs.port->OTYPER &= ~(1UL << cs.pin);
        // High speed
        cs.port->OSPEEDR |= (0x3UL << (cs.pin * 2));
        // Set high (deselected)
        cs.port->BSRR = (1UL << cs.pin);
    }

    void assertCS(SPIDevice device) {
        const CSPin& cs = getCSPin(device);
        cs.port->BSRR = (1UL << (cs.pin + 16));  // Reset bit = low
    }

    void deassertCS(SPIDevice device) {
        const CSPin& cs = getCSPin(device);
        cs.port->BSRR = (1UL << cs.pin);  // Set bit = high
    }

    const CSPin& getCSPin(SPIDevice device) const {
        switch (device) {
            case SPIDevice::Motor:    return MOTOR_CS;
            case SPIDevice::LCD:      return LCD_CS;
            case SPIDevice::NORFlash: return FLASH_CS;
            default:                  return MOTOR_CS;
        }
    }

    /**
     * @brief Execute a single transaction atomically
     *
     * Acquires mutex, sets mode, asserts CS, transfers all data,
     * deasserts CS, releases mutex. The entire operation is atomic.
     */
    bool executeTransaction(SPITransaction& txn) {
        // Validate
        if (txn.length == 0 || txn.length > SPI_MAX_TRANSACTION_SIZE) {
            txn.success = false;
            return false;
        }

        // Acquire bus lock - this ensures no other transaction can interrupt
        if (xSemaphoreTake(m_mutex, portMAX_DELAY) != pdTRUE) {
            txn.success = false;
            return false;
        }

        // Enter critical section to prevent any interruption during transfer
        taskENTER_CRITICAL();

        // Set SPI mode for this device
        m_spi.setMode(txn.mode);

        // Assert chip select
        assertCS(txn.device);

        // Small delay after CS assertion (setup time)
        for (volatile int i = 0; i < 10; i++) {
            __NOP();
        }

        // Transfer all data - this is the atomic part
        // We stay in critical section for the entire transfer
        for (size_t i = 0; i < txn.length; i++) {
            txn.rxData[i] = m_spi.transfer(txn.txData[i]);
        }

        // Small delay before CS deassert (hold time)
        for (volatile int i = 0; i < 10; i++) {
            __NOP();
        }

        // Deassert chip select
        deassertCS(txn.device);

        // Exit critical section
        taskEXIT_CRITICAL();

        // Release bus lock
        xSemaphoreGive(m_mutex);

        txn.success = true;
        return true;
    }
};

#endif // SPI_TRANSACTION_HPP
