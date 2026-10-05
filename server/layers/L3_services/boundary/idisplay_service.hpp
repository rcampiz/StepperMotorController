/**
 * @file idisplay_service.hpp
 * @brief Services:: provider — Display task entry point (H_TASKS_L3)
 *
 * The Display task only waits (50 ms period) and calls tick(). Joystick polling,
 * UI mode, screen navigation, rendering and the remote-display operations all live
 * in Services::DisplayService.
 */
#pragma once

namespace Services {

class IDisplayService {
public:
    virtual ~IDisplayService() = default;

    /// One UI step: poll joystick, update navigation, render. Display task context.
    virtual void tick() = 0;
};

} // namespace Services
