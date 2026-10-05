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
    int32_t  velocity;
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
    virtual int32_t getCount() = 0;
    virtual void resetCount() = 0;
    virtual void clearIndexFlag() = 0;
    virtual bool isAvailable() = 0;
};

} // namespace Services
