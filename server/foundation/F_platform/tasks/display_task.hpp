/**
 * @file display_task.hpp
 * @brief Display task — thin sheet: wait 50 ms, call Services::IDisplayService::tick()
 *
 * All UI logic lives in L3 DisplayService (H_TASKS_L3). The task keeps its handle so the
 * debug service can suspend/resume it through the Platform scheduler.
 */
#ifndef DISPLAY_TASK_HPP
#define DISPLAY_TASK_HPP

#include "F_platform/boundary/ischeduler.hpp"   // Platform::TaskHandle
#include "F_platform/tasks/task_stacks.hpp"
#include <stdint.h>

namespace Platform { class IClock; }
namespace Services { class IDisplayService; }

namespace Scheduler {

// DISPLAY_TASK_STACK_SIZE: tasks/task_stacks.hpp (all the stack sizes, in one place)
constexpr uint32_t DISPLAY_TASK_PRIORITY = 1;
constexpr uint32_t DISPLAY_REFRESH_PERIOD_MS = 50; // 20 Hz refresh

bool DisplayTask_Init(Services::IDisplayService& service, Platform::IClock& clock);
void vDisplayTask(void* pvParameters);

extern Platform::TaskHandle g_displayTaskHandle;

} // namespace Scheduler

#endif // DISPLAY_TASK_HPP
