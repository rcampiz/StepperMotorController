/**
 * @file spi_bitbang.hpp
 * @brief Bit-banged SPI driver for debugging hardware SPI issues
 *
 * Drop-in replacement for SPIBus that uses GPIO bit-banging instead of
 * the hardware SPI peripheral. Supports all 4 SPI modes.
 *
 * SPI Mode Timing:
 *   Mode 0 (CPOL=0, CPHA=0): Clock idle LOW,  shift falling, sample rising
 *   Mode 1 (CPOL=0, CPHA=1): Clock idle LOW,  shift rising,  sample falling
 *   Mode 2 (CPOL=1, CPHA=0): Clock idle HIGH, shift rising,  sample falling
 *   Mode 3 (CPOL=1, CPHA=1): Clock idle HIGH, shift falling, sample rising
 *
 * Locking is provided by an externally injected ILock.
 * Interrupt masking for bit-bang timing uses InterruptGuard (CMSIS PRIMASK).
 * No RTOS headers appear in this file.
 */

#ifndef SPI_BITBANG_HPP
#define SPI_BITBANG_HPP

#include <stdint.h>
#include <stddef.h>
#include "stm32f401xe.h"
#include "L4_drivers/spi/ispi_bus.hpp"
#include "F_platform/ilock.hpp"
#include "F_platform/interrupt_guard.hpp"

// CMSIS standard variable (defined in system_stm32f4xx.c)
extern uint32_t SystemCoreClock;

// DWT (Data Watchpoint and Trace) cycle counter for precise timing
// These are defined in core_cm4.h but we provide fallbacks for compatibility
#ifndef DWT_CTRL_CYCCNTENA_Msk
#define DWT_BASE            (0xE0001000UL)
#define DWT                 ((DWT_Type*)DWT_BASE)
typedef struct {
    volatile uint32_t CTRL;
    volatile uint32_t CYCCNT;
    volatile uint32_t CPICNT;
    volatile uint32_t EXCCNT;
    volatile uint32_t SLEEPCNT;
    volatile uint32_t LSUCNT;
    volatile uint32_t FOLDCNT;
    volatile uint32_t PCSR;
} DWT_Type;
#define DWT_CTRL_CYCCNTENA_Msk  (1UL << 0)
#endif

#ifndef CoreDebug_DEMCR_TRCENA_Msk
#define CoreDebug_BASE      (0xE000EDF0UL)
#define CoreDebug           ((CoreDebug_Type*)CoreDebug_BASE)
typedef struct {
    volatile uint32_t DHCSR;
    volatile uint32_t DCRSR;
    volatile uint32_t DCRDR;
    volatile uint32_t DEMCR;
} CoreDebug_Type;
#define CoreDebug_DEMCR_TRCENA_Msk  (1UL << 24)
#endif

/**
 * @brief Bit-banged SPI implementation
 *
 * Implements ISPIBus interface using GPIO bit-banging.
 * Can be used as a drop-in replacement for hardware SPI.
 */
class SPIBitBang : public ISPIBus {
public:
    // Use the common SPIMode enum from ISPIBus
    using Mode = SPIMode;

    /**
     * @brief Construct bit-banged SPI on specified GPIO pins
     *
     * @param sckPort  GPIO port for SCK
     * @param sckPin   Pin number for SCK (0-15)
     * @param misoPort GPIO port for MISO
     * @param misoPin  Pin number for MISO (0-15)
     * @param mosiPort GPIO port for MOSI
     * @param mosiPin  Pin number for MOSI (0-15)
     * @param mode     SPI mode (default Mode3 for powerSTEP01)
     * @param clockHz  Target SPI clock frequency in Hz (default 1 MHz)
     * @param lock     External lock for bus arbitration
     */
    SPIBitBang(GPIO_TypeDef* sckPort, uint8_t sckPin,
               GPIO_TypeDef* misoPort, uint8_t misoPin,
               GPIO_TypeDef* mosiPort, uint8_t mosiPin,
               Mode mode,
               uint32_t clockHz,
               ILock& lock)
        : m_sckPort(sckPort), m_sckPin(sckPin),
          m_misoPort(misoPort), m_misoPin(misoPin),
          m_mosiPort(mosiPort), m_mosiPin(mosiPin),
          m_mode(mode), m_lock(lock), m_halfPeriodCycles(0)
    {
        // Calculate cycles for half clock period
        // At 84 MHz, 1 MHz SPI = 84 cycles per bit, 42 cycles per half-period
        m_halfPeriodCycles = (SystemCoreClock / clockHz) / 2;
        if (m_halfPeriodCycles < 4) {
            m_halfPeriodCycles = 4;  // Minimum for GPIO toggle overhead
        }

        initDWT();
        init();
    }

