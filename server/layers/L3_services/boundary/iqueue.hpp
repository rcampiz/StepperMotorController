/**
 * @file boundary/iqueue.hpp
 * @brief Consumer port — Services side of the Platform::IQueue harness connection
 *
 * Structurally identical to Platform::IQueue.
 * The harness adapter bridges the provider to this consumer port.
 */

#ifndef SERVICES_IQUEUE_HPP
#define SERVICES_IQUEUE_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace Services {

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

} // namespace Services

#endif // SERVICES_IQUEUE_HPP
