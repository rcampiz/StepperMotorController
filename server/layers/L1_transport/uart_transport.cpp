/**
 * @file uart_transport.cpp
 * @brief USART2 VCP transport implementation
 *
 * Implements interrupt-driven RX and polling TX for the ST-LINK/J-Link
 * Virtual COM Port connection.
 */

#include "L1_transport/uart_transport.hpp"
#include <string.h>

// Debug: IRQ call counter
volatile uint32_t g_usart2IrqCount = 0;
volatile uint32_t g_usart2RxCount = 0;

namespace Comms {

// Singleton instance for ISR access
UartTransport* UartTransport::s_instance = nullptr;

UartTransport::UartTransport(IClock& clock, uint32_t baudRate, uint8_t irqPriority)
    : m_clock(&clock), m_baudRate(baudRate), m_irqPriority(irqPriority) {}

bool UartTransport::init() {
    // Enforce single instance - ISR can only route to one instance
    if (s_instance != nullptr) {
        return false;
    }

    // Set singleton for ISR access
    s_instance = this;

    initGpio();
    initUsart();
    return true;
}

void UartTransport::initGpio() {
    // Enable GPIOA clock
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    GPIO_TypeDef* port = Pins::VCP_UART::PORT;
    constexpr uint8_t txPin = Pins::VCP_UART::TX_PIN;
    constexpr uint8_t rxPin = Pins::VCP_UART::RX_PIN;
    constexpr uint8_t af = Pins::VCP_UART::AF;

    // Configure PA2 (TX) as alternate function, push-pull, high speed
    // MODER: 10 = Alternate function
    port->MODER &= ~(3UL << (txPin * 2));
    port->MODER |= (2UL << (txPin * 2));
    // OTYPER: 0 = Push-pull
    port->OTYPER &= ~(1UL << txPin);
    // OSPEEDR: 11 = High speed
    port->OSPEEDR |= (3UL << (txPin * 2));
    // PUPDR: 00 = No pull-up/pull-down
    port->PUPDR &= ~(3UL << (txPin * 2));
    // AFR: Set alternate function 7 for USART2
    port->AFR[0] &= ~(0xFUL << (txPin * 4));
    port->AFR[0] |= (static_cast<uint32_t>(af) << (txPin * 4));

    // Configure PA3 (RX) as alternate function, with pull-up
    // MODER: 10 = Alternate function
    port->MODER &= ~(3UL << (rxPin * 2));
    port->MODER |= (2UL << (rxPin * 2));
    // OTYPER: 0 = Push-pull (doesn't matter for input)
    port->OTYPER &= ~(1UL << rxPin);
    // OSPEEDR: 11 = High speed
    port->OSPEEDR |= (3UL << (rxPin * 2));
    // PUPDR: 01 = Pull-up (prevents floating when disconnected)
    port->PUPDR &= ~(3UL << (rxPin * 2));
    port->PUPDR |= (1UL << (rxPin * 2));
    // AFR: Set alternate function 7 for USART2
    port->AFR[0] &= ~(0xFUL << (rxPin * 4));
    port->AFR[0] |= (static_cast<uint32_t>(af) << (rxPin * 4));
}

void UartTransport::initUsart() {
    // Enable USART2 clock (APB1)
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    USART_TypeDef* usart = Pins::VCP_UART::INSTANCE;

    // Disable USART for configuration
    usart->CR1 = 0;
    usart->CR2 = 0;
    usart->CR3 = 0;

    // Calculate baud rate
    // BRR = fPCLK / (16 * baud) for OVER8=0
    // APB1 clock = 42 MHz
    // For 115200: BRR = 42000000 / (16 * 115200) = 22.786 ≈ 22 + 13/16
    // USARTDIV = fPCLK / (16 * baud)
    // Mantissa = integer part, Fraction = (fractional part * 16)
    uint32_t integerdivider = ((25 * Pins::VCP_UART::APB1_CLOCK_HZ) / (4 * m_baudRate));
    uint32_t mantissa = integerdivider / 100;
    uint32_t fraction = ((integerdivider - (mantissa * 100)) * 16 + 50) / 100;

    // Handle fraction overflow
    if (fraction >= 16) {
        mantissa++;
        fraction = 0;
    }

    usart->BRR = (mantissa << 4) | (fraction & 0x0F);

    // CR1: Enable TX, RX, RXNE interrupt
    // 8 data bits, no parity (default with M=0, PCE=0)
    usart->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    // CR2: 1 stop bit (default)
    usart->CR2 = 0;

    // CR3: No hardware flow control
    usart->CR3 = 0;

    // Enable USART2 interrupt in NVIC
    NVIC_SetPriority(USART2_IRQn, m_irqPriority);
    NVIC_EnableIRQ(USART2_IRQn);

    // Enable USART
    usart->CR1 |= USART_CR1_UE;
}

bool UartTransport::available() {
    return m_rxBuffer.available();
}

size_t UartTransport::read(uint8_t* buffer, size_t maxLen) {
    size_t count = 0;
    while (count < maxLen && m_rxBuffer.pop(buffer[count])) {
        count++;
    }
    return count;
}

bool UartTransport::readByte(uint8_t& byte, uint32_t timeoutMs) {
    // Try immediate read
    if (m_rxBuffer.pop(byte)) {
        return true;
    }

    // If timeout is 0, return immediately
    if (timeoutMs == 0) {
        return false;
    }

    // Wait with timeout using 1ms delay to avoid spinning hot
    uint32_t startTick = m_clock->getTickMs();

    while ((m_clock->getTickMs() - startTick) < timeoutMs) {
        if (m_rxBuffer.pop(byte)) {
            return true;
        }
        // Sleep 1ms to avoid CPU spin under heavy load
        m_clock->delayMs(1);
    }

    return false;
}

size_t UartTransport::write(const uint8_t* data, size_t len) {
    USART_TypeDef* usart = Pins::VCP_UART::INSTANCE;

    for (size_t i = 0; i < len; i++) {
        // Wait for TXE (transmit data register empty)
        while ((usart->SR & USART_SR_TXE) == 0) {
            // Could add timeout here if needed
        }
        usart->DR = data[i];
    }

    return len;
}

size_t UartTransport::print(const char* str) {
    return write(reinterpret_cast<const uint8_t*>(str), strlen(str));
}

size_t UartTransport::println(const char* str) {
    size_t n = print(str);
    n += write(reinterpret_cast<const uint8_t*>("\r\n"), 2);
    return n;
}

void UartTransport::flush() {
    USART_TypeDef* usart = Pins::VCP_UART::INSTANCE;

    // Wait for TC (transmission complete)
    while ((usart->SR & USART_SR_TC) == 0) {
        // Could add timeout here if needed
    }
}

bool UartTransport::setBaudRate(uint32_t baudRate) {
    USART_TypeDef* usart = Pins::VCP_UART::INSTANCE;

    // Wait for transmission complete (all bits shifted out)
    while ((usart->SR & USART_SR_TC) == 0) {}

    // Small delay to let the last byte propagate through the bridge
    m_clock->delayMs(10);

    // Disable USART
    usart->CR1 &= ~USART_CR1_UE;

    // Recalculate BRR (same formula as initUsart)
    uint32_t integerdivider = ((25 * Pins::VCP_UART::APB1_CLOCK_HZ) / (4 * baudRate));
    uint32_t mantissa = integerdivider / 100;
    uint32_t fraction = ((integerdivider - (mantissa * 100)) * 16 + 50) / 100;
    if (fraction >= 16) {
        mantissa++;
        fraction = 0;
    }
    usart->BRR = (mantissa << 4) | (fraction & 0x0F);

    // Re-enable USART
    usart->CR1 |= USART_CR1_UE;

    // Clear any garbage from RX buffer
    m_rxBuffer.clear();
    (void)usart->DR;  // Clear RXNE if set

    m_baudRate = baudRate;
    return true;
}

void UartTransport::handleIRQ() {
    ::g_usart2IrqCount++;  // Use global scope

    USART_TypeDef* usart = Pins::VCP_UART::INSTANCE;
    uint32_t sr = usart->SR;

    // Check for receive data ready
    if ((sr & USART_SR_RXNE) != 0) {
        uint8_t byte = static_cast<uint8_t>(usart->DR);
        m_rxBuffer.push(byte);  // Overflow counter incremented if full
        ::g_usart2RxCount++;  // Use global scope
    }

    // Clear overrun error if set (read SR then DR)
    if ((sr & USART_SR_ORE) != 0) {
        (void)usart->DR;  // Clear ORE by reading DR
    }
}

} // namespace Comms

// USART2 IRQ Handler
extern "C" void USART2_IRQHandler(void) {
    Comms::UartTransport* instance = Comms::UartTransport::getInstance();
    if (instance != nullptr) {
        instance->handleIRQ();
    }
}
