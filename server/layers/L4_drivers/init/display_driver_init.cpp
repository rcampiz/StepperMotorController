/**
 * @file display_driver_init.cpp
 * @brief L4 init for LCD, joystick and NOR flash — pins from the L4<->L5 harness
 */

#include "harness/pins/display_driver_init.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/joystick.hpp"
#include "L4_drivers/devices/flash_nor.hpp"

namespace Drivers {

static DisplayDriverHandle s_displayHandle;

bool initDisplayDrivers(DisplayDriverHandle*& handle)
{
    const Harness::L4L5Harness& h = Harness::connectL4L5();

    // --- LCD (mandatory) ---
    if (h.lcd == nullptr) {
        return false;
    }
    const Harness::LCDPins& lp = *h.lcd;
    static LCD lcd(*lp.spi, *lp.cs, *lp.dc, *lp.nreset, *lp.te, lp.teSync);
    lcd.init();
    s_displayHandle.lcd = &lcd;

    // --- Joystick (mandatory on GFX01M2) ---
    if (h.joystick == nullptr) {
        return false;
    }
    const Harness::JoystickPins& jp = *h.joystick;
    static Joystick joystick(*jp.left, *jp.right, *jp.up, *jp.down, *jp.center);
    s_displayHandle.joystick = &joystick;

    // --- NOR flash (optional: absent or unresponsive -> nullptr) ---
    if (h.flash != nullptr) {
        const Harness::FlashPins& fp = *h.flash;
        static SPIFlash norFlash(*fp.spi, *fp.cs);
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
