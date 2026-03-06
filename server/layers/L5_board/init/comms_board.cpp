/**
 * @file comms_board.cpp
 * @brief L5 board init for comms subsystem — implements Harness::initCommsBoard
 */

#include "L5_board/init/comms_board.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/uart/uart_hardware.hpp"

// FreeRTOS — for configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"

using Board::UartHardware;
namespace Pins = Board::Pins;

bool Harness::initCommsBoard(Harness::CommsBoardHw& hw)
{
    static UartHardware uartHw(
        Pins::VCP_UART::INSTANCE, Pins::VCP_UART::PORT,
        Pins::VCP_UART::TX_PIN, Pins::VCP_UART::RX_PIN,
        Pins::VCP_UART::AF, Pins::VCP_UART::APB1_CLOCK_HZ, 115200,
        configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1);

    hw.channel = &uartHw;
    return true;
}
