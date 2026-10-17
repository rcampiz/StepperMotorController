/**
 * @file motor_controller.cpp
 * @brief Motor control logic — command processing, control loop, config application
 *
 * Extracted from motor_task.cpp. All functions are pure logic with no
 * RTOS dependencies. IMotorDriver& passed per-call for testability.
 */

#include "L3_services/motion/motor_controller/motor_controller.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "harness/pins/async_event.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include <limits.h>

namespace Services {

MotorController g_motorController;

// ============================================================================
// Unit conversions (static, pure math)
// ============================================================================

uint32_t MotorController::rawSpeedToStepsPerSec(uint32_t rawSpeed)
{
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(rawSpeed) * 15625ULL) / 1048576ULL);
}

int32_t MotorController::signExtendAbsPos(uint32_t rawPos)
{
    if ((rawPos & 0x200000) != 0) {
        return static_cast<int32_t>(rawPos | 0xFFC00000U);
    }
    return static_cast<int32_t>(rawPos);
}

int32_t MotorController::motorPosToEncoderTicks(int32_t position, uint16_t encPPR,
                                                 uint32_t ustepsPerRev)
{
    return static_cast<int32_t>(
        (static_cast<int64_t>(position) * encPPR) / ustepsPerRev);
}

int32_t MotorController::rawSpeedToSetpointTps(uint32_t rawSpeed, bool forward,
                                                uint16_t encPPR, uint16_t fpr)
{
    auto tps = static_cast<int32_t>(
        (static_cast<uint64_t>(rawSpeed) * 15625ULL * encPPR)
        / (1048576ULL * fpr));
    return forward ? tps : -tps;
}

// ============================================================================
// Speed override
// ============================================================================

void MotorController::applySpeedOverride(uint32_t rawMaxSpeed)
{
    m_savedMaxSpeed = m_driver->getParam(MotorReg::MAX_SPEED);
    m_driver->setParam(MotorReg::MAX_SPEED, rawMaxSpeed);
    m_speedOverrideActive = true;
}

void MotorController::restoreSpeedIfOverridden()
{
    if (m_speedOverrideActive) {
        m_driver->setParam(MotorReg::MAX_SPEED, m_savedMaxSpeed);
        m_speedOverrideActive = false;
    }
}

// ============================================================================
// Command processing
// ============================================================================

void MotorController::handleRunCommand(const MotorCommand& cmd)
{
    auto mode = motion.control.mode.getMode();
    auto rawSpeed = static_cast<uint32_t>(cmd.param1);
    bool fwd = (cmd.param2 != 0);
    m_lastRunForward = fwd;
    m_continuousRunActive = true;
    m_driver->run(fwd, rawSpeed);

    if (mode != ControlMode::OPEN_LOOP) {
        uint16_t encPPR = motion.encoder.config.getEncoderPPR();
        uint16_t fpr = motion.stepper.config.getFullStepsPerRev();
        int32_t setpointTps = rawSpeedToSetpointTps(rawSpeed, fwd, encPPR, fpr);

        // Configure supervisor for fault detection
        SupervisorConfig scfg{};
        scfg.moveError      = motion.control.config.getFollowMoveError();
        scfg.moveTimeMs     = motion.control.config.getFollowMoveTimeMs();
        scfg.holdError      = motion.control.config.getFollowHoldError();
        scfg.holdTimeMs     = motion.control.config.getFollowHoldTimeMs();
        scfg.hardLimit      = motion.control.config.getFollowHardLimit();
        scfg.maxRetries     = motion.control.config.getFollowMaxRetries();
        scfg.maxDeltaVRaw   = 0;
        scfg.recoveryTimeMs = 2000;
        motion.control.supervisor.configure(scfg);
        motion.control.supervisor.beginMove(setpointTps, fwd, rawSpeed);

        if (mode == ControlMode::SPEED_TRIM) {
            SpeedTrimConfig tcfg{};
            tcfg.kp = motion.control.config.getPidKp();
            tcfg.ki = motion.control.config.getPidKi();
            auto pidKd100 = static_cast<uint16_t>(
                motion.control.config.getPidKd100());
            tcfg.trimMaxPercent = (pidKd100 > 0 && pidKd100 <= 50)
                                  ? static_cast<uint8_t>(pidKd100) : 0;
            tcfg.outputLimit = static_cast<float>(
                motion.control.config.getPidOutputLimit());
            tcfg.integralLimit = static_cast<float>(
                motion.control.config.getPidIntegralLimit());
            motion.control.trim.configure(tcfg);
            motion.control.trim.beginTrim(setpointTps, fwd, rawSpeed);
        } else {
            motion.control.trim.reset();
            motion.control.supervisor.configurePID(
                motion.control.config.getPidKp(),
                motion.control.config.getPidKi(),
                motion.control.config.getPidKd(),
                static_cast<float>(motion.control.config.getPidOutputLimit()),
                static_cast<float>(motion.control.config.getPidIntegralLimit()));
        }
    } else {
        motion.control.supervisor.goIdle();
        motion.control.trim.reset();
    }
}

