/**
 * @file boundary/imotor_task_control.hpp
 * @brief Motor-side control used by the driver dispatcher and the debug service (Comms task)
 *
 * Everything here is answered by the Motor task through messages or flags; the Comms task
 * never suspends the Motor task. setStepModeSafe sends a SetStepMode command and waits
 * (bounded) for the Motor task's readback. setDebugPaused stops the Motor task's SPI traffic
 * (periodic status polls and command execution) until resumed. (L3-internal interface)
 */
#pragma once
#include "harness/pins/motor_debug_params.hpp"
#include <stdint.h>

namespace Services {

class IMotorTaskControl {
public:
    virtual ~IMotorTaskControl() = default;
    virtual void reinit() = 0;
    virtual bool applyConfig() = 0;
    virtual bool getDebugInfo(Harness::MotorDebugParams& out) = 0;
    /// Queue a SetStepMode command and wait up to STEP_MODE_WAIT_MS for the Motor task's
    /// readback. Returns false (readback 0xFF) on queue-full or timeout.
    virtual bool setStepModeSafe(uint8_t mode, uint8_t& readback) = 0;
    /// Debug: true = the Motor task stops touching the driver (no SPI traffic) until false.
    virtual void setDebugPaused(bool paused) = 0;
    virtual bool isDebugPaused() const = 0;
};

} // namespace Services
