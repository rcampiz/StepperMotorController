/**
 * @file imotion_dispatcher.hpp
 * @brief Interface for motion commands (MOT namespace)
 */

#pragma once

#include "F_platform/types/dispatch_result.hpp"
#include <stdint.h>

namespace Comms {

class IMotionDispatcher {
public:
    virtual ~IMotionDispatcher() = default;

    virtual ServiceStatus motionRun(uint32_t stepsPerSec, bool forward) = 0;
    virtual ServiceStatus motionMove(int32_t steps, uint32_t maxSpeedSps) = 0;
    virtual ServiceStatus motionGoTo(int32_t position, uint32_t maxSpeedSps) = 0;
    virtual ServiceStatus motionStop(bool hard) = 0;
    virtual ServiceStatus motionEnable() = 0;
    virtual ServiceStatus motionDisable() = 0;
    virtual ServiceStatus motionHome(uint32_t maxSpeedSps) = 0;
    virtual ServiceStatus motionZero() = 0;
};

} // namespace Comms
