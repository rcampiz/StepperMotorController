/**
 * @file display_service_init.hpp
 * @brief L3 init contract: bind the LCD, joystick, flash images and the DisplayService
 *
 * Creates no task. The composition root hands displayService to the task sheet.
 */
#pragma once

namespace Platform { class IClock; }

namespace Services {

class IDisplayService;
class IDisplayControl;
class IRemoteDisplay;

struct DisplaySubsystemResult {
    Services::IDisplayService* displayService = nullptr;  ///< Display task entry (H_TASKS_L3)
    Services::IDisplayControl* displayControl = nullptr;  ///< debug enable/disable (Comms task)
    Services::IRemoteDisplay*  remoteDisplay = nullptr;   ///< Remote display interface
    bool flashAvailable = false;                          ///< NOR flash detected
};

bool initDisplaySubsystem(Platform::IClock& clock, DisplaySubsystemResult& result);

} // namespace Services
