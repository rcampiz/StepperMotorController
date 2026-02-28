/**
 * @file freertos_queue.hpp
 * @brief FreeRTOS implementation of IQueue
 *
 * This file is the ONLY place FreeRTOS queue primitives should appear.
 * All other code accesses queuing through the IQueue interface.
 */

#ifndef PLATFORM_FREERTOS_QUEUE_HPP
#define PLATFORM_FREERTOS_QUEUE_HPP

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/queue.h"
#include "F_platform/interfaces/iqueue.hpp"

template<typename T, size_t Depth>
class FreeRTOSQueue : public IQueue<T, Depth> {
public:
    FreeRTOSQueue() : m_handle(nullptr) {
        m_handle = xQueueCreate(Depth, sizeof(T));
    }

    ~FreeRTOSQueue() override {
        if (m_handle != nullptr) {
            vQueueDelete(m_handle);
        }
    }

    bool send(const T& item, uint32_t timeoutMs = 0) override {
        if (m_handle == nullptr) return false;
        TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY
                                                     : pdMS_TO_TICKS(timeoutMs);
        return xQueueSend(m_handle, &item, ticks) == pdTRUE;
    }

    bool receive(T& item, uint32_t timeoutMs = 0) override {
        if (m_handle == nullptr) return false;
        TickType_t ticks = (timeoutMs == UINT32_MAX) ? portMAX_DELAY
                                                     : pdMS_TO_TICKS(timeoutMs);
        return xQueueReceive(m_handle, &item, ticks) == pdTRUE;
    }

    size_t available() const override {
        if (m_handle == nullptr) return 0;
        return uxQueueMessagesWaiting(m_handle);
    }

    size_t freeSlots() const override {
        if (m_handle == nullptr) return 0;
        return uxQueueSpacesAvailable(m_handle);
    }

    void reset() override {
        if (m_handle != nullptr) {
            xQueueReset(m_handle);
        }
    }

    bool valid() const { return m_handle != nullptr; }

    // Non-copyable
    FreeRTOSQueue(const FreeRTOSQueue&) = delete;
    FreeRTOSQueue& operator=(const FreeRTOSQueue&) = delete;

private:
    QueueHandle_t m_handle;
};

#endif // PLATFORM_FREERTOS_QUEUE_HPP
