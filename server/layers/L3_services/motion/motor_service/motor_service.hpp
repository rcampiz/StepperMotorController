/**
 * @file motor_service.hpp
 * @brief L3 MotorService — Motor task entry points, command sink, task control, immediate stop
 *
 * Owns the Motor task's work (H_TASKS_L3): executes queued commands through MotorController,
 * polls status every 50 ms, publishes system telemetry. Also the L3-internal command sink
 * (sendCommand -> Platform queue with a bounded timeout), the motor task control port used
 * by the driver dispatcher and debug service (reinit/applyConfig/getDebugInfo/setStepModeSafe/
 * setDebugPaused), and the immediate stop used by the ESTOP path.
 *
 * Cross-task state is atomic and word-sized: the ESTOP latch, the debug-pause flag and the
 * step-mode result slot. The Comms task never suspends the Motor task.
 */
#pragma once

#include "L3_services/boundary/imotor_service.hpp"
#include "L3_services/boundary/imotor_command_sink.hpp"
#include "L3_services/boundary/imotor_task_control.hpp"
#include "L3_services/boundary/iimmediate_stop.hpp"
#include "L3_services/boundary/imotor_control_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/itask_stats.hpp"
#include <atomic>

namespace Services {

class MotorController;

class MotorService : public IMotorService,
                     public IMotorCommandSink,
                     public IMotorTaskControl,
                     public IImmediateStop {
public:
    using CmdQueue = Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>;

    static constexpr uint32_t STATUS_UPDATE_MS   = 50;
    static constexpr uint32_t SEND_TIMEOUT_MS    = 50;   ///< bounded: never block the Comms task forever
    static constexpr uint32_t STEP_MODE_WAIT_MS  = 250;  ///< Motor task answers within one 50 ms pass
    static constexpr uint32_t STEP_MODE_POLL_MS  = 5;

    void init(IMotorControlService& controller,
              MotorController& rawController,
              CmdQueue& queue,
              Platform::IClock& clock,
              Platform::ITaskStats* taskStats,
              TelemetryStore& telemetry);

    // IMotorService (Motor task)
    void onCommand(const MotorCommand& cmd) override;
    void periodicUpdate() override;

    // IMotorCommandSink (motion/config services, CommandQueue; Comms task context)
    bool sendCommand(const MotorCommand& cmd, uint32_t timeoutMs = SEND_TIMEOUT_MS) override;

    // IMotorTaskControl (driver dispatcher, debug service; Comms task context)
    void reinit() override;
    bool applyConfig() override;
    bool getDebugInfo(Harness::MotorDebugParams& out) override;
    bool setStepModeSafe(uint8_t mode, uint8_t& readback) override;
    void setDebugPaused(bool paused) override;
    bool isDebugPaused() const override;

    // IImmediateStop (ESTOP path; Comms task context)
    void immediateHardHiZ() override;
    void clearImmediateStop() override;
    bool isStopLatched() const override;

    /// Move/GoTo/Run/GoHome/GoMark: refused while the ESTOP latch is set.
    static bool isMotionCommand(MotorCmdType t);
    /// Motion commands dropped while latched or paused (Motor task counter; tests/diagnostics).
    uint32_t refusedWhileLatched() const { return m_refused; }
    /// Result of the last SetStepMode handled by the Motor task (tests/diagnostics).
    bool lastStepModeResult(uint8_t& readback) const;

private:
    void applyPendingTransitions();               ///< Motor task: run the pending stop/clear edge once
    void handleSetStepMode(uint8_t mode);         ///< Motor task: rewrite STEP_MODE, publish the readback

    IMotorControlService* m_controller = nullptr;
    MotorController*      m_rawController = nullptr;
    CmdQueue*             m_queue = nullptr;
    Platform::IClock*     m_clock = nullptr;
    Platform::ITaskStats* m_taskStats = nullptr;
    TelemetryStore*       m_telemetry = nullptr;

    // ESTOP latch: Comms task writes, Motor task reads (lock-free, word-sized)
    std::atomic<bool>     m_stopLatched{false};   ///< set by immediateHardHiZ, cleared by clearImmediateStop
    std::atomic<bool>     m_stopPending{false};   ///< Motor task must run MotorController::onEmergencyStop()
    std::atomic<bool>     m_clearPending{false};  ///< Motor task must run MotorController::clearEmergencyStop()
    std::atomic<bool>     m_debugPaused{false};   ///< SPI_STOP/SPI_START: Motor task leaves the driver alone
    uint32_t              m_refused = 0;          ///< commands dropped while latched/paused (Motor task only)

    // SetStepMode result slot: bit 16 = answered, bit 8 = ok, bits 0..7 = readback
    static constexpr uint32_t STEP_MODE_PENDING  = 0;
    static constexpr uint32_t STEP_MODE_ANSWERED = 1u << 16;
    static constexpr uint32_t STEP_MODE_OK       = 1u << 8;
    std::atomic<uint32_t> m_stepModeResult{STEP_MODE_PENDING};
};

extern MotorService g_motorService;

} // namespace Services
