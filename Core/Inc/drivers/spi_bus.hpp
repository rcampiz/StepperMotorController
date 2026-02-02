/**
 * @file spi_bus.hpp
 * @brief RTOS-safe SPI bus driver for STM32F4
 */

#ifndef SPI_BUS_HPP
#define SPI_BUS_HPP

#include <stdint.h>
#include "FreeRTOS.h"
#include "board/board_pins.hpp"
#include "semphr.h"
#include "stm32f401xe.h"

class SPIBus {
public:
  enum class Prescaler : uint8_t {
    Div2 = 0b000,
    Div4 = 0b001,
    Div8 = 0b010,
    Div16 = 0b011,
    Div32 = 0b100,
    Div64 = 0b101,
    Div128 = 0b110,
    Div256 = 0b111
  };

  /**
   * @brief SPI clock polarity and phase modes
   *
   * Mode 0: CPOL=0, CPHA=0 - Clock idle low, sample on rising edge (NOR flash)
   * Mode 1: CPOL=0, CPHA=1 - Clock idle low, sample on falling edge
   * Mode 2: CPOL=1, CPHA=0 - Clock idle high, sample on falling edge
   * Mode 3: CPOL=1, CPHA=1 - Clock idle high, sample on rising edge
   * (powerSTEP01)
   */
  enum class Mode : uint8_t {
    Mode0 = 0,                          // CPOL=0, CPHA=0
    Mode1 = SPI_CR1_CPHA,               // CPOL=0, CPHA=1
    Mode2 = SPI_CR1_CPOL,               // CPOL=1, CPHA=0
    Mode3 = SPI_CR1_CPOL | SPI_CR1_CPHA // CPOL=1, CPHA=1
  };

  SPIBus(SPI_TypeDef *spi, Prescaler prescaler = Prescaler::Div16,
         Mode mode = Mode::Mode3)
      : m_spi(spi), m_mutex(nullptr), m_prescaler(prescaler), m_mode(mode) {
    m_mutex = xSemaphoreCreateMutex();
    init(prescaler, mode);
  }

  ~SPIBus() {
    if (m_mutex)
      vSemaphoreDelete(m_mutex);
  }

  void init(Prescaler prescaler, Mode mode = Mode::Mode3) {
    enableClock();
    configurePins();

    m_prescaler = prescaler;
    m_mode = mode;

    // Disable SPI for configuration
    m_spi->CR1 &= ~SPI_CR1_SPE;

    // Configure: Master, Software NSS, 8-bit, with specified mode
    m_spi->CR1 = SPI_CR1_MSTR                 // Master mode
                 | SPI_CR1_SSM                // Software slave management
                 | SPI_CR1_SSI                // Internal slave select high
                 | static_cast<uint8_t>(mode) // CPOL/CPHA from mode
                 | (static_cast<uint8_t>(prescaler) << SPI_CR1_BR_Pos);

    m_spi->CR2 = 0;

    // Enable SPI
    m_spi->CR1 |= SPI_CR1_SPE;
  }

  /**
   * @brief Switch SPI mode (CPOL/CPHA) - call while holding lock
   * @param mode New SPI mode
   *
   * Use this to switch between devices with different clock requirements.
   * Must be called while the mutex is held (after lock(), before unlock()).
   */
  void setMode(Mode mode) {
    if (m_mode == mode) return;  // No change needed
    m_mode = mode;

    // Disable SPI for reconfiguration
    m_spi->CR1 &= ~SPI_CR1_SPE;

    // Clear and set CPOL/CPHA bits (bits 0-1)
    m_spi->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);
    m_spi->CR1 |= static_cast<uint8_t>(mode);

    // Re-enable SPI
    m_spi->CR1 |= SPI_CR1_SPE;
  }

  /**
   * @brief Get current SPI mode
   * @return Current mode
   */
  Mode getMode() const { return m_mode; }

  bool lock(TickType_t timeout = portMAX_DELAY) {
    if (!m_mutex)
      return false;
    return xSemaphoreTake(m_mutex, timeout) == pdTRUE;
  }

  void unlock() {
    if (m_mutex)
      xSemaphoreGive(m_mutex);
  }

  uint8_t transfer(uint8_t data) {
    while (!(m_spi->SR & SPI_SR_TXE))
      ;
    *reinterpret_cast<volatile uint8_t *>(&m_spi->DR) = data;
    while (!(m_spi->SR & SPI_SR_RXNE))
      ;
    return *reinterpret_cast<volatile uint8_t *>(&m_spi->DR);
  }

  void transfer(const uint8_t *txBuf, uint8_t *rxBuf, size_t len) {
    for (size_t i = 0; i < len; i++) {
      uint8_t tx = txBuf ? txBuf[i] : 0xFF;
      uint8_t rx = transfer(tx);
      if (rxBuf)
        rxBuf[i] = rx;
    }
  }

  void write(const uint8_t *data, size_t len) { transfer(data, nullptr, len); }

  void read(uint8_t *data, size_t len) { transfer(nullptr, data, len); }

  SPI_TypeDef *peripheral() const { return m_spi; }

private:
  SPI_TypeDef *m_spi;
  SemaphoreHandle_t m_mutex;
  Prescaler m_prescaler;
  Mode m_mode;

  void enableClock() {
    if (m_spi == SPI1) {
      RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    } else if (m_spi == SPI2) {
      RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
    }
    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN;
  }

  void configurePins() {
    if (m_spi == SPI1) {
      configureAF(Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::SCK_PIN,
                  Pins::SPI1_Bus::AF);
      configureAF(Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::MISO_PIN,
                  Pins::SPI1_Bus::AF);
      configureAF(Pins::SPI1_Bus::PORT, Pins::SPI1_Bus::MOSI_PIN,
                  Pins::SPI1_Bus::AF);
    } else if (m_spi == SPI2) {
      configureAF(Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::SCK_PIN,
                  Pins::SPI2_Bus::AF);
      configureAF(Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::MISO_PIN,
                  Pins::SPI2_Bus::AF);
      configureAF(Pins::SPI2_Bus::PORT, Pins::SPI2_Bus::MOSI_PIN,
                  Pins::SPI2_Bus::AF);
    }
  }

  void configureAF(GPIO_TypeDef *port, uint8_t pin, uint8_t af) {
    // Set alternate function mode
    port->MODER &= ~(0x3 << (pin * 2));
    port->MODER |= (0x2 << (pin * 2)); // AF mode

    // High speed
    port->OSPEEDR |= (0x3 << (pin * 2));

    // Set alternate function
    if (pin < 8) {
      port->AFR[0] &= ~(0xF << (pin * 4));
      port->AFR[0] |= (af << (pin * 4));
    } else {
      port->AFR[1] &= ~(0xF << ((pin - 8) * 4));
      port->AFR[1] |= (af << ((pin - 8) * 4));
    }
  }
};

#endif // SPI_BUS_HPP
