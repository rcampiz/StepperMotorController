/**
 * @file itask_notifier.hpp
 * @brief Platform:: primitive — ISR-to-task wake-up (direct task notification)
 *
 * Part of the Platform power net (rule 4). notifyFromISR() is the only member that may be
 * called from interrupt context, and only from an ISR whose NVIC priority is numerically
 * >= configMAX_SYSCALL_INTERRUPT_PRIORITY. waitForNotification() blocks the calling task.
 */
#ifndef PLATFORM_ITASK_NOTIFIER_HPP
#define PLATFORM_ITASK_NOTIFIER_HPP

#include <stdint.h>
#include "F_platform/boundary/ischeduler.hpp"

namespace Platform {

class ITaskNotifier {
public:
    virtual ~ITaskNotifier() = default;

    /// Wake `task`. ISR context only. Coalesces: several notifies before a wait count as one.
    virtual void notifyFromISR(TaskHandle task) = 0;

    /// Block the calling task until notified or timeoutMs elapses. Returns true if notified.
    virtual bool waitForNotification(uint32_t timeoutMs) = 0;
};

class NullTaskNotifier : public ITaskNotifier {
public:
    void notifyFromISR(TaskHandle) override {}
    bool waitForNotification(uint32_t) override { return false; }
};

} // namespace Platform

#endif // PLATFORM_ITASK_NOTIFIER_HPP
