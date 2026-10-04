/**
 * @file boundary/imotorcontrolservice.hpp
 * @brief Consumer port — Scheduler side of the Services::IMotorControlService harness connection
 *
 * Structurally identical to Services::IMotorControlService.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "L3_services/boundary/imotor_driver.hpp"
#include "harness/pins/motor_debug_params.hpp"
#include "harness/pins/imotor_command_sink.hpp"

namespace Scheduler {

class IMotorControlService {
public:
    virtual ~IMotorControlService() = default;

    virtual void processCommand(const Scheduler::MotorCommand& cmd, Services::IMotorDriver& driver) = 0;
    virtual void periodicUpdate(Services::IMotorDriver& driver) = 0;
    virtual void applyConfig(Services::IMotorDriver& driver) = 0;
    virtual bool setStepModeSafe(Services::IMotorDriver& driver, uint8_t mode, uint8_t& readback) = 0;
    virtual void reinit(Services::IMotorDriver& driver) = 0;
    virtual void getDebugInfo(Services::IMotorDriver& driver, Harness::MotorDebugParams& info) = 0;
};

} // namespace Scheduler
