/**
 * @file uart_transport.hpp
 * @brief USART2 VCP transport implementation
 *
 * Uses USART2 connected to ST-LINK/J-Link Virtual COM Port
 * via solder bridges SB13/SB14 (per UM1724).
 */

#ifndef UART_TRANSPORT_HPP
#define UART_TRANSPORT_HPP

#include "comms/transport_interface.hpp"
#include "stm32f401xe.h"
#include "board/board_pins.hpp"

namespace Comms {

class UartTransport : public ITransport {
public:
    /**
     * @brief Construct UART transport
     * @param baudRate Baud rate (default 115200)
     */
    explicit UartTransport(uint32_t baudRate = 115200);

    bool init() override;
    bool available() override;
    size_t read(uint8_t* buffer, size_t maxLen) override;
    bool readByte(uint8_t& byte, uint32_t timeoutMs) override;
    size_t write(const uint8_t* data, size_t len) override;
    size_t print(const char* str) override;
    size_t println(const char* str) override;
    void flush() override;

private:
    uint32_t m_baudRate;

    void initGpio();
    void initUsart();
};

} // namespace Comms

#endif // UART_TRANSPORT_HPP
