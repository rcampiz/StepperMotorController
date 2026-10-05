/**
 * @file comms_task.hpp
 * @brief Comms task — thin sheet: wait 10 ms, call Services::ICommsService::tick()
 *
 * All comms logic lives in L3 CommsService (H_TASKS_L3).
 */
#ifndef COMMS_TASK_HPP
#define COMMS_TASK_HPP

#include "F_platform/boundary/ischeduler.hpp"
#include <stdint.h>

namespace Platform { class IClock; class ITaskNotifier; }
namespace Services { class ICommsService; }

namespace Scheduler {

constexpr uint32_t COMMS_TASK_STACK_SIZE = 2048;  // 8192 bytes (GET_STATUS JSON uses 640B local buf + snprintf)
constexpr uint32_t COMMS_TASK_PRIORITY = 3;
constexpr uint32_t COMMS_POLL_PERIOD_MS = 10;     // Fallback poll when no RX wake arrives

bool CommsTask_Init(Services::ICommsService& service, Platform::IClock& clock,
                    Platform::ITaskNotifier* rxWake);
void vCommsTask(void* pvParameters);

extern Platform::TaskHandle g_commsTaskHandle;

} // namespace Scheduler

#endif // COMMS_TASK_HPP
