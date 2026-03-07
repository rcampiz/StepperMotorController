/**
 * @file display_driver_init.hpp
 * @brief L4 init for display drivers -- LCD, joystick, NOR flash
 *
 * Returns harness interface pointers. Callers never see concrete L4 types.
 */

#pragma once

namespace Drivers { class ICanvas; class IJoystick; class IFlash; }

namespace Drivers {

/**
 * @brief Display driver init result -- boundary interfaces only
 */
struct DisplayDriverHandle {
    Drivers::ICanvas* lcd = nullptr;
    Drivers::IJoystick* joystick = nullptr;
    Drivers::IFlash* norFlash = nullptr;  // nullptr if no device detected
};

/**
 * @brief Initialize display drivers via cascading init (L4 -> L5)
 *
 * Calls Harness::initLCDBoard/JoystickBoard/FlashBoard, then constructs
 * LCD, Joystick, and probes NOR flash (best-effort).
 *
 * @param handle Output: opaque handle (populated on success)
 * @return true if LCD initialized (flash may be nullptr)
 */
bool initDisplayDrivers(DisplayDriverHandle*& handle);

} // namespace Drivers