    /**
     * @brief Initialize GPIO pins for bit-banging
     */
    void init() {
        // Enable GPIO clocks (A, B, C should cover most cases)
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

        // Configure SCK as output
        configureOutput(m_sckPort, m_sckPin);

        // Configure MISO as input (no pull - let external circuit determine)
        configureInput(m_misoPort, m_misoPin);

        // Configure MOSI as output
        configureOutput(m_mosiPort, m_mosiPin);

        // Set clock to idle state based on mode
        setClockIdle();
    }

    /**
     * @brief Switch SPI mode
     * @param mode New SPI mode
     */
    void setMode(Mode mode) override {
        m_mode = mode;
        setClockIdle();
    }

    /**
     * @brief Get current SPI mode
     */
    Mode getMode() const override { return m_mode; }

    /**
     * @brief Set target clock frequency
     * @param clockHz Target frequency in Hz
     */
    void setClockHz(uint32_t clockHz) {
        m_halfPeriodCycles = (SystemCoreClock / clockHz) / 2;
        if (m_halfPeriodCycles < 4) {
            m_halfPeriodCycles = 4;
        }
    }

    /**
     * @brief Get actual clock frequency based on current settings
     */
    uint32_t getClockHz() const {
        return SystemCoreClock / (m_halfPeriodCycles * 2);
    }

    /**
     * @brief Acquire bus lock (blocks until available)
     */
    void lock() override {
        m_lock.acquire();
    }

    /**
     * @brief Release bus lock
     */
    void unlock() override {
        m_lock.release();
    }

    /**
     * @brief Transfer one byte (full duplex)
     *
     * Uses critical section to prevent timing jitter from interrupts.
     *
     * @param data Byte to send
     * @return Byte received
     */
    uint8_t transfer(uint8_t data) override {
        uint8_t rx = 0;

        // Get CPOL and CPHA from mode
        bool cpol = (static_cast<uint8_t>(m_mode) & 0x02) != 0;  // Bit 1
        bool cpha = (static_cast<uint8_t>(m_mode) & 0x01) != 0;  // Bit 0

        // RAII interrupt guard prevents jitter during bit-bang timing
        InterruptGuard guard;

        for (int bit = 7; bit >= 0; bit--) {
            if (cpha == 0) {
                // CPHA=0: Data setup before first edge, sample on first edge

                // Set MOSI (data setup)
                setMOSI((data >> bit) & 1);
                delayCycles(m_halfPeriodCycles);

                // First edge (leading) - transition away from idle
                setClock(!cpol);
                delayCycles(m_halfPeriodCycles);

                // Sample MISO on/after first edge
                if (readMISO()) {
                    rx |= (1 << bit);
                }

                // Second edge (trailing) - return to idle
                setClock(cpol);

            } else {
                // CPHA=1: Data shifted on first edge, sampled on second edge

                // First edge (leading) - transition away from idle, shift data
                setClock(!cpol);
                setMOSI((data >> bit) & 1);
                delayCycles(m_halfPeriodCycles);

                // Second edge (trailing) - return to idle
                setClock(cpol);
                delayCycles(m_halfPeriodCycles);

                // Sample MISO on/after second edge
                if (readMISO()) {
                    rx |= (1 << bit);
                }
            }
        }

        return rx;
    }

