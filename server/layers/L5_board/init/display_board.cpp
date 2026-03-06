/**
 * @file display_board.cpp
 * @brief L5 board init for display subsystem — implements Harness:: init functions
 */

#include "L5_board/init/display_board.hpp"
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

// ============================================================================
// LCD — SPI1 + 4 GPIO pins
// ============================================================================

bool Harness::initLCDBoard(Harness::LCDPins& pins)
{
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

    static GPIO lcdCS(Pins::GFX_LCD::CS_PORT, Pins::GFX_LCD::CS_PIN);
    lcdCS.setMode(GPIOMode::Output);
    lcdCS.setOutputType(GPIOOutputType::PushPull);
    lcdCS.setSpeed(GPIOSpeed::VeryHigh);
    lcdCS.write(true);  // Deasserted
    pins.cs = &lcdCS;

    static GPIO lcdDC(Pins::GFX_LCD::DC_PORT, Pins::GFX_LCD::DC_PIN);
    lcdDC.setMode(GPIOMode::Output);
    lcdDC.setOutputType(GPIOOutputType::PushPull);
    lcdDC.setSpeed(GPIOSpeed::VeryHigh);
    pins.dc = &lcdDC;

    static GPIO lcdNReset(Pins::GFX_LCD::NRESET_PORT, Pins::GFX_LCD::NRESET_PIN);
    lcdNReset.setMode(GPIOMode::Output);
    lcdNReset.setOutputType(GPIOOutputType::PushPull);
    lcdNReset.setSpeed(GPIOSpeed::Low);
    lcdNReset.write(true);  // Not in reset
    pins.nreset = &lcdNReset;

    static GPIO lcdTE(Pins::GFX_LCD::TE_PORT, Pins::GFX_LCD::TE_PIN);
    lcdTE.setMode(GPIOMode::Input);
    lcdTE.setPull(GPIOPull::None);
    pins.te = &lcdTE;

    return true;
}

// ============================================================================
// Joystick — 5 GPIO pins (active low, pull-up)
// ============================================================================

bool Harness::initJoystickBoard(Harness::JoystickPins& pins)
{
    static GPIO joyLeft(Pins::Joystick::LEFT_PORT, Pins::Joystick::LEFT_PIN);
    joyLeft.setMode(GPIOMode::Input);
    joyLeft.setPull(GPIOPull::PullUp);
    pins.left = &joyLeft;

    static GPIO joyRight(Pins::Joystick::RIGHT_PORT, Pins::Joystick::RIGHT_PIN);
    joyRight.setMode(GPIOMode::Input);
    joyRight.setPull(GPIOPull::PullUp);
    pins.right = &joyRight;

    static GPIO joyUp(Pins::Joystick::UP_PORT, Pins::Joystick::UP_PIN);
    joyUp.setMode(GPIOMode::Input);
    joyUp.setPull(GPIOPull::PullUp);
    pins.up = &joyUp;

    static GPIO joyDown(Pins::Joystick::DOWN_PORT, Pins::Joystick::DOWN_PIN);
    joyDown.setMode(GPIOMode::Input);
    joyDown.setPull(GPIOPull::PullUp);
    pins.down = &joyDown;

    static GPIO joyCenter(Pins::Joystick::CENTER_PORT, Pins::Joystick::CENTER_PIN);
    joyCenter.setMode(GPIOMode::Input);
    joyCenter.setPull(GPIOPull::PullUp);
    pins.center = &joyCenter;

    return true;
}

// ============================================================================
// NOR Flash — SPI2 + CS GPIO
// ============================================================================

bool Harness::initFlashBoard(Harness::FlashPins& pins)
{
    Harness::ISPIBus* spi2 = g_spiManager.getSPI2();
    if (spi2 == nullptr) {
        return false;
    }
    pins.spi = spi2;

    static GPIO flashCS(Pins::GFX_Flash::CS_PORT, Pins::GFX_Flash::CS_PIN);
    flashCS.setMode(GPIOMode::Output);
    flashCS.setOutputType(GPIOOutputType::PushPull);
    flashCS.setSpeed(GPIOSpeed::VeryHigh);
    flashCS.write(true);  // Deasserted
    pins.cs = &flashCS;

    return true;
}
