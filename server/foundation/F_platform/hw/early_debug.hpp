/**
 * @file early_debug.hpp
 * @brief Pre-FreeRTOS debug UART output
 *
 * Bare-metal USART2 output for debugging init failures before the scheduler
 * starts. Uses PA2 (TX) / PA3 (RX) at 115200 baud via ST-LINK VCP.
 * Header-only — all functions are inline.
 */

#ifndef EARLY_DEBUG_HPP
#define EARLY_DEBUG_HPP

#include "X_vendor/CMSIS/stm32f401xe.h"
#include <stdint.h>

namespace EarlyDebug {

inline bool& initialized() {
    static bool s = false;
    return s;
}

inline void initUART() {
    // Enable clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    // Brief delay
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // Configure PA2 (TX) as alternate function
    GPIOA->MODER &= ~(3UL << (2 * 2));
    GPIOA->MODER |= (2UL << (2 * 2));   // AF mode
    GPIOA->OSPEEDR |= (3UL << (2 * 2)); // High speed
    GPIOA->AFR[0] &= ~(0xFUL << (2 * 4));
    GPIOA->AFR[0] |= (7UL << (2 * 4));  // AF7 = USART2

    // Configure PA3 (RX) as alternate function
    GPIOA->MODER &= ~(3UL << (3 * 2));
    GPIOA->MODER |= (2UL << (3 * 2));   // AF mode
    GPIOA->PUPDR &= ~(3UL << (3 * 2));
    GPIOA->PUPDR |= (1UL << (3 * 2));   // Pull-up
    GPIOA->AFR[0] &= ~(0xFUL << (3 * 4));
    GPIOA->AFR[0] |= (7UL << (3 * 4));  // AF7 = USART2

    // Disable USART for config
    USART2->CR1 = 0;

    // Baud rate: 115200 at 42 MHz APB1
    // BRR = 42000000 / (16 * 115200) = 22.786
    // Mantissa=22, Fraction=13 (0.786*16=12.58 → round to 13)
    USART2->BRR = (22 << 4) | 13;

    // Enable TX, RX, USART
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    initialized() = true;
}

inline void putChar(char c) {
    while ((USART2->SR & USART_SR_TXE) == 0) {}
    USART2->DR = c;
}

inline void print(const char* str) {
    if (!initialized()) return;
    while (*str) {
        putChar(*str++);
    }
}

inline void println(const char* str) {
    print(str);
    print("\r\n");
}

inline void printUint(uint32_t val) {
    char buf[12];
    char* p = buf + sizeof(buf) - 1;
    *p = '\0';
    do {
        *(--p) = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    print(p);
}

} // namespace EarlyDebug

#endif // EARLY_DEBUG_HPP
