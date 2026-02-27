/**
 * @file motor_task.cpp
 * @brief Motor control task implementation
 */

#include "F_platform/tasks/motor_task.hpp"
#include <stdint.h>
#include "L4_drivers/devices/powerstep01.hpp"
#include "L4_drivers/spi/spi_bus.hpp"
#include "L4_drivers/spi/spi_manager.hpp"
#include "L2_protocol/telemetry.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "L3_services/motion/following_supervisor.hpp"
#include "L3_services/motion/control_mode.hpp"
#include "L3_services/motion/sysid.hpp"
#include "L3_services/motion/speed_trim_controller.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

namespace Tasks {

// Command queue handle
QueueHandle_t g_motorCmdQueue = nullptr;

// Task handle for suspend/resume
TaskHandle_t g_motorTaskHandle = nullptr;

// Motor driver instance (created in init)
static SPIBus* s_spi = nullptr;
static PowerSTEP01* s_motor = nullptr;

// Temporary MAX_SPEED override for GOTO/MOVE/HOME with speed parameter
static uint32_t s_savedMaxSpeed = 0;
static bool s_speedOverrideActive = false;

// Cached direction for supervisor input (set on Run command)
static bool s_lastRunForward = true;

// Track continuous run (RUN command) vs finite move (MOVE/GOTO)
// For RUN, busy→idle means "reached target speed", NOT "stopped"
// Only finite moves should transition supervisor to HOLDING
static bool s_continuousRunActive = false;

// Last trim result for telemetry reporting
static Services::SpeedTrimResult s_lastTrimResult = {};

static void applySpeedOverride(uint32_t rawMaxSpeed) {
    s_savedMaxSpeed = s_motor->getParam(PowerSTEP01::Reg::MAX_SPEED);
    s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, rawMaxSpeed);
    s_speedOverrideActive = true;
}

static void restoreSpeedIfOverridden() {
    if (s_speedOverrideActive) {
        s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, s_savedMaxSpeed);
        s_speedOverrideActive = false;
    }
}

bool MotorTask_Init()
{
    // Create command queue
    g_motorCmdQueue = xQueueCreate(MOTOR_CMD_QUEUE_DEPTH, sizeof(MotorCommand));
    if (g_motorCmdQueue == nullptr) {
        return false;
    }

    // Create SPIBus wrapper around SPI1 from manager
    // Motor driver (powerSTEP01) uses SPI1 with Mode3
    ISPIBus* spi1 = g_spiManager.getSPI1();
    if (spi1 == nullptr) {
        return false;
    }
    s_spi = new SPIBus(*spi1);
    if (s_spi == nullptr) {
        return false;
    }

    // Initialize PowerSTEP01 motor driver
    s_motor = new PowerSTEP01(*s_spi);
    if (s_motor == nullptr) {
        return false;
    }
    s_motor->init();

    // Load motor configuration from flash (or apply defaults)
    Services::g_motorConfig.init();

    // Apply configuration to the driver
    MotorTask_ApplyConfig();

    return true;
}

