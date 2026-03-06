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
#include "harness/pins/isnapshot_writer.hpp"
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

void MotorController::applySpeedOverride(IMotorDriver& driver, uint32_t rawMaxSpeed)
{
    m_savedMaxSpeed = driver.getParam(MotorReg::MAX_SPEED);
    driver.setParam(MotorReg::MAX_SPEED, rawMaxSpeed);
    m_speedOverrideActive = true;
}

void MotorController::restoreSpeedIfOverridden(IMotorDriver& driver)
{
    if (m_speedOverrideActive) {
        driver.setParam(MotorReg::MAX_SPEED, m_savedMaxSpeed);
        m_speedOverrideActive = false;
    }
}

// ============================================================================
// Command processing
// ============================================================================

void MotorController::handleRunCommand(const MotorCommand& cmd, IMotorDriver& driver)
{
    auto mode = motion.control.mode.getMode();
    auto rawSpeed = static_cast<uint32_t>(cmd.param1);
    bool fwd = (cmd.param2 != 0);
    m_lastRunForward = fwd;
    m_continuousRunActive = true;
    driver.run(fwd, rawSpeed);

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

void MotorController::processCommand(const MotorCommand& cmd, IMotorDriver& driver)
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
                applySpeedOverride(driver, static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            driver.move(forward, steps);
            break;
        }

        case MotorCmdType::GoTo:
            if (cmd.param2 != 0) {
                applySpeedOverride(driver, static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            driver.goTo(cmd.param1);
            break;

        case MotorCmdType::Run:
            handleRunCommand(cmd, driver);
            break;

        case MotorCmdType::SoftStop:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            driver.softStop();
            break;

        case MotorCmdType::HardStop:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            driver.hardStop();
            restoreSpeedIfOverridden(driver);
            break;

        case MotorCmdType::SoftHiZ:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            driver.softHiZ();
            break;

        case MotorCmdType::HardHiZ:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            driver.hardHiZ();
            restoreSpeedIfOverridden(driver);
            break;

        case MotorCmdType::GoHome:
            if (cmd.param2 != 0) {
                applySpeedOverride(driver, static_cast<uint32_t>(cmd.param2));
            }
            m_continuousRunActive = false;
            driver.goHome();
            break;

        case MotorCmdType::GoMark:
            m_continuousRunActive = false;
            driver.goMark();
            break;

        case MotorCmdType::ResetPos:
            driver.resetPos();
            break;

        case MotorCmdType::SetAccel:
            driver.setParam(MotorReg::ACC, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetDecel:
            driver.setParam(MotorReg::DEC, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetMaxSpeed:
            driver.setParam(MotorReg::MAX_SPEED, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::SetMark:
            driver.setParam(MotorReg::MARK, static_cast<uint32_t>(cmd.param1));
            break;

        case MotorCmdType::GetStatus:
            // Handled by periodicUpdate — nothing to do here
            break;
    }
}

// ============================================================================
// Periodic control loop
// ============================================================================

void MotorController::periodicUpdate(IMotorDriver& driver)
{
    Trace::setCurrentServiceId(Trace::SVC_MOTION);

    // Read motor status and position/speed
    MotorStatus status = driver.getStatus();
    uint32_t rawPos = driver.getParam(MotorReg::ABS_POS);
    driver.getParam(MotorReg::SPEED);  // tap writes to snapshot

    int32_t position = signExtendAbsPos(rawPos);

    // Motor telemetry is now populated by TracedMotorDriver tap
    // (writeMotorStatus + writeMotorParam on each getStatus/getParam call above)

    // Shared sensor data for control loop
    Protocol::TelemetrySnapshot snap = Harness::telemetry().getSnapshot();
    uint32_t ustepsPerRev = motion.stepper.config.getMicrostepsPerRev();
    uint16_t encPPR = motion.encoder.config.getEncoderPPR();
    uint16_t fpr = motion.stepper.config.getFullStepsPerRev();

    int32_t cmdTicks = motorPosToEncoderTicks(position, encPPR, ustepsPerRev);
    auto measTicks = static_cast<int32_t>(snap.encoder.count);
    int32_t followError = cmdTicks - measTicks;

    // ── System identification (overrides normal control when active) ──
    Trace::setCurrentServiceId(Trace::SVC_SYSID);
    bool sysidActive = false;
    {
        auto& sysid = motion.control.sysId;
        auto sysResult = sysid.tick(
            snap.encoder.velocity, followError, fpr, encPPR);

        if (sysResult.active) {
            sysidActive = true;
            if (sysResult.rawSpeed > 0) {
                driver.run(sysResult.forward, sysResult.rawSpeed);
            } else {
                driver.softStop();
            }
            if (motion.control.supervisor.getState() != SupervisorState::IDLE) {
                motion.control.supervisor.goIdle();
            }
        }
        if (sysResult.justFinished) {
            driver.softStop();
        }
    }

    // ── Speed-trim controller (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_MOTION);
    if (!sysidActive && motion.control.trim.isActive()) {
        SpeedTrimInput ti{};
        ti.encVelTps       = snap.encoder.velocity;
        ti.velQuality      = static_cast<VelocityQuality>(
                                 snap.encoder.velocityQuality);
        ti.forward         = m_lastRunForward;
        ti.fullStepsPerRev = fpr;
        ti.encoderPPR      = encPPR;
        ti.maxSpeedRaw     = driver.getParam(MotorReg::MAX_SPEED);
        ti.dtSec           = 0.050f;

        SpeedTrimResult trimResult = motion.control.trim.update(ti);
        m_lastTrimResult = trimResult;
        if (trimResult.active) {
            driver.run(m_lastRunForward, trimResult.finalSpeedRaw);
        }
    }

    // ── Following Error Supervisor (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_SAFETY);
    if (!sysidActive) {
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
        si.maxSpeedRaw     = driver.getParam(MotorReg::MAX_SPEED);
        si.fullStepsPerRev = fpr;
        si.encoderPPR      = encPPR;
        si.dtSec           = 0.050f;
        si.revolutions     = snap.encoder.revolutions;

        SupervisorAction act = sup.evaluate(si, mode);

        // Apply action to driver
        switch (act.type) {
            case SupervisorAction::Type::SET_SPEED:
                driver.run(act.direction, act.rawSpeed);
                break;
            case SupervisorAction::Type::SOFT_STOP:
                motion.control.trim.reset();
                driver.softStop();
                break;
            case SupervisorAction::Type::HARD_STOP:
                motion.control.trim.reset();
                driver.hardStop();
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

        Harness::snapshotWriter().writeControl(ctrl);
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
        restoreSpeedIfOverridden(driver);
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

void MotorController::applyConfig(IMotorDriver& driver)
{
    const auto& cfg = motion.stepper.config.getConfig();

    // KVAL values
    driver.setParam(MotorReg::KVAL_HOLD, cfg.kvalHold);
    driver.setParam(MotorReg::KVAL_RUN, cfg.kvalRun);
    driver.setParam(MotorReg::KVAL_ACC, cfg.kvalAcc);
    driver.setParam(MotorReg::KVAL_DEC, cfg.kvalDec);

    // Protection thresholds
    driver.setParam(MotorReg::OCD_TH, cfg.ocdThreshold);
    driver.setParam(MotorReg::STALL_TH, cfg.stallThreshold);

    // Motion parameters
    driver.setParam(MotorReg::ACC, cfg.acceleration);
    driver.setParam(MotorReg::DEC, cfg.deceleration);
    driver.setParam(MotorReg::MAX_SPEED, cfg.maxSpeed);
    driver.setParam(MotorReg::MIN_SPEED, cfg.minSpeed);
    driver.setParam(MotorReg::FS_SPD, cfg.fsSpeed);

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
    driver.setParam(MotorReg::ALARM_EN, alarmEn);

    // Microstep mode (read-modify-write to preserve CM_VM and SYNC bits)
    uint32_t stepMode = driver.getParam(MotorReg::STEP_MODE);
    stepMode = (stepMode & 0xF8) | (cfg.stepMode & 0x07);
    driver.setParam(MotorReg::STEP_MODE, stepMode);

    // Clear any latched fault bits from the register write sequence
    driver.getStatus();
}

bool MotorController::setStepModeSafe(IMotorDriver& driver, uint8_t mode,
                                       uint8_t& readback)
{
    // Caller has already done: driver.hardHiZ() + 5ms delay

    // Read-modify-write STEP_MODE (preserve CM_VM and SYNC_SEL bits)
    uint32_t reg = driver.getParam(MotorReg::STEP_MODE);
    reg = (reg & 0xF8) | (mode & 0x07);
    driver.setParam(MotorReg::STEP_MODE, reg);

    // Read back and verify
    uint32_t verify = driver.getParam(MotorReg::STEP_MODE);
    readback = static_cast<uint8_t>(verify & 0x07);

    // Clear any latched fault bits
    driver.getStatus();

    // Re-enable motor (SoftStop holds position with configured KVAL_HOLD)
    driver.softStop();

    return (readback == mode);
}

void MotorController::reinit(IMotorDriver& driver)
{
    driver.init();
    applyConfig(driver);
}

// ============================================================================
// Debug
// ============================================================================

void MotorController::getDebugInfo(IMotorDriver& driver, MotorDebugParams& info)
{
    info.status = driver.getStatus().raw;
    info.kvalHold = static_cast<uint8_t>(driver.getParam(MotorReg::KVAL_HOLD));
    info.kvalRun = static_cast<uint8_t>(driver.getParam(MotorReg::KVAL_RUN));
    info.kvalAcc = static_cast<uint8_t>(driver.getParam(MotorReg::KVAL_ACC));
    info.kvalDec = static_cast<uint8_t>(driver.getParam(MotorReg::KVAL_DEC));
    info.accel = static_cast<uint16_t>(driver.getParam(MotorReg::ACC));
    info.decel = static_cast<uint16_t>(driver.getParam(MotorReg::DEC));
    info.maxSpeed = static_cast<uint16_t>(driver.getParam(MotorReg::MAX_SPEED));

    info.absPos = signExtendAbsPos(driver.getParam(MotorReg::ABS_POS));

    info.ocdTh = static_cast<uint8_t>(driver.getParam(MotorReg::OCD_TH));
    info.stallTh = static_cast<uint8_t>(driver.getParam(MotorReg::STALL_TH));
    info.config = static_cast<uint16_t>(driver.getParam(MotorReg::CONFIG));
    info.alarmEn = static_cast<uint8_t>(driver.getParam(MotorReg::ALARM_EN));
    info.fsSpd = static_cast<uint16_t>(driver.getParam(MotorReg::FS_SPD));
    info.stepMode = static_cast<uint8_t>(driver.getParam(MotorReg::STEP_MODE)) & 0x07;
}

} // namespace Services
