/**
 * @file motor_service.hpp
 * @brief L3 MotorService — Motor task entry points, command sink, task control, immediate stop
 *
 * Owns the Motor task's work (H_TASKS_L3): executes queued commands through MotorController,
 * polls status every 50 ms, publishes system telemetry. Also the L3-internal command sink
 * (sendCommand -> Platform queue with a bounded timeout), the motor task control port used
 * by the driver dispatcher (reinit/applyConfig/getDebugInfo/setStepModeSafe), and the
 * immediate stop used by the ESTOP path (hardHiZ straight to the driver, bypassing the queue).
 */
#pragma once

#include "L3_services/boundary/imotor_service.hpp"
#include "L3_services/boundary/imotor_command_sink.hpp"
#include "L3_services/boundary/imotor_task_control.hpp"
#include "L3_services/boundary/iimmediate_stop.hpp"
#include "L3_services/boundary/imotor_control_service.hpp"
#include "L3_services/infra/telemetry_store/telemetry_store.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/iqueue.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/ischeduler.hpp"
#include "F_platform/boundary/itask_stats.hpp"

namespace Services {

class MotorController;

class MotorService : public IMotorService,
                     public IMotorCommandSink,
                     public IMotorTaskControl,
                     public IImmediateStop {
public:
    using CmdQueue = Platform::IQueue<MotorCommand, Harness::MOTOR_CMD_QUEUE_DEPTH>;

    static constexpr uint32_t STATUS_UPDATE_MS = 50;
    static constexpr uint32_t SEND_TIMEOUT_MS  = 50;   ///< bounded: never block the Comms task forever

    void init(IMotorControlService& controller,
              MotorController& rawController,
              CmdQueue& queue,
              Platform::IClock& clock,
              Platform::IScheduler& scheduler,
              Platform::ITaskStats* taskStats,
              TelemetryStore& telemetry);

    // IMotorService (Motor task)
    void onCommand(const MotorCommand& cmd) override;
    void periodicUpdate() override;

    // IMotorCommandSink (motion/config services, CommandQueue; Comms task context)
    bool sendCommand(const MotorCommand& cmd, uint32_t timeoutMs = SEND_TIMEOUT_MS) override;

    // IMotorTaskControl (driver dispatcher; Comms task context)
    void reinit() override;
    bool applyConfig() override;
    bool getDebugInfo(Harness::MotorDebugParams& out) override;
    bool setStepModeSafe(uint8_t mode, uint8_t& readback) override;

    // IImmediateStop (ESTOP path; Comms task context)
    void immediateHardHiZ() override;

private:
    IMotorControlService* m_controller = nullptr;
    MotorController*      m_rawController = nullptr;
    CmdQueue*             m_queue = nullptr;
    Platform::IClock*     m_clock = nullptr;
    Platform::IScheduler* m_scheduler = nullptr;
    Platform::ITaskStats* m_taskStats = nullptr;
    TelemetryStore*       m_telemetry = nullptr;
};

extern MotorService g_motorService;

} // namespace Services
