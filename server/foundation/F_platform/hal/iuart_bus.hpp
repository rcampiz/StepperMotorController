/**
 * @file iuart_bus.hpp
 * @brief Abstract UART bus interface
 *
 * Decouples L1 transport from CMSIS register access.
 * Implemented by UartHardware (L5_board) which owns all
 * GPIO, RCC, USART, and NVIC configuration.
 *
 * No CMSIS types appear in this interface.
 */

#ifndef IUART_BUS_HPP
#define IUART_BUS_HPP

#include <stdint.h>

class IUartBus {
public:
    virtual ~IUartBus() = default;

    /**
     * @brief Initialize GPIO, clocks, USART peripheral, and NVIC
     * @param baudRate  Desired baud rate (e.g. 115200)
     * @param irqPriority  NVIC priority for the RX interrupt
     * @return true on success
     */
    virtual bool init(uint32_t baudRate, uint8_t irqPriority) = 0;

    /**
     * @brief Blocking write of one byte (waits for TXE, writes to DR)
     */
    virtual void writeByte(uint8_t byte) = 0;

    /**
     * @brief Check if a received byte is waiting (RXNE flag)
     */
    virtual bool hasRxData() const = 0;

    /**
     * @brief Read one received byte from DR (caller must check hasRxData first)
     */
    virtual uint8_t readRxByte() = 0;

    /**
     * @brief Check if an overrun error has occurred (ORE flag)
     */
    virtual bool hasOverrunError() const = 0;

    /**
     * @brief Clear error flags (ORE, etc.) by reading SR then DR
     */
    virtual void clearErrors() = 0;

    /**
     * @brief Block until transmission is fully complete (TC flag)
     */
    virtual void waitTxComplete() = 0;

    /**
     * @brief Reconfigure baud rate (disable USART, update BRR, re-enable)
     * @param baudRate  New baud rate
     * @return true on success
     */
    virtual bool setBaudRate(uint32_t baudRate) = 0;
};

#endif // IUART_BUS_HPP
