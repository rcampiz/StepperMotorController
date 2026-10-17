/**
 * @file display_service_init.hpp
 * @brief L3 init contract: bind the LCD, joystick, flash images and the DisplayService
 *
 * Creates no task. The composition root hands displayService to the task sheet.
 */
#pragma once

namespace Platform { class IClock; class ILock; }

namespace Services {

class IDisplayService;
class IDisplayControl;
class IRemoteDisplay;

struct DisplaySubsystemResult {
    Services::IDisplayService* displayService = nullptr;  ///< Display task entry (H_TASKS_L3)
    Services::IDisplayControl* displayControl = nullptr;  ///< debug enable/disable (Comms task)
    Services::IRemoteDisplay*  remoteDisplay = nullptr;   ///< Remote display interface
    bool flashAvailable = false;                          ///< NOR flash detected
    bool identityValid = false;                           ///< DEV:ID / DEV:ROLE record read from NOR flash
};

/// LCD, joystick, NOR flash images and, on the same NOR flash, the device identity store
/// (g_deviceConfig, guarded by deviceConfigLock). Without a flash the identity store stays
/// uninitialised and DEV:ID / DEV:ROLE setters answer "Flash write failed".
bool initDisplaySubsystem(Platform::IClock& clock, Platform::ILock& deviceConfigLock,
                          DisplaySubsystemResult& result);

} // namespace Services
