/**
 * @file iemergency_stop.hpp
 * @brief Interface for emergency stop action
 *
 * Outbound harness interface: comms_task triggers emergency stop through
 * this instead of calling CommandQueue directly. Keeps task decoupled
 * from L3 service layer.
 */

#pragma once

namespace Harness {

class IEmergencyStop {
public:
    virtual ~IEmergencyStop() = default;

    /** @brief Trigger emergency stop (HardStop to motor task) */
    virtual void emergencyStop() = 0;
};

} // namespace Harness
