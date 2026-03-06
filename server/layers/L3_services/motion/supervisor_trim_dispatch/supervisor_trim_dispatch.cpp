/**
 * @file supervisor_trim_dispatch.cpp
 * @brief L3 ISupervisorTrimDispatcher implementation
 */

#include "supervisor_trim_dispatch.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

Harness::ISupervisorTrimDispatcher::FollowThresholds SupervisorTrimDispatch::getFollowThresholds() {
    return {
        motion.control.config.getFollowMoveError(),
        motion.control.config.getFollowMoveTimeMs(),
        motion.control.config.getFollowHoldError(),
        motion.control.config.getFollowHoldTimeMs(),
        motion.control.config.getFollowHardLimit(),
        motion.control.config.getFollowMaxRetries()
    };
}

void SupervisorTrimDispatch::setFollowThresholds(const FollowThresholds& t) {
    motion.control.config.setFollowThresholds(
        t.moveErr, t.moveTime, t.holdErr, t.holdTime, t.hardLimit, t.maxRetries);
}

void SupervisorTrimDispatch::applySupervisorConfig() {
    SupervisorConfig scfg{};
    scfg.moveError = motion.control.config.getFollowMoveError();
    scfg.moveTimeMs = motion.control.config.getFollowMoveTimeMs();
    scfg.holdError = motion.control.config.getFollowHoldError();
    scfg.holdTimeMs = motion.control.config.getFollowHoldTimeMs();
    scfg.hardLimit = motion.control.config.getFollowHardLimit();
    scfg.maxRetries = motion.control.config.getFollowMaxRetries();
    motion.control.supervisor.configure(scfg);
}

bool SupervisorTrimDispatch::supervisorClearFault() {
    return motion.control.supervisor.clearFault();
}

Harness::ISupervisorTrimDispatcher::SupervisorTelemetry SupervisorTrimDispatch::getSupervisorTelemetry() {
    Services::SupervisorTelemetry st = motion.control.supervisor.getTelemetry();
    return {static_cast<uint8_t>(st.state), static_cast<uint8_t>(st.tier),
            st.posError, st.velError, st.setpoint, st.pidOutput,
            st.retryCount, st.errorDurationMs};
}

Harness::ISupervisorTrimDispatcher::TrimConfig SupervisorTrimDispatch::getTrimConfig() {
    return {
        motion.control.config.getPidKp100(),
        motion.control.config.getPidKi100(),
        motion.control.config.getPidKd100(),
        motion.control.config.getPidOutputLimit(),
        motion.control.config.getPidIntegralLimit()
    };
}

void SupervisorTrimDispatch::setTrimGains(int16_t kp100, int16_t ki100) {
    int16_t kd100 = motion.control.config.getPidKd100();
    motion.control.config.setPidGains(kp100, ki100, kd100);
}

void SupervisorTrimDispatch::setTrimLimits(uint16_t outLim, uint16_t iLim) {
    motion.control.config.setPidLimits(outLim, iLim);
}

void SupervisorTrimDispatch::setTrimMaxPct(int16_t val) {
    int16_t kp100 = motion.control.config.getPidKp100();
    int16_t ki100 = motion.control.config.getPidKi100();
    motion.control.config.setPidGains(kp100, ki100, val);
}

void SupervisorTrimDispatch::resetTrim() {
    motion.control.trim.reset();
}

void SupervisorTrimDispatch::applyTrimConfig() {
    SpeedTrimConfig tcfg{};
    tcfg.kp = motion.control.config.getPidKp();
    tcfg.ki = motion.control.config.getPidKi();
    int16_t kd100 = motion.control.config.getPidKd100();
    tcfg.trimMaxPercent = static_cast<float>(kd100) / 100.0f;
    tcfg.outputLimit = static_cast<float>(motion.control.config.getPidOutputLimit());
    tcfg.integralLimit = static_cast<float>(motion.control.config.getPidIntegralLimit());
    motion.control.trim.configure(tcfg);
}

bool SupervisorTrimDispatch::sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                                         bool forward, uint16_t durationMs, uint16_t settleMs,
                                         uint16_t freqMHz) {
    SysIdConfig cfg{};
    cfg.type = static_cast<SysIdTestType>(type);
    cfg.targetSpeed_sps = targetSpeed;
    cfg.startSpeed_sps = startSpeed;
    cfg.forward = forward;
    cfg.activeDuration_ms = durationMs;
    cfg.settleDuration_ms = settleMs;
    cfg.frequency_mHz = freqMHz;
    return motion.control.sysId.start(cfg);
}

void SupervisorTrimDispatch::sysIdAbort() {
    motion.control.sysId.abort();
}

uint8_t SupervisorTrimDispatch::sysIdGetPhase() {
    return static_cast<uint8_t>(motion.control.sysId.getPhase());
}

uint16_t SupervisorTrimDispatch::sysIdGetSampleCount() {
    return motion.control.sysId.getSampleCount();
}

const Harness::SysIdSampleData* SupervisorTrimDispatch::sysIdGetSamples(uint16_t& count) {
    count = motion.control.sysId.getSampleCount();
    return reinterpret_cast<const Harness::SysIdSampleData*>(
        motion.control.sysId.getSamples());
}

} // namespace Services
