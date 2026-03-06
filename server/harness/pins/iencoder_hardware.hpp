/**
 * @file iencoder_hardware.hpp
 * @brief Abstract interface for quadrature encoder hardware
 *
 * Decouples encoder task from L4 Drivers::Encoder concrete type.
 * Implemented by Drivers::Encoder.
 */

#pragma once

#include <stdint.h>

namespace Harness {

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

} // namespace Harness
