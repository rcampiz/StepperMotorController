/**
 * @file fake_board.hpp
 * @brief Host-side fakes for the L5 Board:: provider interfaces (lower side of H_L4_L5).
 */
#pragma once
#include "harness/defs/harness_l4_l5.hpp"
#include <vector>

namespace fakes {

struct FakeGPIO : Board::IGPIO {
    bool level = false;
    int writes = 0;
    void write(bool v) override { level = v; ++writes; }
    bool read() const override { return level; }
};

struct FakeSPIBus : Board::ISPIBus {
    std::vector<uint8_t> tx;           ///< every byte sent, in order
    std::vector<uint8_t> rxScript;     ///< bytes returned by transfer(), consumed in order
    Board::SPIMode mode = Board::SPIMode::Mode0;
    Board::SPIPrescaler prescaler = Board::SPIPrescaler::Div2;
    int locks = 0, unlocks = 0;

    void lock() override { ++locks; }
    void unlock() override { ++unlocks; }
    void setMode(Board::SPIMode m) override { mode = m; }
    Board::SPIMode getMode() const override { return mode; }
    void setPrescaler(Board::SPIPrescaler p) override { prescaler = p; }
    uint8_t transfer(uint8_t data) override {
        tx.push_back(data);
        if (rxScript.empty()) return 0;
        uint8_t r = rxScript.front();
        rxScript.erase(rxScript.begin());
        return r;
    }
    void transfer(const uint8_t* txBuf, uint8_t* rxBuf, size_t len) override {
        for (size_t i = 0; i < len; ++i) {
            uint8_t r = transfer(txBuf ? txBuf[i] : 0);
            if (rxBuf) rxBuf[i] = r;
        }
    }
};

struct FakeTearingSync : Board::ITearingSync {
    int waits = 0;
    void waitForVSync() override { ++waits; }
};

/// STEP pulse timer driven by the test: fire() is one pulse and its interrupt; advance(t)
/// fires every pulse due within t ticks. Each pulse records the DIR line's level.
struct FakeStepPulseTimer : Board::IStepPulseTimer {
    uint32_t hz = 1000000, minTicks = 20;
    PulseHandler handler = nullptr;
    void* context = nullptr;
    bool on = false;
    uint32_t untilNext = 0;           ///< ticks to the next pulse while on
    uint64_t now = 0;                 ///< ticks advanced so far
    std::vector<uint32_t> periods;    ///< the first period and every one the handler returned
    std::vector<uint64_t> pulseAt;    ///< when each pulse went out
    std::vector<bool> dirAt;          ///< the DIR level at each pulse
    const Board::IGPIO* dir = nullptr;
    int starts = 0, stops = 0;

    uint32_t tickHz() const override { return hz; }
    uint32_t minPeriodTicks() const override { return minTicks; }
    void setHandler(PulseHandler h, void* c) override { handler = h; context = c; }
    void start(uint32_t first) override { on = true; untilNext = first; periods.push_back(first); ++starts; }
    void stop() override { on = false; ++stops; }
    bool running() const override { return on; }

    /// One pulse now and its interrupt; false when the timer is off
    bool fire()
    {
        if (!on || handler == nullptr) return false;
        pulseAt.push_back(now);
        dirAt.push_back(dir != nullptr && dir->read());
        const uint32_t next = handler(context);
        if (next == 0) { on = false; return true; }
        untilNext = next;
        periods.push_back(next);
        return true;
    }
    /// Let `ticks` pass, firing every pulse that falls due
    void advance(uint64_t ticks)
    {
        const uint64_t end = now + ticks;
        while (on && now + untilNext <= end) {
            now += untilNext;
            fire();
        }
        if (on) untilNext -= static_cast<uint32_t>(end - now);
        now = end;
    }
    /// Fire until the timer stops by itself (at most `limit` pulses): the pulses fired
    size_t runToStop(size_t limit = 10000000)
    {
        size_t n = 0;
        while (on && n < limit) {
            now += untilNext;
            fire();
            ++n;
        }
        return n;
    }
};

struct FakeInternalFlash : Board::IInternalFlash {
    int erased = -1;
    std::vector<uint8_t> written;
    uint32_t writtenAddr = 0;
    bool eraseSector(uint8_t s) override { erased = s; return true; }
    bool writeData(uint32_t addr, const uint8_t* d, size_t n) override {
        writtenAddr = addr; written.assign(d, d + n); return true;
    }
    ConfigRegion configRegion() const override { return {0x0807F000u, 7, 0x1000u, 0u}; }
};

} // namespace fakes
