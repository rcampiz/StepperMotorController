/**
 * @file display_service_init.cpp
 * @brief Implements Services::initDisplaySubsystem() — L3 drives the display init cascade
 */

#include "L3_services/display/display_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/ui/display_service/display_service.hpp"
#include "L3_services/ui/ui_mode/ui_mode.hpp"
#include "infra/flash_image_service/flash_image_service.hpp"
#include "infra/indicator_service/indicator_service.hpp"
#include "L3_services/config/device_config/device_config.hpp"

namespace Services {

bool initDisplaySubsystem(Platform::IClock& clock, Platform::ILock& deviceConfigLock,
                          DisplaySubsystemResult& result)
{
    const Harness::L3L4Harness& h = Harness::connectL3L4(clock);
    if (h.canvas == nullptr) {
        return false;
    }

    if (h.flash != nullptr && g_flashImageService.init(h.flash)) {
        result.flashAvailable = true;
        // Device identity lives in the reserved first sectors of the same NOR flash
        // (0x000000-0x001FFF, below FLASH_IMAGE_BASE). Not initialising it left the
        // setters with a null lock (crash on DEV:ID / DEV:ROLE) since f874dd5.
        result.identityValid = g_deviceConfig.init(*h.flash, deviceConfigLock);
    }

    g_indicatorService.init(h.canvas);

    if (!g_displayService.init(*h.canvas, h.joystick, clock,
                               g_flashImageService, g_indicatorService, UI::g_uiMode)) {
        return false;
    }

    result.displayService = &g_displayService;
    result.displayControl = &g_displayService;
    result.remoteDisplay = &g_displayService;
    return true;
}

} // namespace Services
