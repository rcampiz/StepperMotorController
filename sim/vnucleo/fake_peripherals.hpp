/**
 * @file fake_peripherals.hpp
 * @brief The rest of the virtual board's hardware: quadrature encoder (fed by the fake
 *        motor), remote LCD (counts what it is asked to draw), motor-config store and
 *        NOR flash in RAM (optionally persisted to a file per instance).
 */
#pragma once

#include "fake_powerstep.hpp"
#include "host_platform.hpp"

#include "L3_services/boundary/iremote_display.hpp"
#include "L4_drivers/boundary/iconfig_store.hpp"
#include "L4_drivers/boundary/iencoder_hardware.hpp"
#include "L4_drivers/boundary/iflash.hpp"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Sim {

/// TIM-counter encoder on the motor shaft: counts = microsteps x (4 x PPR) / microsteps-per-rev.
/// The DMA sample ring is filled lazily at the sample rate when the service looks at it.
class FakeEncoderHw : public Drivers::IEncoderHardware {
public:
    static constexpr uint32_t RING = 4096;
    FakeEncoderHw(FakePowerStep& motor, uint32_t ppr = 1000, uint32_t fullStepsPerRev = 200)
        : motor_(motor), ppr_(ppr), fspr_(fullStepsPerRev) {}

    bool isReady() const override { return true; }
    int32_t getCount() const override { return count(); }
    void reset() override {
        std::lock_guard<std::mutex> lk(m_);
        offset_ = rawCount();
    }
    Drivers::IndexSnapshot readIndex() const override {
        Drivers::IndexSnapshot s;
        const int32_t c = count();
        const int32_t perRev = static_cast<int32_t>(4 * ppr_);
        s.revolutions = c / perRev;
        s.pulses = static_cast<uint32_t>(std::abs(s.revolutions));
        s.tickMs = nowMs();
        return s;
    }
    void indexISR(uint32_t, uint32_t) override {}
    const uint16_t* sampleBuffer() const override { return ring_; }
    const uint16_t* syncBuffer() const override { return sync_; }
    uint32_t sampleBufferSize() const override { return RING; }
    void pauseSampling() override { std::lock_guard<std::mutex> lk(m_); paused_ = true; }
    uint32_t sampleWriteIndex() const override {
        std::lock_guard<std::mutex> lk(m_);
        if (paused_) return widx_;
        const double now = nowSeconds();
        const uint32_t due = static_cast<uint32_t>((now - t0_) * rate_);
        const int32_t c = count();
        const int32_t sy = syncCount();
        uint32_t n = due - written_;
        if (n > RING) n = RING;                 // a long gap: keep the newest RING samples
        for (uint32_t i = 1; i <= n; ++i) {     // interpolate from the last sample to now
            const double f = static_cast<double>(i) / n;
            ring_[widx_] = static_cast<uint16_t>(lastC_ + std::lround((c - lastC_) * f));
            sync_[widx_] = static_cast<uint16_t>(lastS_ + std::lround((sy - lastS_) * f));
            widx_ = (widx_ + 1) % RING;
        }
        written_ = due;
        lastC_ = c;
        lastS_ = sy;
        return widx_;
    }
    uint16_t sampleRateHz() const override { return rate_; }
    void restartSampling(uint16_t hz) override {
        std::lock_guard<std::mutex> lk(m_);
        rate_ = std::max<uint16_t>(100, std::min<uint16_t>(10000, hz));
        lastC_ = count();
        lastS_ = syncCount();
        for (auto& v : ring_) v = static_cast<uint16_t>(lastC_);
        for (auto& v : sync_) v = static_cast<uint16_t>(lastS_);
        widx_ = 0;
        written_ = 0;
        paused_ = false;
        t0_ = nowSeconds();
    }

private:
    int32_t rawCount() const {
        const double usPerRev = static_cast<double>(fspr_) * motor_.microstepsPerFullStep();
        return static_cast<int32_t>(std::lround(motor_.positionMicrosteps() * 4.0 * ppr_ / usPerRev));
    }
    int32_t count() const { return rawCount() - offset_; }
    /// The chip's SYNC pulses: 8 per full step of commanded position (what TIM3 counts)
    int32_t syncCount() const {
        return static_cast<int32_t>(std::lround(motor_.positionMicrosteps() * 8.0 / motor_.microstepsPerFullStep()));
    }

    FakePowerStep& motor_;
    uint32_t ppr_, fspr_;
    mutable std::mutex m_;
    mutable uint16_t ring_[RING] = {};
    mutable uint16_t sync_[RING] = {};
    mutable int32_t lastS_ = 0;
    mutable bool paused_ = false;
    mutable uint32_t widx_ = 0;
    mutable uint32_t written_ = 0;
    mutable int32_t lastC_ = 0;
    int32_t offset_ = 0;
    uint16_t rate_ = 1000;
    double t0_ = 0.0;
};

