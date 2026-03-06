/**
 * @file motor_driver_dispatch.hpp
 * @brief L3 implementation of IMotorDriverDispatcher
 *
 * Motor lifecycle operations delegate to injected IMotorTaskControl.
 * Fault enable flags read from motion.stepper.config.
 */

#pragma once

#include "harness/pins/imotor_driver_dispatcher.hpp"
#include "harness/pins/imotor_task_control.hpp"

namespace Services {

class MotorDriverDispatch : public Harness::IMotorDriverDispatcher {
public:
    MotorDriverDispatch() = default;
    explicit MotorDriverDispatch(Harness::IMotorTaskControl* ctrl)
        : m_motorCtrl(ctrl) {}

    void setMotorCtrl(Harness::IMotorTaskControl* m) { m_motorCtrl = m; }

    FaultEnableFlags getFaultEnable() override;
    void motorReinit() override;
    bool motorApplyConfig() override;
    bool motorGetDebugInfo(Harness::MotorDebugParams& out) override;
    bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) override;

private:
    Harness::IMotorTaskControl* m_motorCtrl = nullptr;
};

} // namespace Services
