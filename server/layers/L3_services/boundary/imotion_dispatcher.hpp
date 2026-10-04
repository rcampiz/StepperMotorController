/**
 * @file imotion_dispatcher.hpp
 * @brief Interface for motion commands (MOT namespace)
 */

#pragma once

#include "harness/pins/dispatch_result.hpp"
#include <stdint.h>

namespace Services {

class IMotionDispatcher {
public:
    virtual ~IMotionDispatcher() = default;

    virtual Harness::ServiceStatus motionRun(uint32_t stepsPerSec, bool forward) = 0;
    virtual Harness::ServiceStatus motionMove(int32_t steps, uint32_t maxSpeedSps) = 0;
    virtual Harness::ServiceStatus motionGoTo(int32_t position, uint32_t maxSpeedSps) = 0;
    virtual Harness::ServiceStatus motionStop(bool hard) = 0;
    virtual Harness::ServiceStatus motionEnable() = 0;
    virtual Harness::ServiceStatus motionDisable() = 0;
    virtual Harness::ServiceStatus motionHome(uint32_t maxSpeedSps) = 0;
    virtual Harness::ServiceStatus motionZero() = 0;
};

} // namespace Services
