/**
 * @file freertos_scheduler.hpp
 * @brief FreeRTOS implementation of Platform::IScheduler
 *
 * Wraps xTaskCreate, vTaskStartScheduler, vTaskSuspend, vTaskResume.
 */

#ifndef PLATFORM_FREERTOS_SCHEDULER_HPP
#define PLATFORM_FREERTOS_SCHEDULER_HPP

#include "harness/pins/ischeduler.hpp"

namespace Platform {

class FreeRTOSScheduler : public Platform::IScheduler {
public:
    Platform::TaskHandle createTask(const Platform::TaskDescriptor& desc) override;
    [[noreturn]] void start() override;
    void suspendTask(Platform::TaskHandle h) override;
    void resumeTask(Platform::TaskHandle h) override;
};

} // namespace Platform

#endif // PLATFORM_FREERTOS_SCHEDULER_HPP
