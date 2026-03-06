/**
 * @file display_service_init.cpp
 * @brief Implements Services::initDisplaySubsystem() — L3 drives init cascade
 *
 * Layer-clean: only includes L3 + harness.
 * No L4, L5, F_platform/tasks, or F_platform/rtos includes.
 * All Platform deps injected as parameters.
 */

#include "L3_services/display/display_service_init.hpp"

// L4 init through harness
#include "harness/pins/display_driver_init.hpp"

// L3 services that need init before task creation
#include "infra/flash_image_service/flash_image_service.hpp"
#include "infra/indicator_service/indicator_service.hpp"

// Task init through harness
#include "harness/pins/display_task_init.hpp"

namespace Services {

bool initDisplaySubsystem(Harness::IClock& clock,
                          Harness::IScheduler& scheduler,
                          DisplaySubsystemResult& result)
{
    // Step 1: L4 driver init (through harness → L4 → L5)
    Drivers::DisplayDriverHandle* driverHandle = nullptr;
    if (!Drivers::initDisplayDrivers(driverHandle)) {
        return false;
    }

    // Step 2: L3 service init (flash image + indicator)
    if (driverHandle->norFlash != nullptr) {
        if (g_flashImageService.init(driverHandle->norFlash)) {
            result.flashAvailable = true;
        }
    }

    g_indicatorService.init(driverHandle->lcd);

    // Step 3: Task init (through harness → F_platform)
    Harness::DisplayTaskResult taskResult;
    if (!Harness::initDisplayTask(*driverHandle->lcd, driverHandle->joystick,
                                   clock, g_flashImageService, g_indicatorService,
                                   scheduler, taskResult)) {
        return false;
    }

    result.remoteDisplay = taskResult.remoteDisplay;
    return true;
}

} // namespace Services
