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

// Harness adapt functions (forward declarations to avoid harness_wiring_map.hpp conflicts)
namespace Drivers { class ICanvas; class IJoystick; class IFlash; }
namespace Scheduler { class IRemoteDisplay; }
namespace Services { class ICanvas; class IJoystick; class IFlash; class IRemoteDisplay; }
namespace Harness {
    Services::ICanvas&   adaptCanvas(Drivers::ICanvas& raw);
    Services::IJoystick& adaptJoystick(Drivers::IJoystick& raw);
    Services::IFlash&    adaptFlash(Drivers::IFlash& raw);
    Services::IRemoteDisplay& adaptRemoteDisplay(Scheduler::IRemoteDisplay& raw);
}

namespace Services {

bool initDisplaySubsystem(Platform::IClock& clock,
                          Platform::IScheduler& scheduler,
                          DisplaySubsystemResult& result)
{
    // Step 1: L4 driver init (through harness → L4 → L5)
    Drivers::DisplayDriverHandle* driverHandle = nullptr;
    if (!Drivers::initDisplayDrivers(driverHandle)) {
        return false;
    }

    // Step 2: Adapt provider types to consumer ports
    Services::ICanvas& lcd = Harness::adaptCanvas(*driverHandle->lcd);
    Services::IJoystick* joystick = (driverHandle->joystick != nullptr)
        ? &Harness::adaptJoystick(*driverHandle->joystick)
        : nullptr;
    Services::IFlash* flash = (driverHandle->norFlash != nullptr)
        ? &Harness::adaptFlash(*driverHandle->norFlash)
        : nullptr;

    // Step 3: L3 service init (flash image + indicator)
    if (flash != nullptr) {
        if (g_flashImageService.init(flash)) {
            result.flashAvailable = true;
        }
    }

    g_indicatorService.init(&lcd);

    // Step 4: Task init (through harness → F_platform)
    Harness::DisplayTaskResult taskResult;
    if (!Harness::initDisplayTask(lcd, joystick,
                                   clock, g_flashImageService, g_indicatorService,
                                   scheduler, taskResult)) {
        return false;
    }

    result.remoteDisplay = &Harness::adaptRemoteDisplay(*taskResult.remoteDisplay);
    return true;
}

} // namespace Services
