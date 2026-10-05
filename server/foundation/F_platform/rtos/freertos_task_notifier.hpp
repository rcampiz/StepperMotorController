/**
 * @file freertos_task_notifier.hpp
 * @brief FreeRTOS implementation of Platform::ITaskNotifier (direct-to-task notifications)
 */
#pragma once

#include "F_platform/boundary/itask_notifier.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace Platform {

class FreeRTOSTaskNotifier : public ITaskNotifier {
public:
    void notifyFromISR(TaskHandle task) override {
        if (task == nullptr) return;
        BaseType_t woken = pdFALSE;
        vTaskNotifyGiveFromISR(static_cast<TaskHandle_t>(task), &woken);
        portYIELD_FROM_ISR(woken);
    }

    bool waitForNotification(uint32_t timeoutMs) override {
        TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        return ulTaskNotifyTake(pdTRUE, ticks) != 0;
    }
};

} // namespace Platform
