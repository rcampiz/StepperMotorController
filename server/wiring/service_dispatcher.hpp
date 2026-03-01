/**
 * @file service_dispatcher.hpp
 * @brief Foundation implementation of ICommandDispatcher
 *
 * Delegates ICommandDispatcher methods to L3 service APIs.
 * Lives in Foundation (can include anything).
 */

#pragma once

#include "F_platform/interfaces/icommand_dispatcher.hpp"
#include <stdio.h>
#include "L3_services/motion/motion_service.hpp"
#include "L3_services/safety/safety_service.hpp"
#include "L3_services/config/config_service.hpp"
#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "L3_services/motion/control_mode.hpp"
#include "L3_services/motion/following_supervisor.hpp"
#include "L3_services/motion/speed_trim_controller.hpp"
#include "L3_services/motion/sysid.hpp"
#include "L3_services/config/device_config.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include "L3_services/infra/timing_service.hpp"
#include "L3_services/infra/trace.hpp"
#include "L3_services/infra/flash_image_service.hpp"
#include "F_platform/interfaces/iencoder.hpp"

class ServiceDispatcher : public Comms::ICommandDispatcher {
public:
    explicit ServiceDispatcher(Services::IEncoder* encoder = nullptr)
        : m_encoder(encoder) {}

    void setEncoder(Services::IEncoder* encoder) { m_encoder = encoder; }

    // =================================================================
    // Motion
    // =================================================================

