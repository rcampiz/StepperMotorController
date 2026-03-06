/**
 * @file ischeduler.hpp
 * @brief Abstract scheduler interface
 *
 * Boundary contract for task creation and lifecycle.
 * Implementations may use:
 *   - FreeRTOS xTaskCreate + vTaskStartScheduler
 *   - Bare-metal super-loop with cooperative dispatch
 *   - Stub (unit test)
 *
 * No RTOS headers may appear in this file.
 */

#ifndef HARNESS_ISCHEDULER_HPP
#define HARNESS_ISCHEDULER_HPP

#include <stdint.h>

namespace Harness {

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

} // namespace Harness

#endif // HARNESS_ISCHEDULER_HPP
