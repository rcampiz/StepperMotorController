/**
 * @file ilock.hpp
 * @brief Abstract mutual-exclusion lock interface
 *
 * Platform synchronization abstraction. Implementations may use:
 *   - FreeRTOS mutex
 *   - Interrupt masking
 *   - Spinlock
 *   - Cooperative scheduler guard
 *   - No-op (single-threaded / super-loop)
 *
 * No RTOS headers may appear in this file.
 */

#ifndef PLATFORM_ILOCK_HPP
#define PLATFORM_ILOCK_HPP

class ILock {
public:
    virtual ~ILock() = default;

    /** @brief Acquire the lock (blocks until available) */
    virtual void acquire() = 0;

    /** @brief Release the lock */
    virtual void release() = 0;
};

/**
 * @brief No-op lock for single-threaded / super-loop contexts
 *
 * Satisfies the ILock interface with zero overhead.
 * Use when no concurrency protection is needed.
 */
class NullLock : public ILock {
public:
    void acquire() override {}
    void release() override {}
};

#endif // PLATFORM_ILOCK_HPP
