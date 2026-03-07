/**
 * @file freertos_scheduler.cpp
 * @brief FreeRTOS implementation of Platform::IScheduler
 */

#include "F_platform/rtos/freertos_scheduler.hpp"
#include "F_platform/hw/early_debug.hpp"

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

#ifdef ENABLE_SEGGER_SYSTEMVIEW
#include "X_middlewares/SEGGER/SystemView/SEGGER_SYSVIEW.h"
#endif

namespace Platform {

Platform::TaskHandle FreeRTOSScheduler::createTask(const Platform::TaskDescriptor& desc)
{
    TaskHandle_t handle = nullptr;
    BaseType_t result = xTaskCreate(
        desc.entry,
        desc.name,
        static_cast<configSTACK_DEPTH_TYPE>(desc.stackSizeWords),
        desc.parameter,
        tskIDLE_PRIORITY + desc.priority,
        &handle
    );
    if (result != pdPASS) {
        EarlyDebug::print("xTaskCreate FAIL: ");
        EarlyDebug::println(desc.name);
        return nullptr;
    }
    return static_cast<Platform::TaskHandle>(handle);
}

void FreeRTOSScheduler::start()
{
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();
    SEGGER_SYSVIEW_Start();
#endif

    EarlyDebug::println("Starting scheduler...");
    EarlyDebug::println("===================");

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
