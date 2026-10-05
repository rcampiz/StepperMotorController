/**
 * @file boundary/iemergency_stop.hpp
 * @brief Emergency-stop trigger used by the Comms task path (L3-internal interface)
 */

#pragma once

namespace Services {

class IEmergencyStop {
public:
    virtual ~IEmergencyStop() = default;

    /** @brief Trigger emergency stop (HardStop to motor task) */
    virtual void emergencyStop() = 0;
};

} // namespace Services
