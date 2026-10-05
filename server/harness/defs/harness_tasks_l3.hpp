/**
 * @file harness_tasks_l3.hpp
 * @brief H_TASKS_L3 — the ONLY connection between the task sheet (F_platform/tasks,
 *        Scheduler::) and L3 Services.
 *
 * Rule 1: tasks only wait, then call L3. Each task body calls exactly one of the
 * service entry points below. The task sheet keeps priorities, periods, stack sizes,
 * task handles and the wait itself; the composition root creates the tasks and hands
 * each one its L3 service. Nothing flows from L3 back into the task sheet.
 *
 * Rule 4: Platform primitives (clock, queues, locks, notifications) are a power net and
 * are NOT signals of this harness; both sides get them from PlatformResources.
 *
 * Signals (the task sheet is the upper side; L3 provides, the task calls down):
 *   IMotorService::onCommand/periodicUpdate   Motor task   (prio 4, queue-driven, 50 ms status)
 *   ICommsService::onStart/tick               Comms task   (prio 3, RX wake, 10 ms fallback)
 *   IEncoderService::sample                   Encoder task (prio 2, 10 ms; optional)
 *   IDisplayService::tick                     Display task (prio 1, 50 ms)
 *
 * Buffer ownership: nothing crosses by pointer. ISR safety: none of these may be
 * called from ISR context. Task suspend/resume no longer exists: step-mode changes and
 * debug pauses are messages/flags handled by the Motor and Display tasks themselves.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "L3_services/boundary/iencoder_service.hpp"
#include "L3_services/boundary/idisplay_service.hpp"
#include "L3_services/boundary/imotor_service.hpp"
#include "L3_services/boundary/icomms_service.hpp"

namespace Harness {

constexpr SignalDesc TASKS_L3_SIGNALS[] = {
    { "Services::IMotorService",   Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "MotorCommand by const ref for the call; L3 owns driver + controller; ESTOP latch + step-mode/debug-pause messages handled on the Motor task" },
    { "Services::ICommsService",   Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "L3 owns parser ref, heartbeat, telemetry publish, event drain" },
    { "Services::IEncoderService", Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "optional task; L3 owns processor, filters, DMA read index" },
    { "Services::IDisplayService", Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "L3 owns ScreenManager, screens, joystick state, UI mode; debug disable is a flag checked by tick()" },
};
constexpr HarnessDesc TASKS_L3_DESC = { "TASKS_L3", "Scheduler (tasks)", "Services", TASKS_L3_SIGNALS, 4 };

} // namespace Harness
