/**
 * @file display_task_init.hpp
 * @brief Harness init contract — display task + RTOS creation
 *
 * All dependencies injected as harness interfaces.
 * Declared in harness so L3 can init the display task without
 * including L4 or F_platform/tasks headers.
 */

#pragma once

namespace Harness {

class ICanvas;
class IJoystick;
class IClock;
class IRemoteDisplay;
class IScheduler;
class IFlashImageAccess;
class IIndicatorRenderer;

/**
 * @brief Result of display task initialization
 */
struct DisplayTaskResult {
    IRemoteDisplay* remoteDisplay = nullptr;  ///< Remote display interface (valid after init)
};

/**
 * @brief Initialize display task and create RTOS task
 *
 * @param lcd           LCD canvas interface (from L4 init)
 * @param joystick      Joystick interface (may be nullptr)
 * @param clock         System clock (injected from Platform)
 * @param flashImages   Flash image storage service (from L3 init)
 * @param indicator     Motion indicator renderer (from L3 init)
 * @param scheduler     Scheduler for RTOS task creation
 * @param result        Output: remote display interface
 * @return true on success
 */
bool initDisplayTask(ICanvas& lcd,
                     IJoystick* joystick,
                     IClock& clock,
                     IFlashImageAccess& flashImages,
                     IIndicatorRenderer& indicator,
                     IScheduler& scheduler,
                     DisplayTaskResult& result);

} // namespace Harness
