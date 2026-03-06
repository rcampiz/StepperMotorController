/**
 * @file display_driver_init.cpp
 * @brief L4 init for display drivers
 */

#include "L4_drivers/init/display_driver_init.hpp"
#include "harness/pins/display_board.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/joystick.hpp"
#include "L4_drivers/devices/flash_nor.hpp"

namespace Drivers {

static DisplayDriverHandle s_displayHandle;

bool initDisplayDrivers(DisplayDriverHandle*& handle)
{
    // LCD — SPI1 + GPIO
    Harness::LCDPins lcdPins;
    if (!Harness::initLCDBoard(lcdPins)) {
        return false;
    }
    static LCD lcd(*lcdPins.spi, *lcdPins.cs, *lcdPins.dc, *lcdPins.nreset, *lcdPins.te);
    lcd.init();
    s_displayHandle.lcd = &lcd;

    // Joystick — 5 GPIO
    Harness::JoystickPins joyPins;
    if (!Harness::initJoystickBoard(joyPins)) {
        return false;
    }
    static Joystick joystick(*joyPins.left, *joyPins.right, *joyPins.up, *joyPins.down, *joyPins.center);
    s_displayHandle.joystick = &joystick;

    // NOR Flash — SPI2 + CS (best-effort probe)
    Harness::FlashPins flashPins;
    if (Harness::initFlashBoard(flashPins)) {
        static SPIFlash norFlash(*flashPins.spi, *flashPins.cs);
        norFlash.init();

        auto jedec = norFlash.readJEDEC();
        if (jedec.manufacturer != 0x00 && jedec.manufacturer != 0xFF) {
            s_displayHandle.norFlash = &norFlash;
        }
    }

    handle = &s_displayHandle;
    return true;
}

} // namespace Drivers
