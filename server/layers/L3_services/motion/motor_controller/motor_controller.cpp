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
#include "L3_services/boundary/iencoder.hpp"
#include <limits.h>

namespace Services {

MotorController g_motorController;

// ============================================================================
// Unit conversions (static, pure math; physical units, the driver owns the chip's)
// ============================================================================

int32_t MotorController::motorPosToEncoderTicks(int32_t position, uint16_t encPPR,
                                                 uint32_t ustepsPerRev)
{
    return static_cast<int32_t>(
        (static_cast<int64_t>(position) * encPPR) / ustepsPerRev);
}

int32_t MotorController::speedToSetpointTps(uint32_t stepsPerSec, bool forward,
                                             uint16_t encPPR, uint16_t fpr)
{
    if (fpr == 0) return 0;
    auto tps = static_cast<int32_t>(
        (static_cast<uint64_t>(stepsPerSec) * encPPR) / fpr);
    return forward ? tps : -tps;
}

// ============================================================================
// Speed override
// ============================================================================

void MotorController::applySpeedOverride(float maxFullStepsPerSec)
{
    // A per-move speed: the driver's speed ceiling is lowered for this move only. Save the
    // configured value once; a second override while one is active must not save the
    // overridden value. getMaxSpeed() -> setMaxSpeed() round-trips exactly (driver contract).
    if (!m_speedOverrideActive) {
        m_savedMaxSpeedFsps = m_driver->getMaxSpeed();
        m_speedOverrideActive = true;
    }
    m_driver->setMaxSpeed(maxFullStepsPerSec);
}

void MotorController::restoreSpeedIfOverridden()
{
    if (m_speedOverrideActive) {
        m_driver->setMaxSpeed(m_savedMaxSpeedFsps);
        m_speedOverrideActive = false;
    }
}

// ============================================================================
// Command processing
// ============================================================================

void MotorController::handleRunCommand(const MotorCommand& cmd)
{
    auto mode = motion.control.mode.getMode();
    auto stepsPerSec = static_cast<uint32_t>(cmd.param1);   // whole full steps/s
    const auto speedFsps = static_cast<float>(stepsPerSec);
    bool fwd = (cmd.param2 != 0);
    m_lastRunForward = fwd;
    m_continuousRunActive = true;
    restoreSpeedIfOverridden();          // a run is limited by the configured ceiling, never by a move's
    m_driver->run(fwd, speedFsps);

    if (mode != ControlMode::OPEN_LOOP) {
        uint16_t encPPR = motion.encoder.config.getEncoderPPR();
        uint16_t fpr = motion.stepper.config.getFullStepsPerRev();
        int32_t setpointTps = speedToSetpointTps(stepsPerSec, fwd, encPPR, fpr);

        // Configure supervisor for fault detection
        SupervisorConfig scfg{};
        scfg.moveError      = motion.control.config.getFollowMoveError();
        scfg.moveTimeMs     = motion.control.config.getFollowMoveTimeMs();
        scfg.holdError      = motion.control.config.getFollowHoldError();
        scfg.holdTimeMs     = motion.control.config.getFollowHoldTimeMs();
        scfg.hardLimit      = motion.control.config.getFollowHardLimit();
        scfg.maxRetries     = motion.control.config.getFollowMaxRetries();
        scfg.maxDeltaVFsps  = 0.0f;
        scfg.recoveryTimeMs = 2000;
        motion.control.supervisor.configure(scfg);
        motion.control.supervisor.beginMove(setpointTps, fwd, speedFsps);

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
            motion.control.trim.beginTrim(setpointTps, fwd, speedFsps);
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
            if (cmd.param2 != 0) {                 // per-move speed, full steps/s
                applySpeedOverride(static_cast<float>(cmd.param2));
            }
            m_continuousRunActive = false;
            m_driver->move(forward, steps);
            break;
        }

        case MotorCmdType::GoTo:
            if (cmd.param2 != 0) {                 // per-move speed, full steps/s
                applySpeedOverride(static_cast<float>(cmd.param2));
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
            restoreSpeedIfOverridden();  // a move interrupted by STOP left MAX_SPEED at the move's speed
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
            restoreSpeedIfOverridden();
            break;

        case MotorCmdType::HardHiZ:
            m_continuousRunActive = false;
            motion.control.supervisor.goIdle();
            motion.control.trim.reset();
            m_driver->hardHiZ();
            restoreSpeedIfOverridden();
            break;

        case MotorCmdType::GoHome:
            if (cmd.param2 != 0) {                 // per-move speed, full steps/s
                applySpeedOverride(static_cast<float>(cmd.param2));
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

        case MotorCmdType::SetAccel:            // full steps/s^2 (live; DRV:CFG:APPLY restores the saved one)
            m_inForce.accelFsps2 = static_cast<float>(cmd.param1);
            m_driver->setAcceleration(m_inForce.accelFsps2);
            break;

        case MotorCmdType::SetDecel:
            m_inForce.decelFsps2 = static_cast<float>(cmd.param1);
            m_driver->setDeceleration(m_inForce.decelFsps2);
            break;

        case MotorCmdType::SetMaxSpeed:            // full steps/s, the configured ceiling (live)
            m_inForce.maxSpeedFsps = static_cast<float>(cmd.param1);
            m_driver->setMaxSpeed(m_inForce.maxSpeedFsps);
            m_speedOverrideActive = false;   // the new configured value is the one to keep
            break;

        case MotorCmdType::SetMark:
            m_driver->setMark(cmd.param1);
            break;

        case MotorCmdType::GetStatus:
            // Handled by periodicUpdate — nothing to do here
            break;

        case MotorCmdType::SetStepMode:
        case MotorCmdType::StartAt:
        case MotorCmdType::Reinit:
        case MotorCmdType::ApplyConfig:
        case MotorCmdType::GetDebugInfo:
            // Intercepted by MotorService::onCommand (readback slot / scheduled start / jobs); never reach here
            break;
    }
}

// ============================================================================
// Periodic control loop
// ============================================================================

void MotorController::periodicUpdate()
{
    Trace::setCurrentServiceId(Trace::SVC_MOTION);

    // A driver chip can reset behind the firmware's back (its supply dropped: an E-stop that
    // cuts motor power, a loose lead) and lose its configuration, init-only parts included
    // (bench 2026-10-19: MOT:EN did nothing, the chip tripped OCD). The driver knows how to
    // tell (powerSTEP01: a gate-drive register only its init writes, read twice in a row); when it says
    // so, initialise it again and re-apply the settings, with any run dropped.
    if (m_clock != nullptr) {
        const uint32_t now = m_clock->getTickMs();
        if (now - m_resetCheckMs >= RESET_CHECK_MS) {
            m_resetCheckMs = now;
            if (m_driver->checkReset()) {
                m_chipResets++;
                m_continuousRunActive = false;
                motion.control.supervisor.goIdle();
                motion.control.trim.reset();
                reinit();
            }
        }
    }

    // Read the driver's state (flags, position in microsteps, speed in full steps/s) and the
    // encoder count right after it, and publish the pair: slip and the following error compare
    // the two at one instant (the encoder task's own count is up to 10 ms old and this poll
    // ran every 50 ms: up to 45 degrees apart at 150 RPM, bench 2026-10-21). The service owns
    // the telemetry write; the harness tap (if any) is a pure observer.
    const MotorStatus status = m_driver->readState();
    const int64_t encoderAtPosition = encoderNow();
    Services::g_telemetryStore.writeMotorStatus(status, encoderAtPosition);

    const int32_t position = status.positionUsteps;
    m_lastPositionUsteps = position;

    // Shared sensor data for control loop
    Protocol::TelemetrySnapshot snap = Services::g_telemetryStore.getSnapshot();
    uint32_t ustepsPerRev = motion.stepper.config.getMicrostepsPerRev();
    uint16_t encPPR = motion.encoder.config.getEncoderPPR();
    uint16_t fpr = motion.stepper.config.getFullStepsPerRev();

    int32_t cmdTicks = motorPosToEncoderTicks(position, encPPR, ustepsPerRev);
    auto measTicks = static_cast<int32_t>(encoderAtPosition);
    int32_t followError = cmdTicks - measTicks;

    // The resonance avoider, every poll: the run current that keeps the resonance off this speed
    if (!m_motionInhibited) {
        resonanceStep(status, followError, encPPR, fpr);
    }

    // The control laws run every CONTROL_PERIOD_MS whatever the poll period: their gains, slew
    // limits (dtSec below) and the sysid clock count 50 ms ticks. The status read above, the
    // edges and the idle release below run on every poll.
    bool controlDue = true;
    if (m_clock != nullptr) {
        const uint32_t now = m_clock->getTickMs();
        controlDue = !m_controlRan || (now - m_lastControlMs >= CONTROL_PERIOD_MS);
        if (controlDue) {
            m_lastControlMs = now;
            m_controlRan = true;
        }
    }

    // ── System identification (overrides normal control when active) ──
    Trace::setCurrentServiceId(Trace::SVC_SYSID);
    bool sysidActive = false;
    if (controlDue && !m_motionInhibited) {
        auto& sysid = motion.control.sysId;
        auto sysResult = sysid.tick(
            snap.encoder.velocity, followError, fpr, encPPR);

        if (sysResult.active) {
            sysidActive = true;
            if (sysResult.speedFsps > 0.0f) {
                m_driver->run(sysResult.forward, sysResult.speedFsps);
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

    // The driver's speed ceiling, read once for the trim clamp and the supervisor
    float maxSpeedFsps = 0.0f;
    if (controlDue && !m_motionInhibited && !sysidActive) {
        maxSpeedFsps = m_driver->getMaxSpeed();
    }

    // ── Speed-trim controller (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_MOTION);
    if (controlDue && !m_motionInhibited && !sysidActive && motion.control.trim.isActive()) {
        SpeedTrimInput ti{};
        ti.encVelTps       = snap.encoder.velocity;
        ti.velQuality      = static_cast<VelocityQuality>(
                                 snap.encoder.velocityQuality);
        ti.forward         = m_lastRunForward;
        ti.fullStepsPerRev = fpr;
        ti.encoderPPR      = encPPR;
        ti.maxSpeedFsps    = maxSpeedFsps;
        ti.dtSec           = 0.050f;

        SpeedTrimResult trimResult = motion.control.trim.update(ti);
        m_lastTrimResult = trimResult;
        if (trimResult.active) {
            m_driver->run(m_lastRunForward, trimResult.finalSpeedFsps);
        }
    }

    // ── Following Error Supervisor (skipped during sysid) ──
    Trace::setCurrentServiceId(Trace::SVC_SAFETY);
    if (controlDue && !m_motionInhibited && !sysidActive) {
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
        si.baseSpeedFsps   = 0.0f;
        si.motorBusy       = status.busy();
        si.forward         = m_lastRunForward;
        si.maxSpeedFsps    = maxSpeedFsps;
        si.fullStepsPerRev = fpr;
        si.encoderPPR      = encPPR;
        si.dtSec           = 0.050f;
        si.revolutions     = snap.encoder.revolutions;

        SupervisorAction act = sup.evaluate(si, mode);

        // Apply action to driver
        switch (act.type) {
            case SupervisorAction::Type::SET_SPEED:
                m_driver->run(act.direction, act.speedFsps);
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
            motion.stepper.event.post(EventType::FOLLOW_FAULT, static_cast<uint16_t>(status.raw & 0xFFFFu));
        }
        if (act.recoveryEvent) {
            motion.stepper.event.post(EventType::FOLLOW_RECOVERY, static_cast<uint16_t>(status.raw & 0xFFFFu));
        }
        if (act.recoveredEvent) {
            motion.stepper.event.post(EventType::FOLLOW_RECOVERED, static_cast<uint16_t>(status.raw & 0xFFFFu));
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
        ctrl.baseSpeedFsps   = m_lastTrimResult.baseSpeedFsps;
        ctrl.finalSpeedFsps  = m_lastTrimResult.finalSpeedFsps;
        ctrl.trimSpeedFsps   = m_lastTrimResult.finalSpeedFsps - m_lastTrimResult.baseSpeedFsps;
        ctrl.trimFrozen      = m_lastTrimResult.frozen ? 1 : 0;
        ctrl.velQuality      = snap.encoder.velocityQuality;

        Services::g_telemetryStore.writeControl(ctrl);
    }

    // ── Edge detection for async events ──
    // Fault and stall edges on the driver's decoded fault mask (no chip bit layout here)
    const uint16_t raw16 = static_cast<uint16_t>(status.raw & 0xFFFFu);
    bool curFault  = status.shutdownFault();
    bool prevFault = (m_prevFaults & Harness::DriverFault::SHUTDOWN) != 0u;

    bool curStall  = status.stalled();
    bool prevStall = (m_prevFaults & Harness::DriverFault::STALL) != 0u;

    bool curBusy = status.busy();

    // Assertion edges
    if (!prevFault && curFault) {
        motion.stepper.event.post(EventType::FAULT, raw16, status.faults);
    }
    if (!prevStall && curStall) {
        motion.stepper.event.post(EventType::STALL, raw16, status.faults);
    }

    // Deassertion edges
    if (prevFault && !curFault) {
        motion.stepper.event.post(EventType::FAULT_CLEAR, raw16, status.faults);
    }
    if (prevStall && !curStall) {
        motion.stepper.event.post(EventType::STALL_CLEAR, raw16, status.faults);
    }

    // Motion complete: busy → idle
    if (m_prevBusy && !curBusy) {
        restoreSpeedIfOverridden();
        motion.stepper.event.post(EventType::MOTION_DONE, raw16);

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

    m_prevFaults = status.faults;

    // Idle release (config idleReleaseMs): the coils are let go after the motor has sat
    // stopped and energised for that long. The chip re-energises on the next Run/Move/GoTo
    // by itself, so nothing else changes; MOT:EN restarts the clock.
    const uint16_t releaseMs = motion.stepper.config.getIdleReleaseMs();
    const bool stoppedEnergised = !status.hiZ() && !status.busy() && !(status.speedFsps > 0.0f);
    if (releaseMs == 0 || !stoppedEnergised || m_motionInhibited) {
        m_stoppedSeen = false;
    } else if (!m_stoppedSeen) {
        m_stoppedSeen = true;
        m_stoppedSinceMs = m_clock->getTickMs();
    } else if (m_clock->getTickMs() - m_stoppedSinceMs >= releaseMs) {
        m_driver->softHiZ();
        m_stoppedSeen = false;
    }
    m_prevBusy = curBusy;
}

// ============================================================================
// Config application
// ============================================================================

bool MotorController::applyConfig()
{
    // The saved settings, physical; the driver writes its chip and reports the settings it
    // could not put in force with the outputs on (powerSTEP01: microstep mode, drive mode)
    m_inForce = motion.stepper.config.getDriverSettings();
    m_speedOverrideActive = false;       // the saved config is the value to keep from here on
    const bool ok = m_driver->apply(m_inForce) == ApplyResult::APPLIED;
    m_resonance.resync(m_inForce.currentA.run);   // the apply wrote the configured run current
    return ok;
}

bool MotorController::applyWithOutputsOff()
{
    // Release the outputs, apply, energise again (SoftStop holds the position at the hold level)
    m_driver->hardHiZ();
    m_clock->delayMs(5);
    const ApplyResult r = m_driver->apply(m_inForce);
    m_resonance.resync(m_inForce.currentA.run);
    m_driver->softStop();
    return r == ApplyResult::APPLIED;
}

void MotorController::resonanceStep(const MotorStatus& status, int32_t followError,
                                    uint16_t encPPR, uint16_t fpr)
{
    if (m_resReload) {
        m_resReload = false;
        m_resonance.configure(motion.configStore.getResonanceConfig());
    }
    const Harness::DriverCaps& caps = m_driver->caps();
    ResonanceInput in{};
    in.speedFsps = status.speedFsps;
    // Current mode only: the model was profiled on the run current
    in.runA = (m_inForce.mode == Harness::DriveMode::CURRENT) ? m_inForce.currentA.run : 0.0f;
    in.stepA = caps.currentStepA;
    in.maxA = caps.maxCurrentA;
    in.guardAvailable = (m_encoder != nullptr);
    in.followError = followError;
    in.countsPerFullStep = (fpr != 0u) ? static_cast<float>(encPPR) / static_cast<float>(fpr) : 0.0f;
    const ResonanceDecision d = m_resonance.update(in);
    if (d.write && !m_driver->setRunCurrentLive(d.runA)) {
        m_resonance.markUnsupported();
    }
}

bool MotorController::applyConfigOutputsOff()
{
    m_inForce = motion.stepper.config.getDriverSettings();
    m_speedOverrideActive = false;
    return applyWithOutputsOff();
}

bool MotorController::setMicrostepsSafe(uint16_t microsteps, uint16_t& effective)
{
    // The settings in force with a new resolution; the driver caps it for the drive mode,
    // so what it reads back is what every conversion must use
    m_inForce.microsteps = microsteps;
    const bool ok = applyWithOutputsOff();
    Harness::DriverSettings rb = {};
    effective = m_driver->readSettings(rb) ? rb.microsteps : 0;
    return ok;
}

void MotorController::reinit()
{
    m_driver->init();
    applyConfig();
}

// ============================================================================
// Debug
// ============================================================================

void MotorController::getDebugInfo(MotorDebugParams& info, Harness::DriverDiagView view)
{
    info = MotorDebugParams{};
    const MotorStatus st = m_driver->readState();          // status + position
    info.status = static_cast<uint16_t>(st.raw & 0xFFFFu);
    info.faults = st.faults;
    info.outputsOff = st.outputsOff;
    info.moving = st.moving;
    info.absPos = st.positionUsteps;
    Harness::DriverSettings rb = {};
    if (m_driver->readSettings(rb)) {
        info.microsteps = rb.microsteps;
        info.driveMode = (rb.mode == Harness::DriveMode::CURRENT) ? 1 : 0;
    }
    m_driver->diagnostics(view, info.driverText, sizeof(info.driverText));
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

void MotorController::standbyFromIsr()
{
    // One GPIO write: the IHM03A1 has only C6 (100 nF) on the line, so the pin discharges it
    // in microseconds and the powerSTEP01 drops its bridges. Nothing else is touched here.
    if (m_driver != nullptr) {
        m_driver->setStandby(true);
    }
}

void MotorController::publishStandbySnapshot()
{
    // SPI is off while STBY/RESET is low, so the status poll stops; the host must still see
    // the truth: outputs in Hi-Z, not busy, speed 0 (the idle Hi-Z STATUS word of the chip),
    // position as last read.
    Harness::MotorState st = Harness::MotorState::standby();   // outputs off, not readable, speed 0
    st.positionUsteps = m_lastPositionUsteps;
    Services::g_telemetryStore.writeMotorStatus(st, encoderNow());
}

int64_t MotorController::encoderNow()
{
    return (m_encoder != nullptr) ? m_encoder->countNow() : 0;
}

void MotorController::wakeFromStandby()
{
    // Datasheet: >= 0.5 ms logic wake-up + 1 ms charge-pump start before the first command,
    // and every register is back at its default, so this is a full re-initialisation.
    if (m_driver != nullptr) {
        m_driver->setStandby(false);
        if (m_clock != nullptr) {
            m_clock->delayMs(2);
        }
        reinit();
    }
}

} // namespace Services
