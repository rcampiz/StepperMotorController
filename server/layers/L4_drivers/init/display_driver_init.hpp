/**
 * @file display_driver_init.hpp
 * @brief L4 init for display drivers — LCD, joystick, NOR flash
 *
 * Returns harness interface pointers. Callers never see concrete L4 types.
 */

#pragma once

namespace Harness { class ICanvas; class IJoystick; class IFlash; }

namespace Drivers {

/**
 * @brief Display driver init result — harness interfaces only
 */
struct DisplayDriverHandle {
    Harness::ICanvas* lcd = nullptr;
    Harness::IJoystick* joystick = nullptr;
    Harness::IFlash* norFlash = nullptr;  // nullptr if no device detected
};

/**
 * @brief Initialize display drivers via cascading init (L4 → L5)
 *
 * Calls Harness::initLCDBoard/JoystickBoard/FlashBoard, then constructs
 * LCD, Joystick, and probes NOR flash (best-effort).
 *
 * @param handle Output: opaque handle (populated on success)
 * @return true if LCD initialized (flash may be nullptr)
 */
bool initDisplayDrivers(DisplayDriverHandle*& handle);

} // namespace Drivers