/// Remote LCD: accepts everything and counts it (the GUI's Display tab works end to end).
class FakeRemoteDisplay : public Services::IRemoteDisplay {
public:
    void clear(uint16_t) override { ++draws; }
    void text(uint16_t, uint16_t, const char*, uint16_t, uint16_t) override { ++draws; }
    void rect(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t, bool) override { ++draws; }
    void line(int16_t, int16_t, int16_t, int16_t, uint16_t) override { ++draws; }
    void bitmap(uint16_t, uint16_t, uint16_t, uint16_t, const uint8_t*, uint32_t) override { ++draws; }
    void indicator(uint16_t, int8_t, bool) override { ++draws; }
    bool streamStart(uint16_t, uint16_t, uint16_t, uint16_t) override { ++draws; return true; }
    void streamData(const uint8_t*, uint32_t len) override { streamed += len; }
    void streamEnd() override {}
    std::atomic<uint32_t> draws{0};
    std::atomic<uint64_t> streamed{0};
};

/// Byte store backed by RAM and, when a path is given, by a file (survives a restart).
class FileBlob {
public:
    FileBlob(size_t size, std::string path) : data_(size, 0xFF), path_(std::move(path)) {
        if (!path_.empty()) {
            if (FILE* f = std::fopen(path_.c_str(), "rb")) {
                size_t n = std::fread(data_.data(), 1, data_.size(), f);
                (void)n;
                std::fclose(f);
            }
        }
    }
    void persist() {
        if (path_.empty()) return;
        if (FILE* f = std::fopen(path_.c_str(), "wb")) {
            std::fwrite(data_.data(), 1, data_.size(), f);
            std::fclose(f);
        }
    }
    std::vector<uint8_t> data_;
    std::string path_;
};

/// Internal-flash motor config sector (Drivers::IConfigStore).
class FakeConfigStore : public Drivers::IConfigStore {
public:
    explicit FakeConfigStore(const std::string& path) : blob_(1024, path) {}
    size_t capacity() const override { return blob_.data_.size(); }
    bool erase() override {
        std::lock_guard<std::mutex> lk(m_);
        std::fill(blob_.data_.begin(), blob_.data_.end(), 0xFF);
        blob_.persist();
        return true;
    }
    bool write(const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lk(m_);
        if (len > blob_.data_.size()) return false;
        std::memcpy(blob_.data_.data(), data, len);
        blob_.persist();
        return true;
    }
    size_t read(uint8_t* buf, size_t len) const override {
        std::lock_guard<std::mutex> lk(m_);
        const size_t n = std::min(len, blob_.data_.size());
        std::memcpy(buf, blob_.data_.data(), n);
        return n;
    }
private:
    mutable std::mutex m_;
    FileBlob blob_;
};

/// SPI NOR flash (W25Q-like): 4 KB sectors, 256-byte pages, erased = 0xFF.
class FakeNorFlash : public Drivers::IFlash {
public:
    FakeNorFlash(uint32_t bytes, const std::string& path) : blob_(bytes, path) {}
    void read(uint32_t addr, uint8_t* buf, size_t len) override {
        std::lock_guard<std::mutex> lk(m_);
        for (size_t i = 0; i < len; ++i) buf[i] = at(addr + i);
    }
    void readStart(uint32_t addr, uint8_t* buf, size_t len) override { read(addr, buf, len); }
    void readFinish() override {}
    void sectorErase(uint32_t addr) override { erase(addr & ~0xFFFu, 4096); }
    void blockErase64(uint32_t addr) override { erase(addr & ~0xFFFFu, 65536); }
    void pageProgram(uint32_t addr, const uint8_t* data, size_t len) override {
        std::lock_guard<std::mutex> lk(m_);
        for (size_t i = 0; i < len && addr + i < blob_.data_.size(); ++i) {
            blob_.data_[addr + i] &= data[i];          // NOR programs 1 -> 0 only
        }
        blob_.persist();
    }
    Drivers::FlashJedecId readJEDEC() override {
        Drivers::FlashJedecId id{};
        id.manufacturer = 0xEF;
        id.memoryType = 0x40;
        id.capacity = 0x17;
        return id;
    }
    uint32_t capacityBytes() override { return static_cast<uint32_t>(blob_.data_.size()); }
private:
    uint8_t at(size_t a) const { return a < blob_.data_.size() ? blob_.data_[a] : 0xFF; }
    void erase(uint32_t base, uint32_t len) {
        std::lock_guard<std::mutex> lk(m_);
        for (uint32_t i = 0; i < len && base + i < blob_.data_.size(); ++i) blob_.data_[base + i] = 0xFF;
        blob_.persist();
    }
    std::mutex m_;
    FileBlob blob_;
};

} // namespace Sim
