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

struct FakeInternalFlash : Board::IInternalFlash {
    int erased = -1;
    std::vector<uint8_t> written;
    uint32_t writtenAddr = 0;
    bool eraseSector(uint8_t s) override { erased = s; return true; }
    bool writeData(uint32_t addr, const uint8_t* d, size_t n) override {
        writtenAddr = addr; written.assign(d, d + n); return true;
    }
};

} // namespace fakes
