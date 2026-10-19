/**
 * @file iqueue_dispatcher.hpp
 * @brief Interface for command queue / sync operations (SYNC namespace)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Services {

class IQueueDispatcher {
public:
    virtual ~IQueueDispatcher() = default;

    virtual Harness::ServiceStatus queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) = 0;
    virtual Harness::ServiceStatus queueMove(int32_t signedSteps) = 0;
    virtual Harness::ServiceStatus queueGoTo(int32_t position) = 0;
    /// Queued RUN: speed in full steps/s, direction 0 = reverse, 1 = forward
    virtual Harness::ServiceStatus queueRun(uint32_t stepsPerSec, int32_t direction) = 0;
    virtual Harness::ServiceStatus queueStop(bool hard) = 0;
    virtual Harness::ServiceStatus queueHome() = 0;
    virtual Harness::ServiceStatus queueZero() = 0;
    virtual Harness::ServiceStatus queueArm() = 0;
    virtual Harness::ServiceStatus queueStart() = 0;
    virtual Harness::ServiceStatus queueStartAt(uint32_t targetTick) = 0;
    virtual Harness::ServiceStatus queueClear() = 0;

    virtual uint8_t getControllerState() = 0;
    virtual uint32_t getQueueDepth() = 0;
};

} // namespace Services
