/**
 * @file encoder_service_init.hpp
 * @brief L3 init contract: bind the encoder hardware (optional) and the EncoderService
 *
 * Creates no task. encoderService is nullptr when the hardware is absent; the composition
 * root then skips the Encoder task.
 */
#pragma once

namespace Platform { class IClock; class ICriticalSection; class IMicrosecondClock; }

namespace Services {

class IEncoder;
class IEncoderFilterControl;
class IEncoderService;

struct EncoderSubsystemResult {
    bool encoderAvailable = false;                             ///< true if encoder hardware present
    Services::IEncoderService* encoderService = nullptr;       ///< Encoder task entry (H_TASKS_L3)
    Services::IEncoder* encoder = nullptr;                     ///< Encoder data access
    Services::IEncoderFilterControl* filterControl = nullptr;  ///< Filter control
};

/// Always returns true; absent or not-ready hardware leaves encoderAvailable false.
bool initEncoderSubsystem(Platform::IClock& clock,
                          Platform::ICriticalSection& critical,
                          Platform::IMicrosecondClock& usClock,
                          EncoderSubsystemResult& result);

} // namespace Services
