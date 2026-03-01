/**
 * @file imotion_dispatcher.hpp
 * @brief Interface for motion commands (MOT namespace)
 */

#pragma once

#include "F_platform/interfaces/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class IMotionDispatcher {
public:
    virtual ~IMotionDispatcher() = default;

    virtual DispatchResult motionRun(uint32_t stepsPerSec, bool forward) = 0;
    virtual DispatchResult motionMove(int32_t steps, uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionGoTo(int32_t position, uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionStop(bool hard) = 0;
    virtual DispatchResult motionEnable() = 0;
    virtual DispatchResult motionDisable() = 0;
    virtual DispatchResult motionHome(uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionZero() = 0;
};

} // namespace Comms