void MotorController::processCommand(const MotorCommand& cmd)
{
    // Set trace context based on command type
    switch (cmd.type) {
        case MotorCmdType::SetAccel:
        case MotorCmdType::SetDecel:
        case MotorCmdType::SetMaxSpeed:
            Trace::setCurrentServiceId(Trace::SVC_CONFIG);
            break;
        default:
            Trace::setCurrentServiceId(Trace::SVC_MOTION);
            break;
    }

    switch (cmd.type) {
        case MotorCmdType::Move: {
            bool forward = cmd.param1 >= 0;
            uint32_t steps;
            if (forward) {
                steps = static_cast<uint32_t>(cmd.param1);
            } else if (cmd.param1 == INT32_MIN) {
                steps = static_cast<uint32_t>(INT32_MAX) + 1U;
            } else {
                steps = static_cast<uint32_t>(-cmd.param1);
            }
            if (cmd.param2 != 0) {
                applySpeedOverride(static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            m_driver->move(forward, steps);
            break;
        }

        case MotorCmdType::GoTo:
            if (cmd.param2 != 0) {
                applySpeedOverride(static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            m_driver->goTo(cmd.param1);
            break;

        case MotorCmdType::Run:
            handleRunCommand(cmd);
            break;

        case MotorCmdType::SoftStop:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            m_driver->softStop();
            break;

        case MotorCmdType::HardStop:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            m_driver->hardStop();
            restoreSpeedIfOverridden();
            break;

        case MotorCmdType::SoftHiZ:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            m_driver->softHiZ();
            break;

        case MotorCmdType::HardHiZ:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            m_driver->hardHiZ();
            restoreSpeedIfOverridden();
            break;

        case MotorCmdType::GoHome:
            if (cmd.param2 != 0) {
                applySpeedOverride(static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            m_driver->goHome();
            break;

        case MotorCmdType::GoMark:
            m_continuousRunActive = false;
            m_driver->goMark();
            break;

        case MotorCmdType::ResetPos:
            m_driver->resetPos();
            break;

        case MotorCmdType::SetAccel:
            m_driver->setParam(MotorReg::ACC, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetDecel:
            m_driver->setParam(MotorReg::DEC, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetMaxSpeed:
            m_driver->setParam(MotorReg::MAX_SPEED, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetMark:
            m_driver->setParam(MotorReg::MARK, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::GetStatus:
            // Handled by periodicUpdate — nothing to do here
            break;

        case MotorCmdType::SetStepMode:
        case MotorCmdType::StartAt:
            // Intercepted by MotorService::onCommand (readback slot / scheduled start); never reach here
            break;
    }
}

// ============================================================================
// Periodic control loop
// ============================================================================

void MotorController::periodicUpdate()
{
    Trace::setCurrentServiceId(Trace::SVC_MOTION);

    // Read motor status and position/speed, and publish them. The service owns
    // the telemetry write; the harness tap (if any) is a pure observer.
    MotorStatus status = m_driver->getStatus();
    uint32_t rawPos = m_driver->getParam(MotorReg::ABS_POS);
    uint32_t rawSpeed = m_driver->getParam(MotorReg::SPEED);
    {
        auto& writer = Services::g_telemetryStore;
        writer.writeMotorStatus(status.raw);
        writer.writeMotorParam(MotorReg::ABS_POS, rawPos);
        writer.writeMotorParam(MotorReg::SPEED, rawSpeed);
    }

    int32_t position = signExtendAbsPos(rawPos);

    // Shared sensor data for control loop
    Protocol::TelemetrySnapshot snap = Services::g_telemetryStore.getSnapshot();
    uint32_t ustepsPerRev = motion.stepper.config.getMicrostepsPerRev();
    uint16_t encPPR = motion.encoder.config.getEncoderPPR();
    uint16_t fpr = motion.stepper.config.getFullStepsPerRev();

    int32_t cmdTicks = motorPosToEncoderTicks(position, encPPR, ustepsPerRev);
    auto measTicks = static_cast<int32_t>(snap.encoder.count);
    int32_t followError = cmdTicks - measTicks;

    // ── System identification (overrides normal control when active) ──
    Trace::setCurrentServiceId(Trace::SVC_SYSID);
    bool sysidActive = false;
    if (!m_motionInhibited) {
        auto& sysid = motion.control.sysId;
        auto sysResult = sysid.tick(
            snap.encoder.velocity, followError, fpr, encPPR);

        if (sysResult.active) {
            sysidActive = true;
            if (sysResult.rawSpeed > 0) {
                m_driver->run(sysResult.forward, sysResult.rawSpeed);
            } else {
                m_driver->softStop();
            }
            if (motion.control.supervisor.getState() != SupervisorState::IDLE) {
                motion.control.supervisor.goIdle();
            }
        }
        if (sysResult.justFinished) {
            m_driver->softStop();
        }
    }

    // ── Speed-trim controller (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_MOTION);
    if (!m_motionInhibited && !sysidActive && motion.control.trim.isActive()) {
        SpeedTrimInput ti{};
        ti.encVelTps       = snap.encoder.velocity;
        ti.velQuality      = static_cast<VelocityQuality>(
                                 snap.encoder.velocityQuality);
        ti.forward         = m_lastRunForward;
        ti.fullStepsPerRev = fpr;
        ti.encoderPPR      = encPPR;
        ti.maxSpeedRaw     = m_driver->getParam(MotorReg::MAX_SPEED);
        ti.dtSec           = 0.050f;

        SpeedTrimResult trimResult = motion.control.trim.update(ti);
        m_lastTrimResult = trimResult;
        if (trimResult.active) {
            m_driver->run(m_lastRunForward, trimResult.finalSpeedRaw);
        }
    }

    // ── Following Error Supervisor (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_SAFETY);
    if (!m_motionInhibited && !sysidActive) {
        auto mode = motion.control.mode.getMode();
        auto& sup = motion.control.supervisor;

        // Auto-deactivate supervisor if mode reverted
        if (mode == ControlMode::OPEN_LOOP &&
            sup.getState() != SupervisorState::IDLE) {
            sup.goIdle();
            motion.control.trim.reset();
        }

        // Build supervisor input
        SupervisorInput si{};
        si.cmdPosTicks     = cmdTicks;
        si.encPosTicks     = measTicks;
        si.encVelTps       = snap.encoder.velocity;
        si.baseSpeedRaw    = 0;
        si.motorBusy       = status.busy();
        si.forward         = m_lastRunForward;
        si.maxSpeedRaw     = m_driver->getParam(MotorReg::MAX_SPEED);
        si.fullStepsPerRev = fpr;
        si.encoderPPR      = encPPR;
        si.dtSec           = 0.050f;
        si.revolutions     = snap.encoder.revolutions;

        SupervisorAction act = sup.evaluate(si, mode);

        // Apply action to driver
        switch (act.type) {
            case SupervisorAction::Type::SET_SPEED:
                m_driver->run(act.direction, act.rawSpeed);
                break;
            case SupervisorAction::Type::SOFT_STOP:
                motion.control.trim.reset();
                m_driver->softStop();
                break;
            case SupervisorAction::Type::HARD_STOP:
                motion.control.trim.reset();
                m_driver->hardStop();
                break;
            default:
                break;
        }

        // Post supervisor events
        if (act.faultEvent) {
            motion.stepper.event.post(EventType::FOLLOW_FAULT, status.raw);
        }
        if (act.recoveryEvent) {
            motion.stepper.event.post(EventType::FOLLOW_RECOVERY, status.raw);
        }
        if (act.recoveredEvent) {
            motion.stepper.event.post(EventType::FOLLOW_RECOVERED, status.raw);
        }

        // Build control telemetry
        SupervisorTelemetry st = sup.getTelemetry();
        Protocol::ControlTelemetry ctrl{};
        ctrl.followingError  = st.posError;
        ctrl.setpoint        = st.setpoint;
        ctrl.mode            = static_cast<uint8_t>(mode);
        ctrl.tracking        = m_lastTrimResult.active;
        ctrl.pidOutput       = static_cast<int16_t>(m_lastTrimResult.trimTps);
        ctrl.pTerm           = static_cast<int16_t>(m_lastTrimResult.pTerm);
        ctrl.iTerm           = static_cast<int16_t>(m_lastTrimResult.iTerm);
        ctrl.dTerm           = 0;
        ctrl.supervisorState = static_cast<uint8_t>(st.state);
        ctrl.currentTier     = static_cast<uint8_t>(st.tier);
        ctrl.velError        = st.velError;
        ctrl.retryCount      = st.retryCount;
        ctrl.baseSpeedRaw    = static_cast<int32_t>(m_lastTrimResult.finalSpeedRaw)
                               - m_lastTrimResult.trimRaw;
        ctrl.trimSpeedRaw    = m_lastTrimResult.trimRaw;
        ctrl.finalSpeedRaw   = static_cast<int32_t>(m_lastTrimResult.finalSpeedRaw);
        ctrl.trimFrozen      = m_lastTrimResult.frozen ? 1 : 0;
        ctrl.velQuality      = snap.encoder.velocityQuality;

        Services::g_telemetryStore.writeControl(ctrl);
    }

    // ── Edge detection for async events ──
    bool curFault  = status.ocd() || status.uvlo() || status.thermalSD();
    bool prevFault = (m_prevStatusReg & (1 << 13)) == 0
                  || (m_prevStatusReg & (1 << 9)) == 0
                  || ((m_prevStatusReg >> 11) & 0x03) >= 2;

    bool curStall  = status.stallA() || status.stallB();
    bool prevStall = (m_prevStatusReg & (1 << 14)) == 0
                  || (m_prevStatusReg & (1 << 15)) == 0;

    bool curBusy = status.busy();

    // Assertion edges
    if (!prevFault && curFault) {
        motion.stepper.event.post(EventType::FAULT, status.raw);
    }
    if (!prevStall && curStall) {
        motion.stepper.event.post(EventType::STALL, status.raw);
    }

    // Deassertion edges
    if (prevFault && !curFault) {
        motion.stepper.event.post(EventType::FAULT_CLEAR, status.raw);
    }
    if (prevStall && !curStall) {
        motion.stepper.event.post(EventType::STALL_CLEAR, status.raw);
    }

    // Motion complete: busy → idle
    if (m_prevBusy && !curBusy) {
        restoreSpeedIfOverridden();
        motion.stepper.event.post(EventType::MOTION_DONE, status.raw);

        // Transition supervisor to HOLDING only for finite moves
        if (!m_continuousRunActive) {
            auto mode = motion.control.mode.getMode();
            if (mode != ControlMode::OPEN_LOOP) {
                auto& sup = motion.control.supervisor;
                if (sup.getState() == SupervisorState::MOVING) {
                    int32_t holdTicks = motorPosToEncoderTicks(
                        position, encPPR, ustepsPerRev);
                    sup.beginHold(holdTicks);
                }
            }
        }
    }

    m_prevStatusReg = status.raw;
    m_prevBusy = curBusy;
}

// ============================================================================
// Config application
// ============================================================================

bool MotorController::applyConfig()
{
    const auto& cfg = motion.stepper.config.getConfig();

    // KVAL values
    m_driver->setParam(MotorReg::KVAL_HOLD, cfg.kvalHold);
    m_driver->setParam(MotorReg::KVAL_RUN, cfg.kvalRun);
    m_driver->setParam(MotorReg::KVAL_ACC, cfg.kvalAcc);
    m_driver->setParam(MotorReg::KVAL_DEC, cfg.kvalDec);

    // Protection thresholds
    m_driver->setParam(MotorReg::OCD_TH, cfg.ocdThreshold);
    m_driver->setParam(MotorReg::STALL_TH, cfg.stallThreshold);

    // Motion parameters
    m_driver->setParam(MotorReg::ACC, cfg.acceleration);
    m_driver->setParam(MotorReg::DEC, cfg.deceleration);
    m_driver->setParam(MotorReg::MAX_SPEED, cfg.maxSpeed);
    m_driver->setParam(MotorReg::MIN_SPEED, cfg.minSpeed);
    m_driver->setParam(MotorReg::FS_SPD, cfg.fsSpeed);

    // Build ALARM_EN register from fault enable flags
    uint8_t alarmEn = 0x00;
    if (cfg.faultEnable.ocd)         alarmEn |= (1 << 0);
    if (cfg.faultEnable.thermalSD)   alarmEn |= (1 << 1);
    if (cfg.faultEnable.thermalWarn) alarmEn |= (1 << 2);
    if (cfg.faultEnable.uvlo)        alarmEn |= (1 << 3);
    // Bit 4 = UVLO_ADC — keep disabled (floating ADCIN causes false trips)
    if (cfg.faultEnable.stallA)      alarmEn |= (1 << 5);
    if (cfg.faultEnable.stallB)      alarmEn |= (1 << 6);
    if (cfg.faultEnable.cmdErr)      alarmEn |= (1 << 7);
    m_driver->setParam(MotorReg::ALARM_EN, alarmEn);

    // Microstep mode: STEP_MODE is writable only in Hi-Z; a write with the bridges on is
    // ignored by the chip (bench 2026-10-17: config 1/128, chip stayed 1/32). Write it only
    // in Hi-Z (boot, reinit, output disabled), read-modify-write to keep CM_VM and SYNC bits,
    // and verify. Otherwise report it so the caller uses the Motor task's Hi-Z-safe path.
    const uint32_t wanted = cfg.stepMode & 0x07U;
    uint32_t stepMode = m_driver->getParam(MotorReg::STEP_MODE);
    bool stepModeOk = (stepMode & 0x07U) == wanted;
    if (!stepModeOk && m_driver->getStatus().hiZ()) {
        m_driver->setParam(MotorReg::STEP_MODE, (stepMode & 0xF8U) | wanted);
        stepModeOk = (m_driver->getParam(MotorReg::STEP_MODE) & 0x07U) == wanted;
    }

    // Clear any latched fault bits from the register write sequence
    m_driver->getStatus();
    return stepModeOk;
}

bool MotorController::setStepModeSafe(uint8_t mode,
                                       uint8_t& readback)
{
    // Disable outputs before touching STEP_MODE (the Motor task used to do this)
    m_driver->hardHiZ();
    m_clock->delayMs(5);

    // Read-modify-write STEP_MODE (preserve CM_VM and SYNC_SEL bits)
    uint32_t reg = m_driver->getParam(MotorReg::STEP_MODE);
    reg = (reg & 0xF8) | (mode & 0x07);
    m_driver->setParam(MotorReg::STEP_MODE, reg);

    // Read back and verify
    uint32_t verify = m_driver->getParam(MotorReg::STEP_MODE);
    readback = static_cast<uint8_t>(verify & 0x07);

    // Clear any latched fault bits
    m_driver->getStatus();

    // Re-enable motor (SoftStop holds position with configured KVAL_HOLD)
    m_driver->softStop();

    return (readback == mode);
}

void MotorController::reinit()
{
    m_driver->init();
    applyConfig();
}

// ============================================================================
// Debug
// ============================================================================

void MotorController::getDebugInfo(MotorDebugParams& info)
{
    info.status = m_driver->getStatus().raw;
    info.kvalHold = static_cast<uint8_t>(m_driver->getParam(MotorReg::KVAL_HOLD));
    info.kvalRun = static_cast<uint8_t>(m_driver->getParam(MotorReg::KVAL_RUN));
    info.kvalAcc = static_cast<uint8_t>(m_driver->getParam(MotorReg::KVAL_ACC));
    info.kvalDec = static_cast<uint8_t>(m_driver->getParam(MotorReg::KVAL_DEC));
    info.accel = static_cast<uint16_t>(m_driver->getParam(MotorReg::ACC));
    info.decel = static_cast<uint16_t>(m_driver->getParam(MotorReg::DEC));
    info.maxSpeed = static_cast<uint16_t>(m_driver->getParam(MotorReg::MAX_SPEED));

    info.absPos = signExtendAbsPos(m_driver->getParam(MotorReg::ABS_POS));

    info.ocdTh = static_cast<uint8_t>(m_driver->getParam(MotorReg::OCD_TH));
    info.stallTh = static_cast<uint8_t>(m_driver->getParam(MotorReg::STALL_TH));
    info.config = static_cast<uint16_t>(m_driver->getParam(MotorReg::CONFIG));
    info.alarmEn = static_cast<uint8_t>(m_driver->getParam(MotorReg::ALARM_EN));
    info.fsSpd = static_cast<uint16_t>(m_driver->getParam(MotorReg::FS_SPD));
    info.stepMode = static_cast<uint8_t>(m_driver->getParam(MotorReg::STEP_MODE)) & 0x07;
}

void MotorController::bind(IMotorDriver& driver, Platform::IClock& clock)
{
    m_driver = &driver;
    m_clock = &clock;
}

void MotorController::emergencyHiZ()
{
    if (m_driver != nullptr) {
        m_driver->hardHiZ();
    }
}

void MotorController::onEmergencyStop()
{
    // Motor task context. Every path that can re-issue run() from periodicUpdate goes idle.
    motion.control.sysId.abort();
    motion.control.trim.reset();
    motion.control.supervisor.goIdle();
    m_continuousRunActive = false;
    m_motionInhibited = true;
    if (m_driver != nullptr) {
        m_driver->hardHiZ();   // re-assert: a command dequeued just before the latch may have raced the stop
    }
}

void MotorController::clearEmergencyStop()
{
    m_motionInhibited = false;
}

} // namespace Services
