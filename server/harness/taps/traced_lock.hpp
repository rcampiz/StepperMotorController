/**
 * @file traced_lock.hpp
 * @brief Traced wrapper for ILock (L5->Foundation boundary)
 *
 * Logs mutex acquire/release via ITrace.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "harness/pins/ilock.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

namespace Harness {

class TracedLock : public Platform::ILock {
public:
    explicit TracedLock(Platform::ILock& real) : m_real(real) {}

    void acquire() override {
        Harness::ITrace::log(Harness::ITrace::L5_F_LOCK, "[L5>F]", "lock.acquire");
        m_real.acquire();
    }

    void release() override {
        m_real.release();
        Harness::ITrace::log(Harness::ITrace::L5_F_LOCK, "[L5<F]", "lock.release");
    }

private:
    Platform::ILock& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
