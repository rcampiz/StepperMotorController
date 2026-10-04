/**
 * @file boundary/ilock.hpp
 * @brief Consumer port — Services side of the Platform::ILock harness connection
 *
 * Structurally identical to Platform::ILock.
 * The harness adapter bridges the provider to this consumer port.
 */

#ifndef SERVICES_ILOCK_HPP
#define SERVICES_ILOCK_HPP

namespace Services {

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

} // namespace Services

#endif // SERVICES_ILOCK_HPP