void vMotorTask(void* pvParameters)
{
    (void)pvParameters;

    // Safety check: if motor driver not initialized, suspend task
    if (s_motor == nullptr) {
        vTaskSuspend(nullptr);
        return;
    }

    // Wait for system startup
    vTaskDelay(pdMS_TO_TICKS(100));

    // Edge detection state for async events
    uint16_t prevStatusReg = 0xFFFF;  // all-ones = no faults (active-low)
    bool prevBusy = false;

    MotorCommand cmd;
    TickType_t lastStatusUpdate = 0;
    constexpr TickType_t STATUS_UPDATE_PERIOD = pdMS_TO_TICKS(50);

    while (true) {
        // Wait for command with timeout (allows periodic status updates)
        if (xQueueReceive(g_motorCmdQueue, &cmd, STATUS_UPDATE_PERIOD) == pdTRUE) {
            // Process command
            switch (cmd.type) {
                case MotorCmdType::Move: {
                    bool forward = cmd.param1 >= 0;
                    // Safe absolute value: handle INT32_MIN edge case
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
                    s_continuousRunActive = false;  // Finite move
                    s_motor->move(forward, steps);
                    break;
                }

                case MotorCmdType::GoTo:
                    if (cmd.param2 != 0) {
                        applySpeedOverride(static_cast<uint32_t>(cmd.param2));
                    }
                    s_continuousRunActive = false;  // Finite move
                    s_motor->goTo(cmd.param1);
                    break;

                case MotorCmdType::Run: {
                    auto mode = Services::g_controlMode.getMode();
                    uint32_t rawSpeed = static_cast<uint32_t>(cmd.param1);
                    bool fwd = (cmd.param2 != 0);
                    s_lastRunForward = fwd;
                    s_continuousRunActive = true;  // RUN is continuous motion
                    s_motor->run(fwd, rawSpeed);   // Always issue initial run

                    if (mode != Services::ControlMode::OPEN_LOOP) {
                        // Convert raw speed directly to encoder ticks/sec in one
                        // 64-bit operation to avoid intermediate integer truncation.
                        // setpointTps = rawSpeed * 15625 * encPPR / (1048576 * fpr)
                        uint16_t encPPR = Services::g_motorConfig.getEncoderPPR();
                        uint16_t fpr = Services::g_motorConfig.getFullStepsPerRev();
                        int32_t setpointTps = static_cast<int32_t>(
                            (static_cast<uint64_t>(rawSpeed) * 15625ULL * encPPR)
                            / (1048576ULL * fpr));
                        if (!fwd) { setpointTps = -setpointTps; }

                        // Configure supervisor for fault detection
                        Services::SupervisorConfig scfg{};
                        scfg.moveError      = Services::g_motorConfig.getFollowMoveError();
                        scfg.moveTimeMs     = Services::g_motorConfig.getFollowMoveTimeMs();
                        scfg.holdError      = Services::g_motorConfig.getFollowHoldError();
                        scfg.holdTimeMs     = Services::g_motorConfig.getFollowHoldTimeMs();
                        scfg.hardLimit      = Services::g_motorConfig.getFollowHardLimit();
                        scfg.maxRetries     = Services::g_motorConfig.getFollowMaxRetries();
                        scfg.maxDeltaVRaw   = 0;
                        scfg.recoveryTimeMs = 2000;
                        Services::g_supervisor.configure(scfg);
                        Services::g_supervisor.beginMove(setpointTps, fwd, rawSpeed);

                        // In SPEED_TRIM mode, also start trim controller
                        if (mode == Services::ControlMode::SPEED_TRIM) {
                            Services::SpeedTrimConfig tcfg{};
                            tcfg.kp = Services::g_motorConfig.getPidKp();
                            tcfg.ki = Services::g_motorConfig.getPidKi();
                            uint16_t pidKd100 = static_cast<uint16_t>(
                                Services::g_motorConfig.getPidKd100());
                            tcfg.trimMaxPercent = (pidKd100 > 0 && pidKd100 <= 50)
                                                  ? static_cast<uint8_t>(pidKd100) : 0;
                            tcfg.outputLimit = static_cast<float>(
                                Services::g_motorConfig.getPidOutputLimit());
                            tcfg.integralLimit = static_cast<float>(
                                Services::g_motorConfig.getPidIntegralLimit());
                            Services::g_speedTrim.configure(tcfg);
                            Services::g_speedTrim.beginTrim(setpointTps, fwd, rawSpeed);
                        } else {
                            Services::g_speedTrim.reset();
                            // Load PID for supervisor (MONITOR mode uses no PID, but configure anyway)
                            Services::g_supervisor.configurePID(
                                Services::g_motorConfig.getPidKp(),
                                Services::g_motorConfig.getPidKi(),
                                Services::g_motorConfig.getPidKd(),
                                static_cast<float>(Services::g_motorConfig.getPidOutputLimit()),
                                static_cast<float>(Services::g_motorConfig.getPidIntegralLimit()));
                        }
                    } else {
                        Services::g_supervisor.goIdle();
                        Services::g_speedTrim.reset();
                    }
                    break;
                }

                case MotorCmdType::SoftStop:
                    s_continuousRunActive = false;
                    Services::g_supervisor.goIdle();
                    Services::g_speedTrim.reset();
                    s_motor->softStop();
                    break;

                case MotorCmdType::HardStop:
                    s_continuousRunActive = false;
                    Services::g_supervisor.goIdle();
                    Services::g_speedTrim.reset();
                    s_motor->hardStop();
                    restoreSpeedIfOverridden();
                    break;

                case MotorCmdType::SoftHiZ:
                    s_continuousRunActive = false;
                    Services::g_supervisor.goIdle();
                    Services::g_speedTrim.reset();
                    s_motor->softHiZ();
                    break;

                case MotorCmdType::HardHiZ:
                    s_continuousRunActive = false;
                    Services::g_supervisor.goIdle();
                    Services::g_speedTrim.reset();
                    s_motor->hardHiZ();
                    restoreSpeedIfOverridden();
                    break;

                case MotorCmdType::GoHome:
                    if (cmd.param2 != 0) {
                        applySpeedOverride(static_cast<uint32_t>(cmd.param2));
                    }
                    s_continuousRunActive = false;  // Finite move
                    s_motor->goHome();
                    break;

                case MotorCmdType::GoMark:
                    s_continuousRunActive = false;  // Finite move
                    s_motor->goMark();
                    break;

                case MotorCmdType::ResetPos:
                    s_motor->resetPos();
                    break;

                case MotorCmdType::SetAccel:
                    s_motor->setParam(PowerSTEP01::Reg::ACC, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetDecel:
                    s_motor->setParam(PowerSTEP01::Reg::DEC, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetMaxSpeed:
                    s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetMark:
                    s_motor->setParam(PowerSTEP01::Reg::MARK, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::GetStatus:
                    // Force immediate status update
                    lastStatusUpdate = 0;
                    break;
            }
        }

        // Periodic status update
        TickType_t now = xTaskGetTickCount();
        if ((now - lastStatusUpdate) >= STATUS_UPDATE_PERIOD) {
            lastStatusUpdate = now;

            // Read motor status and update telemetry
            PowerSTEP01::Status status = s_motor->getStatus();
            uint32_t rawPos = s_motor->getParam(PowerSTEP01::Reg::ABS_POS);
            uint32_t rawSpeed = s_motor->getParam(PowerSTEP01::Reg::SPEED);

            // Convert raw SPEED register (20-bit) to steps/s
            // Formula: steps_s = raw * 15625 / 1048576
            // (derived from: step/s = raw * 2^-28 / 250ns tick)
            uint32_t speed = static_cast<uint32_t>(
                (static_cast<uint64_t>(rawSpeed) * 15625ULL) / 1048576ULL);

            // Sign-extend 22-bit position to 32-bit
            int32_t position;
            if ((rawPos & 0x200000) != 0) {
                position = static_cast<int32_t>(rawPos | 0xFFC00000U);
            } else {
                position = static_cast<int32_t>(rawPos);
            }

            Comms::MotorTelemetry telem = {};
            telem.position = position;
            telem.speed = speed;
            telem.statusReg = status.raw;
            telem.busy = status.busy();
            telem.hiZ = status.hiZ();
            telem.stalled = status.stallA() || status.stallB();

            Comms::g_telemetry.updateMotor(telem);

            // Shared sensor data for sysid + supervisor
            Comms::TelemetrySnapshot snap = Comms::g_telemetry.getSnapshot();
            uint32_t ustepsPerRev = Services::g_motorConfig.getMicrostepsPerRev();
            uint16_t encPPR = Services::g_motorConfig.getEncoderPPR();
            uint16_t fpr = Services::g_motorConfig.getFullStepsPerRev();

            int32_t cmdTicks = static_cast<int32_t>(
                (static_cast<int64_t>(position) * encPPR) / ustepsPerRev);
            int32_t measTicks = static_cast<int32_t>(snap.encoder.count);
            int32_t followError = cmdTicks - measTicks;

            // System identification — overrides normal control when active
            bool sysidActive = false;
            {
                auto& sysid = Services::g_sysId;
                auto sysResult = sysid.tick(
                    snap.encoder.velocity, followError, fpr, encPPR);

                if (sysResult.active) {
                    sysidActive = true;
                    if (sysResult.rawSpeed > 0) {
                        s_motor->run(sysResult.forward, sysResult.rawSpeed);
                    } else {
                        s_motor->softStop();
                    }
                    // Force supervisor idle during sysid
                    if (Services::g_supervisor.getState() !=
                        Services::SupervisorState::IDLE) {
                        Services::g_supervisor.goIdle();
                    }
                }
                if (sysResult.justFinished) {
                    s_motor->softStop();
                }
            }

            // Speed-trim controller (skipped during sysid)
            if (!sysidActive && Services::g_speedTrim.isActive()) {
                Services::SpeedTrimInput ti{};
                ti.encVelTps       = snap.encoder.velocity;
                ti.velQuality      = static_cast<Services::VelocityQuality>(
                                         snap.encoder.velocityQuality);
                ti.forward         = s_lastRunForward;
                ti.fullStepsPerRev = fpr;
                ti.encoderPPR      = encPPR;
                ti.maxSpeedRaw     = s_motor->getParam(PowerSTEP01::Reg::MAX_SPEED);
                ti.dtSec           = 0.050f;

                Services::SpeedTrimResult trimResult = Services::g_speedTrim.update(ti);
                s_lastTrimResult = trimResult;
                if (trimResult.active) {
                    s_motor->run(s_lastRunForward, trimResult.finalSpeedRaw);
                }
            }

            // Following Error Supervisor evaluation (skipped during sysid)
            if (!sysidActive) {
                auto mode = Services::g_controlMode.getMode();
                auto& sup = Services::g_supervisor;

                // Auto-deactivate supervisor if mode reverted (encoder fault, etc.)
                if (mode == Services::ControlMode::OPEN_LOOP &&
                    sup.getState() != Services::SupervisorState::IDLE) {
                    sup.goIdle();
                    Services::g_speedTrim.reset();
                }

                // Build supervisor input
                Services::SupervisorInput si{};
                si.cmdPosTicks     = cmdTicks;
                si.encPosTicks     = measTicks;
                si.encVelTps       = snap.encoder.velocity;
                si.baseSpeedRaw    = 0;  // Stored internally by supervisor
                si.motorBusy       = status.busy();
                si.forward         = s_lastRunForward;
                si.maxSpeedRaw     = s_motor->getParam(PowerSTEP01::Reg::MAX_SPEED);
                si.fullStepsPerRev = fpr;
                si.encoderPPR      = encPPR;
                si.dtSec           = 0.050f;
                si.revolutions     = snap.encoder.revolutions;

                Services::SupervisorAction act = sup.evaluate(si, mode);

                // Apply action to driver
                switch (act.type) {
                    case Services::SupervisorAction::Type::SET_SPEED:
                        s_motor->run(act.direction, act.rawSpeed);
                        break;
                    case Services::SupervisorAction::Type::SOFT_STOP:
                        Services::g_speedTrim.reset();
                        s_motor->softStop();
                        break;
                    case Services::SupervisorAction::Type::HARD_STOP:
                        Services::g_speedTrim.reset();
                        s_motor->hardStop();
                        break;
                    default:
                        break;
                }

                // Post supervisor events
                if (act.faultEvent) {
                    Services::Event::post(EventType::FOLLOW_FAULT, status.raw);
                }
                if (act.recoveryEvent) {
                    Services::Event::post(EventType::FOLLOW_RECOVERY, status.raw);
                }
                if (act.recoveredEvent) {
                    Services::Event::post(EventType::FOLLOW_RECOVERED, status.raw);
                }

                // Populate control telemetry from supervisor + trim
                Services::SupervisorTelemetry st = sup.getTelemetry();
                Comms::ControlTelemetry ctrl{};
                ctrl.followingError  = st.posError;
                ctrl.setpoint        = st.setpoint;
                ctrl.mode            = static_cast<uint8_t>(mode);
                ctrl.tracking        = s_lastTrimResult.active;
                ctrl.pidOutput       = static_cast<int16_t>(s_lastTrimResult.trimTps);
                ctrl.pTerm           = static_cast<int16_t>(s_lastTrimResult.pTerm);
                ctrl.iTerm           = static_cast<int16_t>(s_lastTrimResult.iTerm);
                ctrl.dTerm           = 0;
                ctrl.supervisorState = static_cast<uint8_t>(st.state);
                ctrl.currentTier     = static_cast<uint8_t>(st.tier);
                ctrl.velError        = st.velError;
                ctrl.retryCount      = st.retryCount;
                // Speed-trim specific fields
                ctrl.baseSpeedRaw    = static_cast<int32_t>(s_lastTrimResult.finalSpeedRaw)
                                       - s_lastTrimResult.trimRaw;
                ctrl.trimSpeedRaw    = s_lastTrimResult.trimRaw;
                ctrl.finalSpeedRaw   = static_cast<int32_t>(s_lastTrimResult.finalSpeedRaw);
                ctrl.trimFrozen      = s_lastTrimResult.frozen ? 1 : 0;
                ctrl.velQuality      = snap.encoder.velocityQuality;

                Comms::g_telemetry.updateControl(ctrl);
            }

            // Edge detection for async events
            // Fault: OCD (bit13, active-low), UVLO (bit9, active-low), TH_SD (bits 11-12 >= 2)
            bool curFault  = status.ocd() || status.uvlo() || status.thermalSD();
            bool prevFault = (prevStatusReg & (1 << 13)) == 0
                          || (prevStatusReg & (1 << 9)) == 0
                          || ((prevStatusReg >> 11) & 0x03) >= 2;

            // Stall: STALL_A (bit14, active-low), STALL_B (bit15, active-low)
            bool curStall  = status.stallA() || status.stallB();
            bool prevStall = (prevStatusReg & (1 << 14)) == 0
                          || (prevStatusReg & (1 << 15)) == 0;

            bool curBusy = status.busy();

            // Assertion edges
            if (!prevFault && curFault) {
                Services::Event::post(EventType::FAULT, status.raw);
            }
            if (!prevStall && curStall) {
                Services::Event::post(EventType::STALL, status.raw);
            }

            // Deassertion edges (CLEAR)
            if (prevFault && !curFault) {
                Services::Event::post(EventType::FAULT_CLEAR, status.raw);
            }
            if (prevStall && !curStall) {
                Services::Event::post(EventType::STALL_CLEAR, status.raw);
            }

            // Motion complete: busy -> idle transition
            if (prevBusy && !curBusy) {
                restoreSpeedIfOverridden();
                Services::Event::post(EventType::MOTION_DONE, status.raw);

                // Transition supervisor to HOLDING only for finite moves
                // For RUN command, busy→idle means "reached target speed" — motor still spinning
                if (!s_continuousRunActive) {
                    auto mode = Services::g_controlMode.getMode();
                    if (mode != Services::ControlMode::OPEN_LOOP) {
                        auto& sup = Services::g_supervisor;
                        if (sup.getState() == Services::SupervisorState::MOVING) {
                            uint32_t ustepsPerRev = Services::g_motorConfig.getMicrostepsPerRev();
                            uint16_t encPPR = Services::g_motorConfig.getEncoderPPR();
                            int32_t cmdTicks = static_cast<int32_t>(
                                (static_cast<int64_t>(position) * encPPR) / ustepsPerRev);
                            sup.beginHold(cmdTicks);
                        }
                    }
                }
            }

            prevStatusReg = status.raw;
            prevBusy = curBusy;
        }
    }
}

bool MotorTask_SendCommand(const MotorCommand& cmd, TickType_t timeout)
{
    if (g_motorCmdQueue == nullptr) {
        return false;
    }
    return xQueueSend(g_motorCmdQueue, &cmd, timeout) == pdTRUE;
}

bool MotorTask_Move(int32_t steps)
{
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::Move;
    cmd.param1 = steps;
    return MotorTask_SendCommand(cmd);
}

bool MotorTask_Stop(bool hard)
{
    MotorCommand cmd = {};
    cmd.type = hard ? MotorCmdType::HardStop : MotorCmdType::SoftStop;
    return MotorTask_SendCommand(cmd);
}

void MotorTask_Suspend()
{
    if (g_motorTaskHandle != nullptr) {
        vTaskSuspend(g_motorTaskHandle);
    }
}

void MotorTask_Resume()
{
    if (g_motorTaskHandle != nullptr) {
        vTaskResume(g_motorTaskHandle);
    }
}

bool MotorTask_GetDebugInfo(MotorDebugInfo& info)
{
    if (s_motor == nullptr) {
        return false;
    }

    // Read all debug registers directly
    info.status = s_motor->getStatus().raw;
    info.kvalHold = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_HOLD));
    info.kvalRun = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_RUN));
    info.kvalAcc = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_ACC));
    info.kvalDec = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_DEC));
    info.accel = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::ACC));
    info.decel = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::DEC));
    info.maxSpeed = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::MAX_SPEED));

    // Sign-extend 22-bit position
    uint32_t rawPos = s_motor->getParam(PowerSTEP01::Reg::ABS_POS);
    if ((rawPos & 0x200000) != 0) {
        info.absPos = static_cast<int32_t>(rawPos | 0xFFC00000U);
    } else {
        info.absPos = static_cast<int32_t>(rawPos);
    }

    // Readback protection/config registers for verification
    info.ocdTh = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::OCD_TH));
    info.stallTh = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::STALL_TH));
    info.config = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::CONFIG));
    info.alarmEn = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::ALARM_EN));
    info.fsSpd = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::FS_SPD));
    info.stepMode = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::STEP_MODE)) & 0x07;

    return true;
}

