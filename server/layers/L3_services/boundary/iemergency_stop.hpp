/**
 * @file boundary/iemergencystop.hpp
 * @brief Consumer port — Services side of the Scheduler::IEmergencyStop harness connection
 *
 * Structurally identical to Scheduler::IEmergencyStop.
 * The harness adapter bridges the provider to this consumer port.
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
