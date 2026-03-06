/**
 * @file freertos_lock.hpp
 * @brief FreeRTOS mutex implementation of ILock
 *
 * This file is the ONLY place FreeRTOS mutex primitives should appear.
 * All other code accesses locking through the ILock interface.
 */

#ifndef PLATFORM_FREERTOS_LOCK_HPP
#define PLATFORM_FREERTOS_LOCK_HPP

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/semphr.h"
#include "harness/pins/ilock.hpp"

namespace Platform {

class FreeRTOSMutex : public Harness::ILock {
public:
    FreeRTOSMutex() : m_sem(nullptr) {
        m_sem = xSemaphoreCreateMutex();
    }

    ~FreeRTOSMutex() override {
        if (m_sem != nullptr) {
            vSemaphoreDelete(m_sem);
        }
    }

    void acquire() override {
        if (m_sem != nullptr) {
            xSemaphoreTake(m_sem, portMAX_DELAY);
        }
    }

    void release() override {
        if (m_sem != nullptr) {
            xSemaphoreGive(m_sem);
        }
    }

    bool valid() const { return m_sem != nullptr; }

    // Non-copyable
    FreeRTOSMutex(const FreeRTOSMutex&) = delete;
    FreeRTOSMutex& operator=(const FreeRTOSMutex&) = delete;

private:
    SemaphoreHandle_t m_sem;
};

} // namespace Platform

#endif // PLATFORM_FREERTOS_LOCK_HPP
