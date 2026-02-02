/**
 * @file uart_transport.cpp
 * @brief USART2 VCP transport implementation
 */

#include "comms/uart_transport.hpp"
#include "board/board_pins.hpp"
#include <string.h>

namespace Comms {

UartTransport::UartTransport(uint32_t baudRate) : m_baudRate(baudRate) {}

bool UartTransport::init() {
  initGpio();
  initUsart();
  return true;
}

void UartTransport::initGpio() {
  // TODO: Configure PA2 (TX) and PA3 (RX) as AF7 for USART2
  // Enable GPIOA clock
  // Configure pins as alternate function
}

void UartTransport::initUsart() {
  // TODO: Configure USART2
  // Enable USART2 clock
  // Set baud rate from m_baudRate and Pins::VCP_UART::APB1_CLOCK_HZ
  // Configure 8N1
  // Enable TX and RX
}

bool UartTransport::available() {
  // TODO: Check USART2->SR RXNE flag
  return false;
}

size_t UartTransport::read(uint8_t *buffer, size_t maxLen) {
  // TODO: Read available bytes from USART2
  (void)buffer;
  (void)maxLen;
  return 0;
}

bool UartTransport::readByte(uint8_t &byte, uint32_t timeoutMs) {
  // TODO: Blocking read with timeout
  (void)byte;
  (void)timeoutMs;
  return false;
}

size_t UartTransport::write(const uint8_t *data, size_t len) {
  // TODO: Write bytes to USART2
  (void)data;
  (void)len;
  return 0;
}

size_t UartTransport::print(const char *str) {
  return write(reinterpret_cast<const uint8_t *>(str), strlen(str));
}

size_t UartTransport::println(const char *str) {
  size_t n = print(str);
  n += write(reinterpret_cast<const uint8_t *>("\r\n"), 2);
  return n;
}

void UartTransport::flush() {
  // TODO: Wait for TX complete flag
}

} // namespace Comms