bool MotorTask_ApplyConfig()
{
    if (s_motor == nullptr) {
        return false;
    }

    const auto& cfg = Services::g_motorConfig.getConfig();

    // Apply KVAL values
    s_motor->setParam(PowerSTEP01::Reg::KVAL_HOLD, cfg.kvalHold);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_RUN, cfg.kvalRun);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_ACC, cfg.kvalAcc);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_DEC, cfg.kvalDec);

    // Apply protection thresholds
    s_motor->setParam(PowerSTEP01::Reg::OCD_TH, cfg.ocdThreshold);
    s_motor->setParam(PowerSTEP01::Reg::STALL_TH, cfg.stallThreshold);

    // Apply motion parameters
    s_motor->setParam(PowerSTEP01::Reg::ACC, cfg.acceleration);
    s_motor->setParam(PowerSTEP01::Reg::DEC, cfg.deceleration);
    s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, cfg.maxSpeed);
    s_motor->setParam(PowerSTEP01::Reg::MIN_SPEED, cfg.minSpeed);
    s_motor->setParam(PowerSTEP01::Reg::FS_SPD, cfg.fsSpeed);

    // Build ALARM_EN register based on fault enable flags
    // ALARM_EN bits: 0=OCD, 1=TH_SD, 2=TH_WRN, 3=UVLO, 4=UVLO_ADC, 5=STALL_A, 6=STALL_B, 7=CMD_ERR
    // powerSTEP01: bit=1 enables alarm on FLAG pin, bit=0 disables
    uint8_t alarmEn = 0x00;  // Start with all disabled
    if (cfg.faultEnable.ocd) {
        alarmEn |= (1 << 0);
    }
    if (cfg.faultEnable.thermalSD) {
        alarmEn |= (1 << 1);
    }
    if (cfg.faultEnable.thermalWarn) {
        alarmEn |= (1 << 2);
    }
    if (cfg.faultEnable.uvlo) {
        alarmEn |= (1 << 3);
    }
    // Bit 4 = UVLO_ADC - keep disabled (floating ADCIN causes false trips)
    if (cfg.faultEnable.stallA) {
        alarmEn |= (1 << 5);
    }
    if (cfg.faultEnable.stallB) {
        alarmEn |= (1 << 6);
    }
    if (cfg.faultEnable.cmdErr) {
        alarmEn |= (1 << 7);
    }
    s_motor->setParam(PowerSTEP01::Reg::ALARM_EN, alarmEn);

    // Apply microstep mode (read-modify-write to preserve CM_VM and SYNC bits)
    uint32_t stepMode = s_motor->getParam(PowerSTEP01::Reg::STEP_MODE);
    stepMode = (stepMode & 0xF8) | (cfg.stepMode & 0x07);
    s_motor->setParam(PowerSTEP01::Reg::STEP_MODE, stepMode);

    // Clear any latched fault bits from the register write sequence
    s_motor->getStatus();

    return true;
}

