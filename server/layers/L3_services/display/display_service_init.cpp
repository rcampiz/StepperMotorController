/**
 * @file display_service_init.cpp
 * @brief Implements Services::initDisplaySubsystem() — L3 drives the display init cascade
 *
 * L4 devices arrive through H_L3_L4; the Display task is created through the task init
 * contract and calls DisplayService::tick() (H_TASKS_L3).
 */

#include "L3_services/display/display_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/display_task_init.hpp"
#include "L3_services/ui/display_service/display_service.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"
#include "infra/flash_image_service/flash_image_service.hpp"
#include "infra/indicator_service/indicator_service.hpp"

namespace Services {

bool initDisplaySubsystem(Platform::IClock& clock,
                          Platform::IScheduler& scheduler,
                          DisplaySubsystemResult& result)
{
    const Harness::L3L4Harness& h = Harness::connectL3L4(clock);
    if (h.canvas == nullptr) {
        return false;
    }

    if (h.flash != nullptr && g_flashImageService.init(h.flash)) {
        result.flashAvailable = true;
    }

    g_indicatorService.init(h.canvas);

    if (!g_displayService.init(*h.canvas, h.joystick, clock,
                               g_flashImageService, g_indicatorService, UI::g_uiMode)) {
        return false;
    }

    if (!Harness::initDisplayTask(g_displayService, clock, scheduler)) {
        return false;
    }

    result.remoteDisplay = &g_displayService;
    return true;
}

} // namespace Services
