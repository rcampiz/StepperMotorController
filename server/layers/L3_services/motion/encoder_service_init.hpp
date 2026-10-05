/**
 * @file encoder_service_init.hpp
 * @brief Services::initEncoderSubsystem() — L3 entry point for the encoder cascade
 */
#pragma once

namespace Platform { class IClock; class IScheduler; class ICriticalSection; class IMicrosecondClock; }

namespace Services {

class IEncoder;
class IEncoderFilterControl;

struct EncoderSubsystemResult {
    bool encoderAvailable = false;                 ///< true if encoder hardware present
    Services::IEncoder* encoder = nullptr;         ///< Encoder data access
    Services::IEncoderFilterControl* filterControl = nullptr;  ///< Filter control
};

/// Always returns true unless task creation fails; absent hardware leaves encoderAvailable false.
bool initEncoderSubsystem(Platform::IClock& clock,
                          Platform::IScheduler& scheduler,
                          Platform::ICriticalSection& critical,
                          Platform::IMicrosecondClock& usClock,
                          EncoderSubsystemResult& result);

} // namespace Services
