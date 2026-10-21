/**
 * @file boundary/iencoder.hpp
 * @brief Position/velocity readout from the encoder service (L3-internal interface)
 */

#pragma once

#include "harness/pins/velocity_quality.hpp"
#include <stdint.h>

namespace Services {

/// Encoder state snapshot (mirrors Scheduler::EncoderSnapshot)
struct EncoderSnapshot {
    int64_t  count;
    int32_t  velocity;         // counts per second (signed), filtered
    bool     indexSeen;
    uint32_t indexTick;
    int32_t  revolutions;
    uint32_t indexPeriodUs;
    VelocityQuality velocityQuality;
};

/// Abstract encoder interface — consumer port for L3 services
class IEncoder {
public:
    virtual ~IEncoder() = default;

    virtual EncoderSnapshot getState() = 0;
    /// The 64-bit count at this instant: the last sampled count plus how far the hardware
    /// counter has moved since (getState().count is up to one sampling period old). Any task.
    virtual int64_t countNow() { return getState().count; }
    virtual int32_t getCount() = 0;
    virtual void resetCount() = 0;
    virtual void clearIndexFlag() = 0;
    virtual bool isAvailable() = 0;

    // --- position capture (CTRL:CAP:*): freeze the DMA rings, read them, resume ---
    struct CaptureInfo {
        uint32_t samples = 0;     ///< ring length (oldest first after a freeze)
        uint16_t rateHz = 0;      ///< sample rate the rings were filled at
        bool     sync = false;    ///< a commanded-step ring exists beside the encoder ring
    };
    virtual bool captureFreeze(CaptureInfo& info) { (void)info; return false; }
    /// Sample i (0 = oldest) of the frozen rings: encoder count and SYNC pulse count, 16-bit wrapped.
    virtual bool captureRead(uint32_t i, uint16_t& enc, uint16_t& sync) { (void)i; (void)enc; (void)sync; return false; }
    virtual void captureResume() {}
};

} // namespace Services
