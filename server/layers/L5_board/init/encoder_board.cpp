/**
 * @file encoder_board.cpp
 * @brief L5 board init for encoder subsystem — implements Harness::initEncoderBoard
 */

#include "L5_board/init/encoder_board.hpp"
#include "L5_board/gpio.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/timer/timer_counter_hw.hpp"
#include "L5_board/dma/encoder_sampler.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

// FreeRTOS — for configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"

using Board::GPIO;
using Board::GPIOMode;
using Board::GPIOPull;
using Board::ExtiEdge;
using Board::TimerCounterTIM4;
namespace Pins = Board::Pins;

bool Harness::initEncoderBoard(Harness::EncoderPins& pins)
{
    // Configure EA (PB6) as AF for TIM4_CH1
    static GPIO encoderEA(Pins::Encoder::EA_PORT, Pins::Encoder::EA_PIN);
    encoderEA.setMode(GPIOMode::AltFunc);
    encoderEA.setAlternateFunction(Pins::Encoder::EA_AF);
    encoderEA.setPull(GPIOPull::None);

    // Configure EB (PB7) as AF for TIM4_CH2
    static GPIO encoderEB(Pins::Encoder::EB_PORT, Pins::Encoder::EB_PIN);
    encoderEB.setMode(GPIOMode::AltFunc);
    encoderEB.setAlternateFunction(Pins::Encoder::EB_AF);
    encoderEB.setPull(GPIOPull::None);

    // Configure EZ (PC9) as input with pull-up for index pulse,
    // and enable falling-edge EXTI interrupt (index pulse is active low)
    static GPIO encoderEZ(Pins::Encoder::EZ_PORT, Pins::Encoder::EZ_PIN);
    encoderEZ.setMode(GPIOMode::Input);
    encoderEZ.setPull(GPIOPull::PullUp);
    encoderEZ.enableExti(ExtiEdge::Falling,
                         configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);

    // Create and initialize TIM4 encoder mode timer
    static TimerCounterTIM4 timerCounter;
    timerCounter.init();
    pins.timer = &timerCounter;

    // Construct DMA sampler (reads TIM4->CNT via TIM3 + DMA1_Stream2)
    static Board::EncoderSampler sampler(&TIM4->CNT);
    pins.sampler = &sampler;

    return true;
}
