/**
 * @file motor_board.cpp
 * @brief L5 board init for motor subsystem — implements Harness::initMotorBoard
 */

#include "harness/defs/harness_l4_l5.hpp"
#include "L5_board/gpio.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/spi/spi_manager.hpp"


using Board::GPIO;
using Board::GPIOMode;
using Board::GPIOOutputType;
using Board::GPIOSpeed;
using Board::GPIOPull;
using Board::g_spiManager;
namespace Pins = Board::Pins;

bool Harness::initMotorBoard(Harness::MotorPins& pins)
{
    // SPI1 from manager (shared with LCD)
    Board::ISPIBus* spi1 = g_spiManager.getSPI1();
    if (spi1 == nullptr) {
        return false;
    }

    pins.spi = spi1;

    // GPIO objects for PowerSTEP01 pins
    static GPIO motorCS(Pins::IHM03A1::CS_PORT, Pins::IHM03A1::CS_PIN);
    motorCS.setMode(GPIOMode::Output);
    motorCS.setOutputType(GPIOOutputType::PushPull);
    motorCS.setSpeed(GPIOSpeed::VeryHigh);
    motorCS.write(true);  // Deasserted
    pins.cs = &motorCS;

    static GPIO motorStbyRst(Pins::IHM03A1::STBY_RST_PORT, Pins::IHM03A1::STBY_RST_PIN);
    motorStbyRst.setMode(GPIOMode::Output);
    motorStbyRst.setOutputType(GPIOOutputType::PushPull);
    motorStbyRst.setSpeed(GPIOSpeed::Low);
    pins.stbyRst = &motorStbyRst;

    // BUSY/SYNC: the Motor task selects the chip's SYNC output (a pulse per 1/8 step) on
    // this pin and TIM3 counts the pulses (AF2 = TIM3_CH2); the level is still readable.
    // The firmware takes the busy state from the status register, not from this pin.
    static GPIO motorBusy(Pins::IHM03A1::BUSY_PORT, Pins::IHM03A1::BUSY_PIN);
    motorBusy.setMode(GPIOMode::AltFunc);
    motorBusy.setAlternateFunction(2);
    motorBusy.setPull(GPIOPull::PullUp);
    pins.busy = &motorBusy;

    static GPIO motorFlag(Pins::IHM03A1::FLAG_PORT, Pins::IHM03A1::FLAG_PIN);
    motorFlag.setMode(GPIOMode::Input);
    motorFlag.setPull(GPIOPull::PullUp);
    pins.flag = &motorFlag;

    return true;
}
