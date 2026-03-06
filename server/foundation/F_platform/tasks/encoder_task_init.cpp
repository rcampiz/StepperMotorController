/**
 * @file encoder_task_init.cpp
 * @brief Implements Harness::initEncoderTask() — task + RTOS creation
 *
 * All dependencies arrive as harness interfaces — no L4 includes.
 */

#include "F_platform/tasks/encoder_task_init.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/pins/encoder_driver_init.hpp"
#include "F_platform/rtos/freertos_critical_section.hpp"
#include "F_platform/tasks/encoder_task.hpp"

namespace Harness {

static Platform::FreeRTOSCriticalSection s_critSection;

bool initEncoderTask(Drivers::EncoderDriverHandle& driverHandle,
                     IClock& clock,
                     IEncoderProcessing& processor,
                     IEncoderStatusSink& statusSink,
                     uint8_t measWindowMs,
                     uint16_t sampleRateHz,
                     IScheduler& scheduler,
                     EncoderTaskResult& result)
{
    // Task init (opens opaque handle — upcasts L4 concrete types to interfaces)
    if (!Scheduler::EncoderTask_Init(*driverHandle.encoder, *driverHandle.timer,
                                      *driverHandle.sampler, s_critSection,
                                      clock, processor, statusSink,
                                      measWindowMs, sampleRateHz)) {
        return false;
    }

    // RTOS task creation
    auto* handle = scheduler.createTask(
        {"Encoder", Scheduler::ENCODER_TASK_STACK_SIZE,
         Scheduler::ENCODER_TASK_PRIORITY,
         Scheduler::vEncoderTask, nullptr});
    if (handle == nullptr) {
        return false;
    }

    // Return harness interfaces for dispatch wiring
    result.encoder = Scheduler::EncoderTask_GetEncoderInterface();
    result.filterControl = Scheduler::EncoderTask_GetFilterInterface();
    return true;
}

} // namespace Harness
