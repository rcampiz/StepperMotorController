/**
 * @file sd_supervisor_trim.cpp
 * @brief ServiceDispatcher — ISupervisorTrimDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "L3_services/motion/following_supervisor.hpp"
#include "L3_services/motion/speed_trim_controller.hpp"
#include "L3_services/motion/sysid.hpp"

Comms::ISupervisorTrimDispatcher::FollowThresholds ServiceDispatcher::getFollowThresholds() {
    return {
        Services::g_motorConfig.getFollowMoveError(),
        Services::g_motorConfig.getFollowMoveTimeMs(),
        Services::g_motorConfig.getFollowHoldError(),
        Services::g_motorConfig.getFollowHoldTimeMs(),
        Services::g_motorConfig.getFollowHardLimit(),
        Services::g_motorConfig.getFollowMaxRetries()
    };
}

void ServiceDispatcher::setFollowThresholds(const FollowThresholds& t) {
    Services::g_motorConfig.setFollowThresholds(
        t.moveErr, t.moveTime, t.holdErr, t.holdTime, t.hardLimit, t.maxRetries);
}

void ServiceDispatcher::applySupervisorConfig() {
    Services::SupervisorConfig scfg{};
    scfg.moveError = Services::g_motorConfig.getFollowMoveError();
    scfg.moveTimeMs = Services::g_motorConfig.getFollowMoveTimeMs();
    scfg.holdError = Services::g_motorConfig.getFollowHoldError();
    scfg.holdTimeMs = Services::g_motorConfig.getFollowHoldTimeMs();
    scfg.hardLimit = Services::g_motorConfig.getFollowHardLimit();
    scfg.maxRetries = Services::g_motorConfig.getFollowMaxRetries();
    Services::g_supervisor.configure(scfg);
}

bool ServiceDispatcher::supervisorClearFault() {
    return Services::g_supervisor.clearFault();
}

Comms::ISupervisorTrimDispatcher::SupervisorTelemetry ServiceDispatcher::getSupervisorTelemetry() {
    Services::SupervisorTelemetry st = Services::g_supervisor.getTelemetry();
    return {static_cast<uint8_t>(st.state), static_cast<uint8_t>(st.tier),
            st.posError, st.velError, st.setpoint, st.pidOutput,
            st.retryCount, st.errorDurationMs};
}

Comms::ISupervisorTrimDispatcher::TrimConfig ServiceDispatcher::getTrimConfig() {
    return {
        Services::g_motorConfig.getPidKp100(),
        Services::g_motorConfig.getPidKi100(),
        Services::g_motorConfig.getPidKd100(),
        Services::g_motorConfig.getPidOutputLimit(),
        Services::g_motorConfig.getPidIntegralLimit()
    };
}

void ServiceDispatcher::setTrimGains(int16_t kp100, int16_t ki100) {
    int16_t kd100 = Services::g_motorConfig.getPidKd100();
    Services::g_motorConfig.setPidGains(kp100, ki100, kd100);
}

void ServiceDispatcher::setTrimLimits(uint16_t outLim, uint16_t iLim) {
    Services::g_motorConfig.setPidLimits(outLim, iLim);
}

void ServiceDispatcher::setTrimMaxPct(int16_t val) {
    int16_t kp100 = Services::g_motorConfig.getPidKp100();
    int16_t ki100 = Services::g_motorConfig.getPidKi100();
    Services::g_motorConfig.setPidGains(kp100, ki100, val);
}

void ServiceDispatcher::resetTrim() {
    Services::g_speedTrim.reset();
}

void ServiceDispatcher::applyTrimConfig() {
    Services::SpeedTrimConfig tcfg{};
    tcfg.kp = Services::g_motorConfig.getPidKp();
    tcfg.ki = Services::g_motorConfig.getPidKi();
    int16_t kd100 = Services::g_motorConfig.getPidKd100();
    tcfg.trimMaxPercent = static_cast<float>(kd100) / 100.0f;
    tcfg.outputLimit = static_cast<float>(Services::g_motorConfig.getPidOutputLimit());
    tcfg.integralLimit = static_cast<float>(Services::g_motorConfig.getPidIntegralLimit());
    Services::g_speedTrim.configure(tcfg);
}

bool ServiceDispatcher::sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                                    bool forward, uint16_t durationMs, uint16_t settleMs,
                                    uint16_t freqMHz) {
    Services::SysIdConfig cfg{};
    cfg.type = static_cast<Services::SysIdTestType>(type);
    cfg.targetSpeed_sps = targetSpeed;
    cfg.startSpeed_sps = startSpeed;
    cfg.forward = forward;
    cfg.activeDuration_ms = durationMs;
    cfg.settleDuration_ms = settleMs;
    cfg.frequency_mHz = freqMHz;
    return Services::g_sysId.start(cfg);
}

void ServiceDispatcher::sysIdAbort() {
    Services::g_sysId.abort();
}

uint8_t ServiceDispatcher::sysIdGetPhase() {
    return static_cast<uint8_t>(Services::g_sysId.getPhase());
}

uint16_t ServiceDispatcher::sysIdGetSampleCount() {
    return Services::g_sysId.getSampleCount();
}

const Comms::SysIdSampleData* ServiceDispatcher::sysIdGetSamples(uint16_t& count) {
    count = Services::g_sysId.getSampleCount();
    return reinterpret_cast<const Comms::SysIdSampleData*>(
        Services::g_sysId.getSamples());
}
