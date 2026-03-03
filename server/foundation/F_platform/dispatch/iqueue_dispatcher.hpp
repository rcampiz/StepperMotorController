/**
 * @file iqueue_dispatcher.hpp
 * @brief Interface for command queue / sync operations (SYNC namespace)
 */

#pragma once

#include "F_platform/types/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class IQueueDispatcher {
public:
    virtual ~IQueueDispatcher() = default;

    virtual ServiceStatus queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) = 0;
    virtual ServiceStatus queueMove(int32_t signedSteps) = 0;
    virtual ServiceStatus queueGoTo(int32_t position) = 0;
    virtual ServiceStatus queueRun(uint32_t speedRaw, int32_t direction) = 0;
    virtual ServiceStatus queueStop(bool hard) = 0;
    virtual ServiceStatus queueHome() = 0;
    virtual ServiceStatus queueZero() = 0;
    virtual ServiceStatus queueArm() = 0;
    virtual ServiceStatus queueStart() = 0;
    virtual ServiceStatus queueStartAt(uint32_t targetTick) = 0;
    virtual ServiceStatus queueClear() = 0;

    virtual uint8_t getControllerState() = 0;
    virtual uint32_t getQueueDepth() = 0;
};

} // namespace Comms
