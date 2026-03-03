/**
 * @file iqueue.hpp
 * @brief Abstract typed message queue interface
 *
 * Platform queue abstraction. Implementations may use:
 *   - FreeRTOS queue
 *   - Circular buffer (superloop / unit test)
 *
 * No RTOS headers may appear in this file.
 */

#ifndef PLATFORM_IQUEUE_HPP
#define PLATFORM_IQUEUE_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>

template<typename T, size_t Depth>
class IQueue {
public:
    virtual ~IQueue() = default;

    /**
     * @brief Send an item to the queue
     * @param item Item to enqueue
     * @param timeoutMs Max wait time in ms (0 = non-blocking)
     * @return true if item was enqueued
     */
    virtual bool send(const T& item, uint32_t timeoutMs = 0) = 0;

    /**
     * @brief Receive an item from the queue
     * @param item Output: dequeued item
     * @param timeoutMs Max wait time in ms (0 = non-blocking)
     * @return true if item was dequeued
     */
    virtual bool receive(T& item, uint32_t timeoutMs = 0) = 0;

    /** @brief Number of items currently in the queue */
    virtual size_t available() const = 0;

    /** @brief Number of free slots remaining */
    virtual size_t freeSlots() const = 0;

    /** @brief Discard all items in the queue */
    virtual void reset() = 0;
};

/**
 * @brief Simple circular buffer queue for single-threaded / test contexts
 *
 * Non-blocking: timeoutMs is ignored (send/receive return immediately).
 * No RTOS dependency.
 */
template<typename T, size_t Depth>
class NullQueue : public IQueue<T, Depth> {
public:
    NullQueue() : m_head(0), m_tail(0), m_count(0) {}

    bool send(const T& item, uint32_t /*timeoutMs*/ = 0) override {
        if (m_count >= Depth) return false;
        memcpy(&m_buf[m_head], &item, sizeof(T));
        m_head = (m_head + 1) % Depth;
        ++m_count;
        return true;
    }

    bool receive(T& item, uint32_t /*timeoutMs*/ = 0) override {
        if (m_count == 0) return false;
        memcpy(&item, &m_buf[m_tail], sizeof(T));
        m_tail = (m_tail + 1) % Depth;
        --m_count;
        return true;
    }

    size_t available() const override { return m_count; }
    size_t freeSlots() const override { return Depth - m_count; }
    void reset() override { m_head = m_tail = m_count = 0; }

private:
    T m_buf[Depth];
    size_t m_head;
    size_t m_tail;
    size_t m_count;
};

#endif // PLATFORM_IQUEUE_HPP
