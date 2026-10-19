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
    // ── Static unit conversions (pure math, physical units only) ──

    /** @brief Motor position (microsteps) → encoder ticks */
    static int32_t motorPosToEncoderTicks(int32_t position, uint16_t encPPR,
                                           uint32_t ustepsPerRev);

    /** @brief Commanded speed (whole full steps/s) → encoder ticks/sec setpoint (signed) */
    static int32_t speedToSetpointTps(uint32_t stepsPerSec, bool forward,
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
    /// Chip resets seen (supply drop) and repaired by re-initialising, since boot
    uint32_t chipResetsRepaired() const { return m_chipResets; }

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
    // Speed override state: the driver's speed ceiling in force before the override
    float m_savedMaxSpeedFsps = 0.0f;
    bool m_speedOverrideActive = false;
    int32_t m_lastPositionUsteps = 0;  ///< last position read (published while the chip is in standby)
    uint32_t m_stoppedSinceMs = 0;     ///< tick when the motor was last seen stopped and energised (0: not)
    bool     m_stoppedSeen = false;

    // Chip-reset watch: the powerSTEP01 can reset behind the firmware's back (its supply
    // dropped). GATECFG1 is written only by init; when it stops reading what init left,
    // twice in a row, the chip is initialised again.
    static constexpr uint32_t RESET_CHECK_MS = 500;
    uint16_t m_initSignature = 0;      ///< GATECFG1 as init left it (0: no check, e.g. fakes)
    uint32_t m_resetCheckMs = 0;
    uint8_t  m_resetMismatches = 0;
    uint32_t m_chipResets = 0;         ///< chip resets seen and repaired since boot

    // Direction/mode tracking
    bool m_lastRunForward = true;
    bool m_continuousRunActive = false;

    // Trim result cache for telemetry
    SpeedTrimResult m_lastTrimResult = {};

    // Edge detection state
    uint8_t m_prevFaults = 0;          ///< Harness::DriverFault bits at the last poll (edge detection)
    IMotorDriver* m_driver = nullptr;      ///< owned reference, bound at init (H_L3_L4)
    Platform::IClock* m_clock = nullptr;   ///< Platform power net
    bool m_prevBusy = false;
    bool m_motionInhibited = false;        ///< between onEmergencyStop() and clearEmergencyStop() (Motor task only)

    // Internal helpers
    void applySpeedOverride(float maxFullStepsPerSec);
    void restoreSpeedIfOverridden();
    void handleRunCommand(const MotorCommand& cmd);
};

extern MotorController g_motorController;

} // namespace Services
