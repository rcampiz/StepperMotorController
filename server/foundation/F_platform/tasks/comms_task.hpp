/**
 * @file comms_task.hpp
 * @brief Comms task — thin sheet: wait 10 ms, call Services::ICommsService::tick()
 *
 * All comms logic lives in L3 CommsService (H_TASKS_L3).
 */
#ifndef COMMS_TASK_HPP
#define COMMS_TASK_HPP

#include "F_platform/boundary/ischeduler.hpp"
#include "F_platform/tasks/task_stacks.hpp"
#include <stdint.h>

namespace Platform { class IClock; class ITaskNotifier; }
namespace Services { class ICommsService; }

namespace Scheduler {

// COMMS_TASK_STACK_SIZE: tasks/task_stacks.hpp (all the stack sizes, in one place)
constexpr uint32_t COMMS_TASK_PRIORITY = 3;
constexpr uint32_t COMMS_POLL_PERIOD_MS = 10;     // Fallback poll when no RX wake arrives

bool CommsTask_Init(Services::ICommsService& service, Platform::IClock& clock,
                    Platform::ITaskNotifier* rxWake);
void vCommsTask(void* pvParameters);

extern Platform::TaskHandle g_commsTaskHandle;

} // namespace Scheduler

#endif // COMMS_TASK_HPP
