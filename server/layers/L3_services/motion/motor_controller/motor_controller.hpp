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

#include "L3_services/boundary/imotor_control_service.hpp"
#include "L3_services/boundary/imotor_command_sink.hpp"
#include "L3_services/motion/speed_trim_controller/speed_trim_controller.hpp"
#include "F_platform/boundary/iclock.hpp"
#include <stdint.h>

namespace Services {

// Consumer port types used extensively in this module
using Services::MotorCmdType;
using Services::MotorCommand;
using Harness::MotorDebugParams;

class MotorController : public Services::IMotorControlService {
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
    /// Bind the L4 driver (from H_L3_L4) and the Platform clock. Call once before any other method.
    void bind(IMotorDriver& driver, Platform::IClock& clock);

    /// ESTOP path: force outputs to Hi-Z immediately (bypasses the command queue).
    void emergencyHiZ();

    /// Motor-task side of ESTOP: idle sysid/trim/supervisor, inhibit the control loops, re-assert Hi-Z.
    void onEmergencyStop();
    /// Motor-task side of CLEAR: the control loops may drive the motor again.
    void clearEmergencyStop();
    bool motionInhibited() const { return m_motionInhibited; }

    void processCommand(const MotorCommand& cmd) override;

    // ── Periodic control loop (called every 50ms) ──

    /**
     * @brief Run one control loop iteration
     *
     * Reads motor status, runs sysid/trim/supervisor, posts events,
     * builds motor + control telemetry, detects fault/stall edges.
     */
    void periodicUpdate() override;

    // ── Config application ──

    /**
     * @brief Apply MotorConfig to driver registers
     *
     * STEP_MODE is a "WH" register: the chip accepts the write only with the bridges in
     * Hi-Z and silently ignores it otherwise (measured on the bench 2026-10-17). It is
     * written here only in Hi-Z and read back; returns false when it could not be applied,
     * and the caller must go through setStepModeSafe() on the Motor task.
     */
    bool applyConfig() override;

    /**
     * @brief Write STEP_MODE with readback verify
     *
     * Caller must HiZ the driver + delay 5ms before calling this.
     * This method does the register write, readback, fault clear, and SoftStop.
     */
    bool setStepModeSafe(uint8_t mode, uint8_t& readback) override;

    /** @brief Reinitialize driver hardware + reapply config */
    void reinit() override;

    /** @brief ISR context: drive STBY/RESET low (bridges off in microseconds, registers reset) */
    void standbyFromIsr();

    /** @brief Motor task: release STBY/RESET, wait for the chip, re-initialise and re-apply the config */
    void wakeFromStandby();

    /** @brief Motor task: publish "Hi-Z, stopped" while the chip is in standby and cannot be read */
    void publishStandbySnapshot();

    // ── Debug ──

    /** @brief Read all powerSTEP01 registers into debug struct */
    void getDebugInfo(MotorDebugParams& info) override;

private:
    // Speed override state
    uint32_t m_savedMaxSpeed = 0;
    bool m_speedOverrideActive = false;
    uint32_t m_stoppedSinceMs = 0;     ///< tick when the motor was last seen stopped and energised (0: not)
    bool     m_stoppedSeen = false;

    // Direction/mode tracking
    bool m_lastRunForward = true;
    bool m_continuousRunActive = false;

    // Trim result cache for telemetry
    SpeedTrimResult m_lastTrimResult = {};

    // Edge detection state
    uint16_t m_prevStatusReg = 0xFFFF;
    IMotorDriver* m_driver = nullptr;      ///< owned reference, bound at init (H_L3_L4)
    Platform::IClock* m_clock = nullptr;   ///< Platform power net
    bool m_prevBusy = false;
    bool m_motionInhibited = false;        ///< between onEmergencyStop() and clearEmergencyStop() (Motor task only)

    // Internal helpers
    void applySpeedOverride(uint32_t rawMaxSpeed);
    void restoreSpeedIfOverridden();
    void handleRunCommand(const MotorCommand& cmd);
};

extern MotorController g_motorController;

} // namespace Services
