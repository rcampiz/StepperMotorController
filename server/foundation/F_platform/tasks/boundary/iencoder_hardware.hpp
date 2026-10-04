/**
 * @file boundary/iencoderhardware.hpp
 * @brief Consumer port — Scheduler side of the Drivers::IEncoderHardware harness connection
 *
 * Structurally identical to Drivers::IEncoderHardware.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Scheduler {

class IEncoderHardware {
public:
    virtual ~IEncoderHardware() = default;

    virtual bool isReady() const = 0;
    virtual int32_t getCount() const = 0;
    virtual void reset() = 0;

    // Index pulse
    virtual bool isIndexSeen() const = 0;
    virtual void clearIndexFlag() = 0;
    virtual uint32_t getIndexTick() const = 0;
    virtual int32_t getRevolutions() const = 0;
    virtual uint32_t getIndexPeriodUs() const = 0;

    /** @brief Called from EXTI ISR when index pulse detected */
    virtual void indexISR(uint32_t tickMs, uint32_t tickUs) = 0;
};

} // namespace Scheduler
