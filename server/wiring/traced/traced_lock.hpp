/**
 * @file traced_lock.hpp
 * @brief Traced wrapper for ILock (L5->Foundation boundary)
 *
 * Logs mutex acquire/release via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "F_platform/hal/ilock.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

class TracedLock : public ILock {
public:
    explicit TracedLock(ILock& real) : m_real(real) {}

    void acquire() override {
        ITrace::log(ITrace::L5_F_LOCK, "[L5>F]", "lock.acquire");
        m_real.acquire();
    }

    void release() override {
        m_real.release();
        ITrace::log(ITrace::L5_F_LOCK, "[L5<F]", "lock.release");
    }

private:
    ILock& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
