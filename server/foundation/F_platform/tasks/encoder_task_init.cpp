/**
 * @file encoder_task_init.cpp
 * @brief Implements Harness::initEncoderTask() — task creation only
 */

#include "F_platform/tasks/encoder_task_init.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "F_platform/boundary/ischeduler.hpp"

namespace Harness {

bool initEncoderTask(Services::IEncoderService& service,
                     Platform::IClock& clock,
                     Platform::IScheduler& scheduler)
{
    if (!Scheduler::EncoderTask_Init(service, clock)) {
        return false;
    }
    auto* handle = scheduler.createTask(
        {"Encoder", Scheduler::ENCODER_TASK_STACK_SIZE,
         Scheduler::ENCODER_TASK_PRIORITY,
         Scheduler::vEncoderTask, nullptr});
    return handle != nullptr;
}

} // namespace Harness
