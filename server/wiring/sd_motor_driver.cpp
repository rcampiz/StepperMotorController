/**
 * @file sd_motor_driver.cpp
 * @brief ServiceDispatcher — IMotorDriverDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "L3_services/motion/motor_config.hpp"

Comms::IMotorDriverDispatcher::FaultEnableFlags ServiceDispatcher::getFaultEnable() {
    const auto& fe = Services::g_motorConfig.getFaultEnable();
    return {fe.ocd != 0, fe.thermalSD != 0, fe.thermalWarn != 0,
            fe.uvlo != 0, fe.stallA != 0, fe.stallB != 0, fe.cmdErr != 0};
}

void ServiceDispatcher::motorReinit() {
    Tasks::MotorTask_Reinit();
}

bool ServiceDispatcher::motorApplyConfig() {
    return Tasks::MotorTask_ApplyConfig();
}

bool ServiceDispatcher::motorGetDebugInfo(MotorDebugParams& out) {
    Tasks::MotorDebugInfo info;
    if (!Tasks::MotorTask_GetDebugInfo(info)) {
        return false;
    }
    out.status    = info.status;
    out.kvalHold  = info.kvalHold;
    out.kvalRun   = info.kvalRun;
    out.kvalAcc   = info.kvalAcc;
    out.kvalDec   = info.kvalDec;
    out.accel     = info.accel;
    out.decel     = info.decel;
    out.maxSpeed  = info.maxSpeed;
    out.absPos    = info.absPos;
    out.ocdTh     = info.ocdTh;
    out.stallTh   = info.stallTh;
    out.config    = info.config;
    out.alarmEn   = info.alarmEn;
    out.fsSpd     = info.fsSpd;
    out.stepMode  = info.stepMode;
    return true;
}

bool ServiceDispatcher::motorSetStepModeSafe(uint8_t mode, uint8_t& readback) {
    Tasks::MotorTask_Suspend();
    bool ok = Tasks::MotorTask_SetStepModeSafe(mode, readback);
    Tasks::MotorTask_Resume();
    return ok;
}