bool MotorTask_SetStepModeSafe(uint8_t mode, uint8_t& readback)
{
    if (s_motor == nullptr || mode > 7) {
        readback = 0xFF;
        return false;
    }

    // Put motor into Hi-Z (de-energize coils — required before STEP_MODE write)
    s_motor->hardHiZ();

    // Small delay for Hi-Z to take effect
    vTaskDelay(pdMS_TO_TICKS(5));

    // Read-modify-write STEP_MODE (preserve CM_VM and SYNC_SEL bits)
    uint32_t reg = s_motor->getParam(PowerSTEP01::Reg::STEP_MODE);
    reg = (reg & 0xF8) | (mode & 0x07);
    s_motor->setParam(PowerSTEP01::Reg::STEP_MODE, reg);

    // Read back and verify
    uint32_t verify = s_motor->getParam(PowerSTEP01::Reg::STEP_MODE);
    readback = static_cast<uint8_t>(verify & 0x07);

    // Clear any latched fault bits
    s_motor->getStatus();

    // Re-enable motor (SoftStop holds position with configured KVAL_HOLD)
    s_motor->softStop();

    return (readback == mode);
}

void MotorTask_Reinit()
{
    if (s_motor != nullptr) {
        // Reinitialize the motor driver with correct settings
        // This should be called after LCD_DISABLE to ensure proper SPI config
        s_motor->init();

        // Re-apply stored configuration
        MotorTask_ApplyConfig();
    }
}

} // namespace Tasks
