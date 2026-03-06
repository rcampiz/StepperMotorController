/**
 * @file freertos_scheduler.hpp
 * @brief FreeRTOS implementation of Harness::IScheduler
 *
 * Wraps xTaskCreate, vTaskStartScheduler, vTaskSuspend, vTaskResume.
 */

#ifndef PLATFORM_FREERTOS_SCHEDULER_HPP
#define PLATFORM_FREERTOS_SCHEDULER_HPP

#include "harness/pins/ischeduler.hpp"

namespace Platform {

class FreeRTOSScheduler : public Harness::IScheduler {
public:
    Harness::TaskHandle createTask(const Harness::TaskDescriptor& desc) override;
    [[noreturn]] void start() override;
    void suspendTask(Harness::TaskHandle h) override;
    void resumeTask(Harness::TaskHandle h) override;
};

} // namespace Platform

#endif // PLATFORM_FREERTOS_SCHEDULER_HPP
