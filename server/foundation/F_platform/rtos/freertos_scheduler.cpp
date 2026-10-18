/**
 * @file freertos_scheduler.cpp
 * @brief FreeRTOS implementation of Platform::IScheduler
 */

#include "F_platform/rtos/freertos_scheduler.hpp"
#include "F_platform/hw/early_debug.hpp"
#include "F_platform/hw/early_debug_board.hpp"

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

#ifdef ENABLE_SEGGER_SYSTEMVIEW
#include "X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.h"
#endif

namespace Platform {

// F11: no kernel heap. TCBs and stacks come from static storage; the stack pool is
// carved sequentially at init (pre-scheduler, single-threaded), never freed.
static StaticTask_t s_tcbs[FreeRTOSScheduler::MAX_TASKS];
static StackType_t  s_stackPool[FreeRTOSScheduler::STACK_POOL_WORDS];
static uint32_t     s_tasksCreated = 0;
static uint32_t     s_poolUsed = 0;

uint32_t FreeRTOSScheduler::stackPoolFreeWords()
{
    return STACK_POOL_WORDS - s_poolUsed;
}

Platform::TaskHandle FreeRTOSScheduler::createTask(const Platform::TaskDescriptor& desc)
{
    if (s_tasksCreated >= MAX_TASKS || s_poolUsed + desc.stackSizeWords > STACK_POOL_WORDS) {
        EarlyDebug::print("createTask FAIL (static pool exhausted): ");
        EarlyDebug::println(desc.name);
        return nullptr;
    }
    StackType_t* stack = &s_stackPool[s_poolUsed];
    TaskHandle_t handle = xTaskCreateStatic(
        desc.entry,
        desc.name,
        static_cast<configSTACK_DEPTH_TYPE>(desc.stackSizeWords),
        desc.parameter,
        tskIDLE_PRIORITY + desc.priority,
        stack,
        &s_tcbs[s_tasksCreated]
    );
    if (handle == nullptr) {
        EarlyDebug::print("xTaskCreateStatic FAIL: ");
        EarlyDebug::println(desc.name);
        return nullptr;
    }
    s_poolUsed += desc.stackSizeWords;
    s_tasksCreated++;
    return static_cast<Platform::TaskHandle>(handle);
}

void FreeRTOSScheduler::start()
{
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();
    SEGGER_SYSVIEW_Start();
#endif

    // Through the guard (F16): dropped once the composition root retired the debug UART
    Platform::debugPrintln("Starting scheduler...");
    Platform::debugPrintln("===================");

    vTaskStartScheduler();

    EarlyDebug::println("!!! SCHEDULER RETURNED !!!");
    while (1) {}
}

void FreeRTOSScheduler::suspendTask(Platform::TaskHandle h)
{
    if (h != nullptr) {
        vTaskSuspend(static_cast<TaskHandle_t>(h));
    }
}

void FreeRTOSScheduler::resumeTask(Platform::TaskHandle h)
{
    if (h != nullptr) {
        vTaskResume(static_cast<TaskHandle_t>(h));
    }
}

} // namespace Platform
