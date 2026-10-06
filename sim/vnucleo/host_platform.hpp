/**
 * @file host_platform.hpp
 * @brief Platform power net on a desktop OS: clock, locks, queues, critical section,
 *        microsecond clock, all on std::chrono / std::mutex (virtual Nucleo only).
 */
#pragma once

#include "F_platform/boundary/iclock.hpp"
#include "F_platform/boundary/icritical_section.hpp"
#include "F_platform/boundary/ilock.hpp"
#include "F_platform/boundary/imicrosecond_clock.hpp"
#include "F_platform/boundary/iqueue.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

namespace Sim {

using SteadyClock = std::chrono::steady_clock;

/// Milliseconds and microseconds since process start (the board's "power-on").
inline SteadyClock::time_point epoch() {
    static const SteadyClock::time_point t0 = SteadyClock::now();
    return t0;
}
inline uint32_t nowMs() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        SteadyClock::now() - epoch()).count());
}
inline uint32_t nowUs() {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        SteadyClock::now() - epoch()).count());
}
inline double nowSeconds() {
    return std::chrono::duration<double>(SteadyClock::now() - epoch()).count();
}

class HostClock : public Platform::IClock {
public:
    uint32_t getTickMs() const override { return nowMs(); }
    uint32_t getTickMsFromISR() const override { return nowMs(); }
    void delayMs(uint32_t ms) override { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
    void sleepUntilMs(uint32_t& lastWakeMs, uint32_t periodMs) override {
        lastWakeMs += periodMs;
        const uint32_t now = nowMs();
        const int32_t left = static_cast<int32_t>(lastWakeMs - now);
        if (left > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(left));
        } else if (left < -static_cast<int32_t>(10 * periodMs)) {
            lastWakeMs = now;                       // fell far behind: do not burst
        }
    }
};

class HostMicrosecondClock : public Platform::IMicrosecondClock {
public:
    uint32_t getTickUs() const override { return nowUs(); }
};

/// FreeRTOS mutexes are recursive-safe in this firmware's use; a recursive mutex keeps the
/// host from deadlocking on any nested acquire the target tolerates.
class HostLock : public Platform::ILock {
public:
    void acquire() override { m_.lock(); }
    void release() override { m_.unlock(); }
private:
    std::recursive_mutex m_;
};

class HostCritical : public Platform::ICriticalSection {
public:
    void enter() override { m_.lock(); }
    void exit() override { m_.unlock(); }
private:
    std::recursive_mutex m_;
};

/// Bounded queue with the FreeRTOS semantics the firmware relies on (copy in, timeouts).
template <typename T, size_t Depth>
class HostQueue : public Platform::IQueue<T, Depth> {
public:
    bool send(const T& item, uint32_t timeoutMs = 0) override {
        std::unique_lock<std::mutex> lk(m_);
        if (!notFull_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                               [this] { return count_ < Depth; })) {
            return false;
        }
        std::memcpy(&buf_[head_], &item, sizeof(T));
        head_ = (head_ + 1) % Depth;
        ++count_;
        notEmpty_.notify_one();
        return true;
    }
    bool receive(T& item, uint32_t timeoutMs = 0) override {
        std::unique_lock<std::mutex> lk(m_);
        if (!notEmpty_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                [this] { return count_ > 0; })) {
            return false;
        }
        std::memcpy(&item, &buf_[tail_], sizeof(T));
        tail_ = (tail_ + 1) % Depth;
        --count_;
        notFull_.notify_one();
        return true;
    }
    size_t available() const override { std::lock_guard<std::mutex> lk(m_); return count_; }
    size_t freeSlots() const override { std::lock_guard<std::mutex> lk(m_); return Depth - count_; }
    void reset() override {
        std::lock_guard<std::mutex> lk(m_);
        head_ = tail_ = count_ = 0;
        notFull_.notify_all();
    }
private:
    mutable std::mutex m_;
    std::condition_variable notEmpty_, notFull_;
    T buf_[Depth];
    size_t head_ = 0, tail_ = 0, count_ = 0;
};

} // namespace Sim
