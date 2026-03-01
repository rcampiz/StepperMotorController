/**
 * @file iqueue_dispatcher.hpp
 * @brief Interface for command queue / sync operations (SYNC namespace)
 */

#pragma once

#include "F_platform/interfaces/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class IQueueDispatcher {
public:
    virtual ~IQueueDispatcher() = default;

    virtual DispatchResult queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) = 0;
    virtual DispatchResult queueMove(int32_t signedSteps) = 0;
    virtual DispatchResult queueGoTo(int32_t position) = 0;
    virtual DispatchResult queueRun(uint32_t speedRaw, int32_t direction) = 0;
    virtual DispatchResult queueStop(bool hard) = 0;
    virtual DispatchResult queueHome() = 0;
    virtual DispatchResult queueZero() = 0;
    virtual DispatchResult queueArm() = 0;
    virtual DispatchResult queueStart() = 0;
    virtual DispatchResult queueStartAt(uint32_t targetTick) = 0;
    virtual DispatchResult queueClear() = 0;

    virtual uint8_t getControllerState() = 0;
    virtual uint32_t getQueueDepth() = 0;
    virtual const char* controllerStateString() = 0;
};

} // namespace Comms
