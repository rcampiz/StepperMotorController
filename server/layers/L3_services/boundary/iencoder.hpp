/**
 * @file iencoder.hpp
 * @brief Consumer port — L3 side of the Scheduler::IEncoder harness connection
 *
 * This is what L3 services need from the encoder. The harness adapter
 * bridges Scheduler::IEncoder (tasks provider) to Services::IEncoder (this port).
 *
 * Structurally identical to Scheduler::IEncoder — the adapter is a
 * trivial forwarder. L3 code depends only on L3 types.
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
