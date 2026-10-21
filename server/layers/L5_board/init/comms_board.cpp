/**
 * @file comms_board.cpp
 * @brief L5 init for the host byte channel: USART2 (VCP) or SEGGER RTT
 */

#include "harness/pins/comms_board.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/uart/uart_hardware.hpp"
#include "L5_board/rtt/rtt_channel.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"

using Board::UartHardware;
namespace Pins = Board::Pins;

// NVIC 1: above the FreeRTOS critical sections (5) and the SEGGER RTT lock (2). The receive ISR
// makes no RTOS call (the Comms task's wake is deferred, the out-of-band ESTOP is atomics and one
// GPIO write); bench 2026-10-20: at NVIC 6 back-to-back bytes were lost from 921600 baud up.
static_assert(configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY > 2, "the receive ISR must stay above the RTOS");
constexpr uint8_t UART_RX_IRQ_PRIORITY = 1;

bool Harness::initCommsBoard(Harness::CommsBoardHw& hw, Harness::CommsChannel type)
{
    switch (type) {
    case CommsChannel::VCP_UART: {
        static UartHardware uartHw(
            Pins::VCP_UART::INSTANCE, Pins::VCP_UART::PORT,
            Pins::VCP_UART::TX_PIN, Pins::VCP_UART::RX_PIN,
            Pins::VCP_UART::AF, Pins::VCP_UART::APB1_CLOCK_HZ, 115200,
            UART_RX_IRQ_PRIORITY);
        hw.channel = &uartHw;
    } break;

    case CommsChannel::RTT: {
        static Board::RttChannel rttHw(0);
        hw.channel = &rttHw;
    } break;
    }
    return hw.channel != nullptr;
}

// X_L5_L1 (harness_l5_l1_bytes.hpp): optional RX wake of the Comms task
#include "harness/defs/harness_l5_l1_bytes.hpp"

void Harness::setCommsRxWake(Platform::ITaskNotifier* notifier, Platform::TaskHandle task)
{
    if (Board::UartHardware* uart = Board::UartHardware::getInstance()) {
        uart->setRxWake(notifier, task);
    }
}

void Harness::setCommsTxClock(Platform::IClock* clock)
{
    if (Board::UartHardware* uart = Board::UartHardware::getInstance()) {
        uart->setTxClock(clock);
    }
}
