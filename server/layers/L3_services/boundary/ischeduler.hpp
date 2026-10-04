/**
 * @file boundary/ischeduler.hpp
 * @brief Consumer port — Services side of the Platform::IScheduler harness connection
 *
 * Structurally identical to Platform::IScheduler.
 * The harness adapter bridges the provider to this consumer port.
 */

#ifndef SERVICES_ISCHEDULER_HPP
#define SERVICES_ISCHEDULER_HPP

#include <stdint.h>

namespace Services {

/// Task creation descriptor (RTOS-free POD)
struct TaskDescriptor {
    const char* name;
    uint32_t    stackSizeWords;
    uint8_t     priority;       ///< Higher = more important
    void      (*entry)(void*);
    void*       parameter;
};

/// Opaque task handle (FreeRTOS: TaskHandle_t, super-loop: index/pointer)
using TaskHandle = void*;

/// Abstract scheduler interface
class IScheduler {
public:
    virtual ~IScheduler() = default;

    /**
     * @brief Create and register a task
     * @param desc Task descriptor
     * @return Opaque handle (nullptr on failure)
     */
    virtual TaskHandle createTask(const TaskDescriptor& desc) = 0;

    /**
     * @brief Start the scheduler — never returns
     *
     * RTOS: calls vTaskStartScheduler().
     * Super-loop: enters cooperative dispatch loop.
     */
    [[noreturn]] virtual void start() = 0;

    /**
     * @brief Suspend a running task
     * @param h Task handle from createTask() (no-op if nullptr)
     */
    virtual void suspendTask(TaskHandle h) = 0;

    /**
     * @brief Resume a suspended task
     * @param h Task handle from createTask() (no-op if nullptr)
     */
    virtual void resumeTask(TaskHandle h) = 0;
};

} // namespace Services

#endif // SERVICES_ISCHEDULER_HPP
