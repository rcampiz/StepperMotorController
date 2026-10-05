/**
 * @file harness_tasks_l3.hpp
 * @brief H_TASKS_L3 — the ONLY connection between the task sheet (F_platform/tasks,
 *        Scheduler::) and L3 Services.
 *
 * Rule 1: tasks only wait, then call L3. Each task body calls exactly one of the
 * service entry points below. The task sheet keeps priorities, periods, stack sizes
 * and the wait itself.
 *
 * Rule 4: Platform primitives (clock, queues, locks, task handles, notifications) are a
 * power net and are NOT signals of this harness; both sides get them from
 * PlatformResources. TaskHandles below are opaque Platform handles that L3 may only use
 * with Platform::IScheduler (suspend/resume).
 *
 * Signals
 *   Upward (task calls L3, Flow::Up, Sync, Ctx::Task):
 *     IEncoderService::sample          Encoder task (prio 2, 10 ms)     — step 4b
 *     IDisplayService::tick            Display task (prio 1, 50 ms)     — step 4b
 *     IMotorService::onCommand/periodicUpdate   Motor task (prio 4, queue-driven, 50 ms status)
 *     ICommsService::onStart/tick      Comms task (prio 3, 10 ms)
 *   Downward (L3 reads the task sheet, Flow::Down, Sync):
 *     TaskHandles                      set by the composition root after task creation
 *
 * Buffer ownership: nothing crosses by pointer. ISR safety: none of these may be
 * called from ISR context.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "F_platform/boundary/ischeduler.hpp"
#include "L3_services/boundary/iencoder_service.hpp"
#include "L3_services/boundary/idisplay_service.hpp"
#include "L3_services/boundary/imotor_service.hpp"
#include "L3_services/boundary/icomms_service.hpp"

namespace Harness {

struct TaskHandles {
    Platform::TaskHandle motor   = nullptr;
    Platform::TaskHandle comms   = nullptr;
    Platform::TaskHandle encoder = nullptr;   ///< nullptr when the encoder task was not created
    Platform::TaskHandle display = nullptr;
};

/// Composition root publishes the handles once all tasks exist; L3 reads them lazily.
void setTaskHandles(const TaskHandles& handles);
const TaskHandles& taskHandles();

constexpr SignalDesc TASKS_L3_SIGNALS[] = {
    { "Services::IMotorService",   Provider::Upper, Flow::Up,   Timing::Sync, ctx(Ctx::Task), "MotorCommand by const ref for the call; L3 owns driver + controller" },
    { "Services::ICommsService",   Provider::Upper, Flow::Up,   Timing::Sync, ctx(Ctx::Task), "L3 owns parser ref, heartbeat, telemetry publish, event drain" },
    { "Services::IEncoderService", Provider::Upper, Flow::Up,   Timing::Sync, ctx(Ctx::Task), "optional task; L3 owns processor, filters, DMA read index" },
    { "Services::IDisplayService", Provider::Upper, Flow::Up,   Timing::Sync, ctx(Ctx::Task), "L3 owns ScreenManager, screens, joystick state, UI mode" },
    { "Harness::TaskHandles",      Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::PreScheduler), "opaque; used only with Platform::IScheduler" },
};
constexpr HarnessDesc TASKS_L3_DESC = { "TASKS_L3", "Scheduler (tasks)", "Services", TASKS_L3_SIGNALS, 5 };

} // namespace Harness
