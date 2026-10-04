/**
 * @file display_driver_init.cpp
 * @brief L4 init for display drivers
 */

#include "harness/pins/display_driver_init.hpp"
#include "harness/pins/display_board.hpp"
#include "harness/adapters/spi_adapter.hpp"
#include "harness/adapters/gpio_adapter.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/joystick.hpp"
#include "L4_drivers/devices/flash_nor.hpp"

namespace Drivers {

static DisplayDriverHandle s_displayHandle;

bool initDisplayDrivers(DisplayDriverHandle*& handle)
{
    // LCD — SPI1 + GPIO (adapt Board:: to Drivers::)
    Harness::LCDPins lcdPins;
    if (!Harness::initLCDBoard(lcdPins)) {
        return false;
    }
    static Harness::SPIAdapter lcdSpiAdapter(*lcdPins.spi);
    static Harness::GPIOAdapter lcdCsAdapter(*lcdPins.cs);
    static Harness::GPIOAdapter lcdDcAdapter(*lcdPins.dc);
    static Harness::GPIOAdapter lcdNresetAdapter(*lcdPins.nreset);
    static Harness::GPIOAdapter lcdTeAdapter(*lcdPins.te);
    static LCD lcd(lcdSpiAdapter, lcdCsAdapter, lcdDcAdapter, lcdNresetAdapter, lcdTeAdapter);
    lcd.init();
    s_displayHandle.lcd = &lcd;

    // Joystick — 5 GPIO (adapt Board:: to Drivers::)
    Harness::JoystickPins joyPins;
    if (!Harness::initJoystickBoard(joyPins)) {
        return false;
    }
    static Harness::GPIOAdapter joyLeftAdapter(*joyPins.left);
    static Harness::GPIOAdapter joyRightAdapter(*joyPins.right);
    static Harness::GPIOAdapter joyUpAdapter(*joyPins.up);
    static Harness::GPIOAdapter joyDownAdapter(*joyPins.down);
    static Harness::GPIOAdapter joyCenterAdapter(*joyPins.center);
    static Joystick joystick(joyLeftAdapter, joyRightAdapter, joyUpAdapter, joyDownAdapter, joyCenterAdapter);
    s_displayHandle.joystick = &joystick;

    // NOR Flash — SPI2 + CS (best-effort probe, adapt Board:: to Drivers::)
    Harness::FlashPins flashPins;
    if (Harness::initFlashBoard(flashPins)) {
        static Harness::SPIAdapter flashSpiAdapter(*flashPins.spi);
        static Harness::GPIOAdapter flashCsAdapter(*flashPins.cs);
        static SPIFlash norFlash(flashSpiAdapter, flashCsAdapter);
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
