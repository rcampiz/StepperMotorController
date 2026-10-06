/**
 * @file interrupt_guard.hpp (virtual Nucleo shadow)
 * @brief Host replacement for the PRIMASK guard: one process-wide recursive mutex.
 *
 * sim/CMakeLists.txt puts sim/vnucleo/shadow ahead of server/ on the include path, so the
 * firmware's trace ring compiles unchanged on the host. Same contract: the guarded block
 * is atomic with respect to every other guarded block.
 */
#ifndef HARNESS_INTERRUPT_GUARD_HPP
#define HARNESS_INTERRUPT_GUARD_HPP

#include <mutex>

class InterruptGuard {
public:
    InterruptGuard() { mutex().lock(); }
    ~InterruptGuard() { mutex().unlock(); }
    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;

private:
    static std::recursive_mutex& mutex() {
        static std::recursive_mutex m;
        return m;
    }
};

#endif // HARNESS_INTERRUPT_GUARD_HPP
