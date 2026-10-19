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
    /// Static stack pool shared by every task created through this scheduler (F11).
    /// Motor 1024 + Comms 2048 + Encoder 256 + Display 640 = 3968 words today.
    static constexpr uint32_t STACK_POOL_WORDS = 4096;
    static constexpr uint32_t MAX_TASKS = 6;
    /// Words of the pool not yet handed to a task (diagnostics).
    static uint32_t stackPoolFreeWords();
    /// Stack size (words) of a task this scheduler created; 0 for any other (the idle task).
    static uint32_t stackWordsOf(const void* handle);

    Platform::TaskHandle createTask(const Platform::TaskDescriptor& desc) override;
    [[noreturn]] void start() override;
    void suspendTask(Platform::TaskHandle h) override;
    void resumeTask(Platform::TaskHandle h) override;
};

} // namespace Platform

#endif // PLATFORM_FREERTOS_SCHEDULER_HPP
