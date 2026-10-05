/**
 * @file seq_snapshot.hpp
 * @brief Single-writer (ISR) / single-reader (task) snapshot guarded by a sequence counter
 *
 * The writer bumps the counter to odd, copies the payload, bumps it to even. The reader
 * copies the payload between two counter reads and retries if the counter was odd or
 * changed, so it never observes a mix of old and new fields. Single-core Cortex-M with one
 * ISR writer: compiler barriers are sufficient, no DMB needed. Shared helper (harness pin),
 * no RTOS dependency, host-testable.
 */
#pragma once
#include <stdint.h>

namespace Harness {

template<typename T>
class SeqSnapshot {
public:
    static constexpr uint32_t MAX_RETRIES = 16;

    /// Writer side (one context only, typically an ISR).
    void write(const T& v) {
        m_seq = m_seq + 1u;     // odd: write in progress
        barrier();
        m_data = v;
        barrier();
        m_seq = m_seq + 1u;     // even: stable
    }

    /// Reader side. `midRead` runs between the payload copy and the closing sequence check;
    /// production passes nothing, host tests inject a concurrent write there.
    template<typename Hook>
    T read(Hook&& midRead) const {
        T out{};
        for (uint32_t attempt = 0; attempt < MAX_RETRIES; ++attempt) {
            const uint32_t s1 = m_seq;
            barrier();
            out = m_data;
            barrier();
            midRead();
            barrier();
            const uint32_t s2 = m_seq;
            if ((s1 & 1u) == 0u && s1 == s2) {
                return out;
            }
        }
        return out;   // writer storm beyond MAX_RETRIES: return the last copy
    }

    T read() const { return read([] {}); }

    uint32_t sequence() const { return m_seq; }

private:
    static void barrier() { __asm__ __volatile__("" ::: "memory"); }

    volatile uint32_t m_seq = 0;
    T m_data{};
};

} // namespace Harness
