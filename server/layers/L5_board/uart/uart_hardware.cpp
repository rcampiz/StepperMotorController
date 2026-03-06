/**
 * @file uart_hardware.cpp
 * @brief UartHardware static members and USART2 ISR handler
 */

#include "L5_board/uart/uart_hardware.hpp"

// Singleton instance for ISR access
Board::UartHardware* Board::UartHardware::s_instance = nullptr;

bool Board::UartHardware::init() {
    // Enforce single instance — ISR can only route to one instance
    if (s_instance != nullptr) {
        return false;
    }
    s_instance = this;

    initGPIO();
    initUSART();
    return true;
}

// Debug: IRQ call counters
volatile uint32_t g_usart2IrqCount = 0;
volatile uint32_t g_usart2RxCount = 0;

// USART2 IRQ Handler
extern "C" void USART2_IRQHandler(void) {
    g_usart2IrqCount++;

    Board::UartHardware* instance = Board::UartHardware::getInstance();
    if (instance != nullptr) {
        instance->handleIRQ();
        g_usart2RxCount++;
    }
}
