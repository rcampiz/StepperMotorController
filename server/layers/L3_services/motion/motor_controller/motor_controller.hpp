/**
 * @file motor_controller.hpp
 * @brief Motor control logic -- command processing, control loop, config application
 *
 * Pure logic, no RTOS or hardware dependencies. Called by motor_task
 * which handles queue scheduling and RTOS primitives.
 *
 * IMotorDriver& is passed to methods that need hardware access (NOT stored
 * as member), keeping the service testable without real hardware.
 */

#pragma once

#include "harness/pins/imotor_control_service.hpp"
#include "harness/pins/itelemetry.hpp"
#include "L3_services/motion/speed_trim_controller/speed_trim_controller.hpp"
#include <stdint.h>

namespace Services {

// Harness pin types used extensively in this module
using Harness::IMotorDriver;
using Harness::MotorStatus;
using Harness::IMotorCommandSink;
using Harness::MotorCmdType;
using Harness::MotorCommand;
using Harness::MotorDebugParams;
namespace MotorReg = Harness::MotorReg;

class MotorController : public Harness::IMotorControlService {
public:
    // ── Static unit conversions (pure math) ──

    /** @brief Raw SPEED register (20-bit) → steps/s */
    static uint32_t rawSpeedToStepsPerSec(uint32_t rawSpeed);

    /** @brief ABS_POS (22-bit unsigned) → signed 32-bit */
    static int32_t signExtendAbsPos(uint32_t rawPos);

    /** @brief Motor position (microsteps) → encoder ticks */
    static int32_t motorPosToEncoderTicks(int32_t position, uint16_t encPPR,
                                           uint32_t ustepsPerRev);

    /** @brief Raw speed → encoder ticks/sec setpoint (signed) */
    static int32_t rawSpeedToSetpointTps(uint32_t rawSpeed, bool forward,
                                          uint16_t encPPR, uint16_t fpr);

    // ── Command processing ──

    /**
     * @brief Process a single motor command
     *
     * Handles motion commands (RUN/STOP/MOVE/GOTO), config commands,
     * and supervisor/trim orchestration for closed-loop modes.
     */
    void processCommand(const MotorCommand& cmd, IMotorDriver& driver) override;

    // ── Periodic control loop (called every 50ms) ──

    /**
     * @brief Run one control loop iteration
     *
     * Reads motor status, runs sysid/trim/supervisor, posts events,
     * builds motor + control telemetry, detects fault/stall edges.
     */
    void periodicUpdate(IMotorDriver& driver) override;

    // ── Config application ──

    /** @brief Apply MotorConfig to driver registers */
    void applyConfig(IMotorDriver& driver) override;

    /**
     * @brief Write STEP_MODE with readback verify
     *
     * Caller must HiZ the driver + delay 5ms before calling this.
     * This method does the register write, readback, fault clear, and SoftStop.
     */
    bool setStepModeSafe(IMotorDriver& driver, uint8_t mode, uint8_t& readback) override;

    /** @brief Reinitialize driver hardware + reapply config */
    void reinit(IMotorDriver& driver) override;

    // ── Debug ──

    /** @brief Read all powerSTEP01 registers into debug struct */
    void getDebugInfo(IMotorDriver& driver, MotorDebugParams& info) override;

private:
    // Speed override state
    uint32_t m_savedMaxSpeed = 0;
    bool m_speedOverrideActive = false;

    // Direction/mode tracking
    bool m_lastRunForward = true;
    bool m_continuousRunActive = false;

    // Trim result cache for telemetry
    SpeedTrimResult m_lastTrimResult = {};

    // Edge detection state
    uint16_t m_prevStatusReg = 0xFFFF;
    bool m_prevBusy = false;

    // Internal helpers
    void applySpeedOverride(IMotorDriver& driver, uint32_t rawMaxSpeed);
    void restoreSpeedIfOverridden(IMotorDriver& driver);
    void handleRunCommand(const MotorCommand& cmd, IMotorDriver& driver);
};

extern MotorController g_motorController;

} // namespace Services
