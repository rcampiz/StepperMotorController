/**
 * @file motor_board.cpp
 * @brief L5 board init for motor subsystem — implements Harness::initMotorBoard
 */

#include "L5_board/init/motor_board.hpp"
#include "L5_board/gpio.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/spi/spi_manager.hpp"

// Tracing (conditional)
#include "harness/trace/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_spi_bus.hpp"
#endif

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
    Harness::ISPIBus* spi1 = g_spiManager.getSPI1();
    if (spi1 == nullptr) {
        return false;
    }

#ifdef ENABLE_INTERFACE_TRACE
    static Harness::TracedSPIBus s_tracedSPI(*spi1);
    pins.spi = &s_tracedSPI;
#else
    pins.spi = spi1;
#endif

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

    static GPIO motorBusy(Pins::IHM03A1::BUSY_PORT, Pins::IHM03A1::BUSY_PIN);
    motorBusy.setMode(GPIOMode::Input);
    motorBusy.setPull(GPIOPull::PullUp);
    pins.busy = &motorBusy;

    static GPIO motorFlag(Pins::IHM03A1::FLAG_PORT, Pins::IHM03A1::FLAG_PIN);
    motorFlag.setMode(GPIOMode::Input);
    motorFlag.setPull(GPIOPull::PullUp);
    pins.flag = &motorFlag;

    return true;
}