    /**
     * @brief Transfer multiple bytes
     *
     * Note: Each byte transfer is in a critical section, but there may be
     * gaps between bytes. For continuous streaming, use transferContinuous().
     */
    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            uint8_t tx = txBuf ? txBuf[i] : 0xFF;
            uint8_t rx = transfer(tx);
            if (rxBuf) {
                rxBuf[i] = rx;
            }
        }
    }

    /**
     * @brief Transfer multiple bytes in one critical section
     *
     * Use for transfers that must have precise timing between bytes.
     * Warning: Long transfers will block interrupts!
     */
    void transferContinuous(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) {
        InterruptGuard guard;

        for (size_t i = 0; i < len; i++) {
            uint8_t tx = txBuf ? txBuf[i] : 0xFF;
            uint8_t rx = transferNoCS(tx);
            if (rxBuf) {
                rxBuf[i] = rx;
            }
        }
    }

    /**
     * @brief Debug: Read current MISO pin state
     */
    bool debugReadMISO() const {
        return (m_misoPort->IDR & (1UL << m_misoPin)) != 0;
    }

    /**
     * @brief Debug: Get configured half-period in CPU cycles
     */
    uint32_t debugGetHalfPeriodCycles() const {
        return m_halfPeriodCycles;
    }

private:
    GPIO_TypeDef* m_sckPort;
    uint8_t m_sckPin;
    GPIO_TypeDef* m_misoPort;
    uint8_t m_misoPin;
    GPIO_TypeDef* m_mosiPort;
    uint8_t m_mosiPin;
    Mode m_mode;
    ILock& m_lock;
    uint32_t m_halfPeriodCycles;

    /**
     * @brief Initialize DWT cycle counter for precise timing
     */
    void initDWT() {
        // Enable DWT and cycle counter
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }

    /**
     * @brief Delay for specified number of CPU cycles using DWT
     */
    void delayCycles(uint32_t cycles) {
        uint32_t start = DWT->CYCCNT;
        while ((DWT->CYCCNT - start) < cycles) {
            // Busy wait
        }
    }

    void configureOutput(GPIO_TypeDef* port, uint8_t pin) {
        // Clear mode bits and set to output (01)
        port->MODER &= ~(0x3UL << (pin * 2));
        port->MODER |= (0x1UL << (pin * 2));
        // High speed
        port->OSPEEDR |= (0x3UL << (pin * 2));
        // Push-pull (default)
        port->OTYPER &= ~(1UL << pin);
    }

    void configureInput(GPIO_TypeDef* port, uint8_t pin) {
        // Clear mode bits (00 = input)
        port->MODER &= ~(0x3UL << (pin * 2));
        // No pull-up/pull-down
        port->PUPDR &= ~(0x3UL << (pin * 2));
    }

    void setClockIdle() {
        bool cpol = (static_cast<uint8_t>(m_mode) & 0x02) != 0;
        setClock(cpol);
    }

    void setClock(bool high) {
        if (high) {
            m_sckPort->BSRR = (1UL << m_sckPin);
        } else {
            m_sckPort->BSRR = (1UL << (m_sckPin + 16));
        }
    }

    void setMOSI(bool high) {
        if (high) {
            m_mosiPort->BSRR = (1UL << m_mosiPin);
        } else {
            m_mosiPort->BSRR = (1UL << (m_mosiPin + 16));
        }
    }

    bool readMISO() const {
        return (m_misoPort->IDR & (1UL << m_misoPin)) != 0;
    }

    /**
     * @brief Transfer one byte without entering/exiting critical section
     * (for use inside transferContinuous)
     */
    uint8_t transferNoCS(uint8_t data) {
        uint8_t rx = 0;
        bool cpol = (static_cast<uint8_t>(m_mode) & 0x02) != 0;
        bool cpha = (static_cast<uint8_t>(m_mode) & 0x01) != 0;

        for (int bit = 7; bit >= 0; bit--) {
            if (cpha == 0) {
                setMOSI((data >> bit) & 1);
                delayCycles(m_halfPeriodCycles);
                setClock(!cpol);
                delayCycles(m_halfPeriodCycles);
                if (readMISO()) rx |= (1 << bit);
                setClock(cpol);
            } else {
                setClock(!cpol);
                setMOSI((data >> bit) & 1);
                delayCycles(m_halfPeriodCycles);
                setClock(cpol);
                delayCycles(m_halfPeriodCycles);
                if (readMISO()) rx |= (1 << bit);
            }
        }
        return rx;
    }
};

#endif // SPI_BITBANG_HPP