    Comms::DispatchResult motionRun(uint32_t stepsPerSec, bool forward) override {
        auto r = Services::Motion::run(stepsPerSec, forward);
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionMove(int32_t steps, uint32_t maxSpeedSps) override {
        auto r = Services::Motion::move(steps, maxSpeedSps);
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionGoTo(int32_t position, uint32_t maxSpeedSps) override {
        auto r = Services::Motion::goTo(position, maxSpeedSps);
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionStop(bool hard) override {
        auto r = Services::Motion::stop(hard);
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionEnable() override {
        auto r = Services::Motion::enable();
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionDisable() override {
        auto r = Services::Motion::disable();
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionHome(uint32_t maxSpeedSps) override {
        auto r = Services::Motion::home(maxSpeedSps);
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    Comms::DispatchResult motionZero() override {
        auto r = Services::Motion::zero();
        return {r == Services::Motion::Result::OK,
                Services::Motion::resultToString(r)};
    }

    // =================================================================
    // Config
    // =================================================================

    Comms::DispatchResult configSetAccelRaw(uint16_t val) override {
        auto r = Services::Config::setAccelRaw(val);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    Comms::DispatchResult configSetDecelRaw(uint16_t val) override {
        auto r = Services::Config::setDecelRaw(val);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    Comms::DispatchResult configSetMaxSpeedRaw(uint16_t val) override {
        auto r = Services::Config::setMaxSpeedRaw(val);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    Comms::DispatchResult configSetAccelPhysical(uint32_t stepsPerS2) override {
        auto r = Services::Config::setAccelPhysical(stepsPerS2);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    Comms::DispatchResult configSetDecelPhysical(uint32_t stepsPerS2) override {
        auto r = Services::Config::setDecelPhysical(stepsPerS2);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    Comms::DispatchResult configSetMaxSpeedPhysical(uint32_t stepsPerS) override {
        auto r = Services::Config::setMaxSpeedPhysical(stepsPerS);
        return {r == Services::Config::Result::OK,
                Services::Config::resultToString(r)};
    }

    // =================================================================
    // Motor config queries
    // =================================================================

    uint32_t getMicrostepsPerRev() override {
        return Services::g_motorConfig.getMicrostepsPerRev();
    }

    uint16_t getFullStepsPerRev() override {
        return Services::g_motorConfig.getFullStepsPerRev();
    }

    // =================================================================
    // Safety
    // =================================================================

    void safetyEstop() override {
        Services::Safety::emergencyStop();
    }

    Comms::DispatchResult safetyClearFault(char* activeFaults, uint32_t bufSize) override {
        auto r = Services::Safety::clearFault(activeFaults, bufSize);
        if (r == Services::Safety::Result::OK) return {true, nullptr};
        if (r == Services::Safety::Result::FAULT_ACTIVE) return {false, activeFaults};
        return {false, "Invalid state"};
    }

    Comms::DispatchResult safetyForceClearFault() override {
        auto r = Services::Safety::forceClearFault();
        return {r == Services::Safety::Result::OK,
                r == Services::Safety::Result::OK ? nullptr : "Invalid state"};
    }

    uint32_t safetySetHeartbeatTimeout(uint32_t ms) override {
        return Services::Safety::setHeartbeatTimeout(ms);
    }

    void safetyHeartbeatReceived(uint32_t seq) override {
        Services::Safety::heartbeatReceived(seq);
    }

    void safetyGetHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                                   uint32_t& lastSeq, uint32_t& remainingMs,
                                   bool& timedOut) override {
        Services::Safety::HeartbeatStatus hb;
        Services::Safety::getHeartbeatStatus(hb);
        enabled = hb.enabled;
        timeoutMs = hb.timeoutMs;
        lastSeq = hb.lastSeq;
        remainingMs = hb.remainingMs;
        timedOut = hb.timedOut;
    }

    // =================================================================
    // Queue / Sync
    // =================================================================

    Comms::DispatchResult queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) override {
        Services::MotorCommand cmd = {};
        cmd.type = static_cast<Services::MotorCmdType>(cmdType);
        cmd.param1 = p1;
        cmd.param2 = p2;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK,
                Services::resultToString(r)};
    }

    Comms::DispatchResult queueMove(int32_t signedSteps) override {
        Services::MotorCommand cmd = {};
        cmd.type = Services::MotorCmdType::Move;
        cmd.param1 = signedSteps;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueGoTo(int32_t position) override {
        Services::MotorCommand cmd = {};
        cmd.type = Services::MotorCmdType::GoTo;
        cmd.param1 = position;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueRun(uint32_t speedRaw, int32_t direction) override {
        Services::MotorCommand cmd = {};
        cmd.type = Services::MotorCmdType::Run;
        cmd.param1 = static_cast<int32_t>(speedRaw);
        cmd.param2 = direction;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueStop(bool hard) override {
        Services::MotorCommand cmd = {};
        cmd.type = hard ? Services::MotorCmdType::HardStop : Services::MotorCmdType::SoftStop;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueHome() override {
        Services::MotorCommand cmd = {};
        cmd.type = Services::MotorCmdType::GoHome;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueZero() override {
        Services::MotorCommand cmd = {};
        cmd.type = Services::MotorCmdType::ResetPos;
        auto r = Services::g_commandQueue.queueCommand(cmd);
        return {r == Services::QueueResult::OK, Services::resultToString(r)};
    }

    Comms::DispatchResult queueArm() override {
        auto r = Services::g_commandQueue.arm();
        return {r == Services::QueueResult::OK,
                Services::resultToString(r)};
    }

    Comms::DispatchResult queueStart() override {
        auto r = Services::g_commandQueue.start();
        return {r == Services::QueueResult::OK,
                Services::resultToString(r)};
    }

    Comms::DispatchResult queueStartAt(uint32_t targetTick) override {
        auto r = Services::g_commandQueue.startAt(targetTick);
        return {r == Services::QueueResult::OK,
                Services::resultToString(r)};
    }

    Comms::DispatchResult queueClear() override {
        auto r = Services::g_commandQueue.clearQueue();
        return {r == Services::QueueResult::OK,
                Services::resultToString(r)};
    }

    uint8_t getControllerState() override {
        return static_cast<uint8_t>(Services::g_commandQueue.getState());
    }

    uint32_t getQueueDepth() override {
        return static_cast<uint32_t>(Services::g_commandQueue.getQueueDepth());
    }

    const char* controllerStateString() override {
        return Services::stateToString(Services::g_commandQueue.getState());
    }

    // =================================================================
    // Control mode / status
    // =================================================================

    uint8_t getControlMode() override {
        return static_cast<uint8_t>(Services::g_controlMode.getMode());
    }

    const char* controlModeString() override {
        return Services::modeToString(Services::g_controlMode.getMode());
    }

    uint8_t getEncoderStatus() override {
        return static_cast<uint8_t>(Services::g_controlMode.getEncoderStatus());
    }

    const char* encoderStatusString() override {
        return Services::encoderStatusToString(Services::g_controlMode.getEncoderStatus());
    }

    void getFaultEnable(bool& ocd, bool& thermalSD, bool& thermalWarn,
                        bool& uvlo, bool& stallA, bool& stallB,
                        bool& cmdErr) override {
        const auto& fe = Services::g_motorConfig.getFaultEnable();
        ocd = fe.ocd;
        thermalSD = fe.thermalSD;
        thermalWarn = fe.thermalWarn;
        uvlo = fe.uvlo;
        stallA = fe.stallA;
        stallB = fe.stallB;
        cmdErr = fe.cmdErr;
    }

    // =================================================================
    // Device config
    // =================================================================

    void getDeviceInfo(uint16_t& deviceId, const char*& roleStr) override {
        deviceId = Services::g_deviceConfig.getDeviceId();
        roleStr = Services::roleToString(Services::g_deviceConfig.getRole());
    }

    bool setDeviceId(uint16_t id) override {
        return Services::g_deviceConfig.setDeviceId(id);
    }

    Comms::DispatchResult setRole(const char* roleName) override {
        Services::WheelRole role = Services::parseRole(roleName);
        if (Services::g_deviceConfig.setRole(role)) {
            return {true, Services::roleToString(role)};
        }
        return {false, "Flash write failed"};
    }

    // =================================================================
    // Control mode set
    // =================================================================

    Comms::DispatchResult setControlMode(const char* modeName) override {
        Services::ControlMode mode = Services::parseMode(modeName);
        if (Services::g_controlMode.setMode(mode)) {
            return {true, Services::modeToString(mode)};
        }
        // Build error message for closed-loop modes requiring encoder
        if (mode != Services::ControlMode::OPEN_LOOP) {
            static char errBuf[80];
            snprintf(errBuf, sizeof(errBuf), "Cannot enter %s: encoder %s",
                     Services::modeToString(mode),
                     Services::encoderStatusToString(
                         Services::g_controlMode.getEncoderStatus()));
            return {false, errBuf};
        }
        return {false, "Mode change failed"};
    }

    // =================================================================
    // Encoder data queries
    // =================================================================

    bool isEncoderAvailable() override {
        return m_encoder != nullptr && m_encoder->isAvailable();
    }

    void getEncoderState(int32_t& count, int32_t& velocity, bool& indexSeen) override {
        if (m_encoder == nullptr) { count = 0; velocity = 0; indexSeen = false; return; }
        Services::EncoderSnapshot st = m_encoder->getState();
        count = static_cast<int32_t>(st.count);
        velocity = st.velocity;
        indexSeen = st.indexSeen;
    }

    // =================================================================
    // Timing
    // =================================================================

    uint32_t getTickUs() override {
        return Services::TickTimer_GetTick();
    }

    void setTransportDelay(uint32_t ms) override {
        Services::Timing::setTransportDelay(ms);
    }

    uint32_t getTransportDelay() override {
        return Services::Timing::getTransportDelay();
    }

    // =================================================================
    // Encoder
    // =================================================================

    void encoderResetCount() override {
        if (m_encoder != nullptr) m_encoder->resetCount();
    }

    // =================================================================
    // Motor config — flash operations
    // =================================================================

    bool configSaveToFlash() override {
        return Services::g_motorConfig.saveToFlash();
    }

    bool configLoadFromFlash() override {
        return Services::g_motorConfig.loadFromFlash();
    }

    bool configFactoryReset() override {
        return Services::g_motorConfig.factoryReset();
    }

    bool configIsValid() override {
        return Services::g_motorConfig.isValid();
    }

    // =================================================================
    // Motor config — DRV setters
    // =================================================================

    void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) override {
        Services::g_motorConfig.setKval(hold, run, acc, dec);
    }

    void configSetOcdThreshold(uint8_t thresh) override {
        Services::g_motorConfig.setOcdThreshold(thresh);
    }

    void configSetStallThreshold(uint8_t thresh) override {
        Services::g_motorConfig.setStallThreshold(thresh);
    }

    void configSetFaultAction(uint8_t action) override {
        Services::g_motorConfig.setFaultAction(action);
    }

    void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                    bool uvlo, bool stallA, bool stallB, bool cmdErr) override {
        Services::FaultEnableFlags flags = {};
        flags.ocd = ocd;
        flags.thermalSD = thermalSD;
        flags.thermalWarn = thermalWarn;
        flags.uvlo = uvlo;
        flags.stallA = stallA;
        flags.stallB = stallB;
        flags.cmdErr = cmdErr;
        Services::g_motorConfig.setFaultEnable(flags);
    }

    void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) override {
        Services::g_motorConfig.setMotionParams(acc, dec, maxSpd);
    }

    void configSetStepMode(uint8_t mode) override {
        Services::g_motorConfig.setStepMode(mode);
    }

    void configSetFullStepsPerRev(uint16_t steps) override {
        Services::g_motorConfig.setFullStepsPerRev(steps);
    }

    void configSetEncoderPPR(uint16_t ppr) override {
        Services::g_motorConfig.setEncoderPPR(ppr);
    }

    uint16_t configGetEncoderPPR() override {
        return Services::g_motorConfig.getEncoderPPR();
    }

    void formatMotorConfig(char* buf, uint32_t bufSize) override {
        const auto& cfg = Services::g_motorConfig.getConfig();
        bool valid = Services::g_motorConfig.isValid();
        snprintf(buf, bufSize,
                 "Motor Config %s\n"
                 "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
                 "OCD_TH=%02X STALL_TH=%02X\n"
                 "ACC=%u DEC=%u MAXSPD=%u MINSPD=%u FS_SPD=%u\n"
                 "Faults: OCD=%d TH_SD=%d TH_W=%d UVLO=%d STALL_A=%d STALL_B=%d CMD=%d\n"
                 "Action: %s\n"
                 "StepMode: %u (1/%u)",
                 valid ? "(from flash)" : "(defaults)",
                 (unsigned)cfg.kvalHold, (unsigned)cfg.kvalRun,
                 (unsigned)cfg.kvalAcc, (unsigned)cfg.kvalDec,
                 (unsigned)cfg.ocdThreshold, (unsigned)cfg.stallThreshold,
                 (unsigned)cfg.acceleration, (unsigned)cfg.deceleration,
                 (unsigned)cfg.maxSpeed, (unsigned)cfg.minSpeed, (unsigned)cfg.fsSpeed,
                 cfg.faultEnable.ocd, cfg.faultEnable.thermalSD,
                 cfg.faultEnable.thermalWarn, cfg.faultEnable.uvlo,
                 cfg.faultEnable.stallA, cfg.faultEnable.stallB, cfg.faultEnable.cmdErr,
                 cfg.faultAction == 0 ? "HardStop" :
                 cfg.faultAction == 1 ? "HardHiZ" : "SoftStop",
                 (unsigned)(cfg.stepMode & 0x07), 1U << (cfg.stepMode & 0x07));
    }

    // =================================================================
    // Motor config — encoder filter
    // =================================================================

    void configSetEncFilter(uint8_t type, uint8_t alpha) override {
        Services::g_motorConfig.setEncFilter(type, alpha);
    }

    void configSetEncSmaWindow(uint8_t window) override {
        Services::g_motorConfig.getConfigMutable().encSmaWindow = window;
    }

    void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                 uint8_t smaWindow, uint8_t measWindowMs,
                                 uint8_t rateDiv) override {
        Services::g_motorConfig.setEncFilterFull(flags, emaAlpha, smaWindow,
                                                  measWindowMs, rateDiv);
    }

    // =================================================================
    // Following supervisor
    // =================================================================

    void getFollowThresholds(int16_t& moveErr, uint16_t& moveTime,
                              int16_t& holdErr, uint16_t& holdTime,
                              int16_t& hardLimit, uint8_t& maxRetries) override {
        moveErr = Services::g_motorConfig.getFollowMoveError();
        moveTime = Services::g_motorConfig.getFollowMoveTimeMs();
        holdErr = Services::g_motorConfig.getFollowHoldError();
        holdTime = Services::g_motorConfig.getFollowHoldTimeMs();
        hardLimit = Services::g_motorConfig.getFollowHardLimit();
        maxRetries = Services::g_motorConfig.getFollowMaxRetries();
    }

    void setFollowThresholds(int16_t moveErr, uint16_t moveTime,
                              int16_t holdErr, uint16_t holdTime,
                              int16_t hardLimit, uint8_t maxRetries) override {
        Services::g_motorConfig.setFollowThresholds(
            moveErr, moveTime, holdErr, holdTime, hardLimit, maxRetries);
    }

    void applySupervisorConfig() override {
        Services::SupervisorConfig scfg{};
        scfg.moveError = Services::g_motorConfig.getFollowMoveError();
        scfg.moveTimeMs = Services::g_motorConfig.getFollowMoveTimeMs();
        scfg.holdError = Services::g_motorConfig.getFollowHoldError();
        scfg.holdTimeMs = Services::g_motorConfig.getFollowHoldTimeMs();
        scfg.hardLimit = Services::g_motorConfig.getFollowHardLimit();
        scfg.maxRetries = Services::g_motorConfig.getFollowMaxRetries();
        Services::g_supervisor.configure(scfg);
    }

    bool supervisorClearFault() override {
        return Services::g_supervisor.clearFault();
    }

    void getSupervisorTelemetry(uint8_t& state, uint8_t& tier,
                                 int32_t& posError, int32_t& velError,
                                 int32_t& setpoint, int16_t& pidOutput,
                                 uint8_t& retryCount, uint32_t& errorDurationMs) override {
        Services::SupervisorTelemetry st = Services::g_supervisor.getTelemetry();
        state = static_cast<uint8_t>(st.state);
        tier = static_cast<uint8_t>(st.tier);
        posError = st.posError;
        velError = st.velError;
        setpoint = st.setpoint;
        pidOutput = st.pidOutput;
        retryCount = st.retryCount;
        errorDurationMs = st.errorDurationMs;
    }

    // =================================================================
    // Speed trim
    // =================================================================

    void getTrimGains(int16_t& kp100, int16_t& ki100, int16_t& kd100,
                       uint16_t& outLim, uint16_t& iLim) override {
        kp100 = Services::g_motorConfig.getPidKp100();
        ki100 = Services::g_motorConfig.getPidKi100();
        kd100 = Services::g_motorConfig.getPidKd100();
        outLim = Services::g_motorConfig.getPidOutputLimit();
        iLim = Services::g_motorConfig.getPidIntegralLimit();
    }

    void setTrimGains(int16_t kp100, int16_t ki100) override {
        int16_t kd100 = Services::g_motorConfig.getPidKd100();
        Services::g_motorConfig.setPidGains(kp100, ki100, kd100);
    }

    void setTrimLimits(uint16_t outLim, uint16_t iLim) override {
        Services::g_motorConfig.setPidLimits(outLim, iLim);
    }

    void setTrimMaxPct(int16_t val) override {
        int16_t kp100 = Services::g_motorConfig.getPidKp100();
        int16_t ki100 = Services::g_motorConfig.getPidKi100();
        Services::g_motorConfig.setPidGains(kp100, ki100, val);
    }

    void resetTrim() override {
        Services::g_speedTrim.reset();
    }

    void applyTrimConfig() override {
        Services::SpeedTrimConfig tcfg{};
        tcfg.kp = Services::g_motorConfig.getPidKp();
        tcfg.ki = Services::g_motorConfig.getPidKi();
        int16_t kd100 = Services::g_motorConfig.getPidKd100();
        tcfg.trimMaxPercent = static_cast<float>(kd100) / 100.0f;
        tcfg.outputLimit = static_cast<float>(Services::g_motorConfig.getPidOutputLimit());
        tcfg.integralLimit = static_cast<float>(Services::g_motorConfig.getPidIntegralLimit());
        Services::g_speedTrim.configure(tcfg);
    }

    // =================================================================
    // System ID
    // =================================================================

    bool sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                     bool forward, uint16_t durationMs, uint16_t settleMs,
                     uint16_t freqMHz) override {
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

    void sysIdAbort() override {
        Services::g_sysId.abort();
    }

    uint8_t sysIdGetPhase() override {
        return static_cast<uint8_t>(Services::g_sysId.getPhase());
    }

    uint16_t sysIdGetSampleCount() override {
        return Services::g_sysId.getSampleCount();
    }

    const Comms::SysIdSampleData* sysIdGetSamples(uint16_t& count) override {
        count = Services::g_sysId.getSampleCount();
        // Layout-compatible with Services::SysIdSample
        return reinterpret_cast<const Comms::SysIdSampleData*>(
            Services::g_sysId.getSamples());
    }

    // =================================================================
    // Events
    // =================================================================

    void enableEvents(uint8_t mask, uint16_t currentStatus) override {
        Services::Event::enable(mask, currentStatus);
    }

    void disableEvents() override {
        Services::Event::disable();
    }

    void getEventStats(uint32_t& sent, uint32_t& lostCritical,
                        uint32_t& lostInfo, uint8_t& mask,
                        uint8_t& queueDepth) override {
        Services::Event::Stats st = Services::Event::getStats();
        sent = st.sent;
        lostCritical = st.lostCritical;
        lostInfo = st.lostInfo;
        mask = st.enableMask;
        queueDepth = st.queueDepth;
    }

    // =================================================================
    // Trace
    // =================================================================

    uint32_t traceGetCount() override {
        return static_cast<uint32_t>(Trace::getCount());
    }

    bool traceGetEntry(uint32_t index, Comms::ICommandDispatcher::TraceEntryData& out) override {
        Trace::Entry e;
        if (!Trace::getEntry(static_cast<size_t>(index), e)) return false;
        out.tick = e.tick;
        out.tag = e.tag;
        out.arg0 = e.arg0;
        out.dir = static_cast<uint8_t>(e.dir);
        out.method = e.method;
        return true;
    }

    void traceReset() override {
        Trace::reset();
    }

    void traceRecordEntry(const char* tag, uint32_t arg0) override {
        Trace::record(Trace::ENTRY, tag, arg0);
    }

    void traceRecordExit(const char* tag, uint32_t arg0) override {
        Trace::record(Trace::EXIT, tag, arg0);
    }

    // =================================================================
    // Flash image
    // =================================================================

    bool flashIsAvailable() override {
        return Services::g_flashImageService.isAvailable();
    }

    Comms::ICommandDispatcher::FlashInfo flashGetInfo() override {
        auto info = Services::g_flashImageService.getInfo();
        return {info.manufacturer, info.memoryType, info.capacityCode,
                info.capacityBytes, info.maxSlots};
    }

    uint32_t flashMaxSlots() override {
        return Services::g_flashImageService.maxSlots();
    }

    bool flashEraseSlot(uint32_t slot) override {
        return Services::g_flashImageService.eraseSlot(slot);
    }

    bool flashWriteSlotData(uint32_t slot, uint32_t offset,
                            const uint8_t* data, size_t len) override {
        return Services::g_flashImageService.writeSlotData(slot, offset, data, len);
    }

    bool flashReadSlotChunk(uint32_t slot, uint32_t offset,
                            uint8_t* buf, size_t len) override {
        return Services::g_flashImageService.readSlotChunk(slot, offset, buf, len);
    }

    bool flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                  uint8_t* buf, size_t len) override {
        return Services::g_flashImageService.readSlotChunkStart(slot, offset, buf, len);
    }

    void flashReadSlotChunkFinish() override {
        Services::g_flashImageService.readSlotChunkFinish();
    }

    bool flashEraseAll() override {
        return Services::g_flashImageService.eraseAll();
    }

    uint32_t flashSlotAddress(uint32_t slot) override {
        return Services::g_flashImageService.slotAddress(slot);
    }

private:
    Services::IEncoder* m_encoder;
};
