/**
 * @file motion_dispatch.cpp
 * @brief L3 IMotionDispatcher implementation — delegates to Services::Motion
 */

#include "motion_dispatch.hpp"
#include "L3_services/motion/motion_service/motion_service.hpp"

namespace Services {

static Harness::ServiceStatus mapMotionResult(Motion::Result r) {
    switch (r) {
        case Motion::Result::OK:            return Harness::ServiceStatus::Ok;
        case Motion::Result::FAULT:         return Harness::ServiceStatus::Fault;
        case Motion::Result::QUEUE_FULL:    return Harness::ServiceStatus::QueueFull;
        case Motion::Result::INVALID_PARAM: return Harness::ServiceStatus::InvalidParam;
        default:                            return Harness::ServiceStatus::InvalidParam;
    }
}

Harness::ServiceStatus MotionDispatch::motionRun(uint32_t stepsPerSec, bool forward) {
    return mapMotionResult(Motion::run(stepsPerSec, forward));
}

Harness::ServiceStatus MotionDispatch::motionMove(int32_t steps, uint32_t maxSpeedSps) {
    return mapMotionResult(Motion::move(steps, maxSpeedSps));
}

Harness::ServiceStatus MotionDispatch::motionGoTo(int32_t position, uint32_t maxSpeedSps) {
    return mapMotionResult(Motion::goTo(position, maxSpeedSps));
}

Harness::ServiceStatus MotionDispatch::motionStop(bool hard) {
    return mapMotionResult(Motion::stop(hard));
}

Harness::ServiceStatus MotionDispatch::motionEnable() {
    return mapMotionResult(Motion::enable());
}

Harness::ServiceStatus MotionDispatch::motionDisable() {
    return mapMotionResult(Motion::disable());
}

Harness::ServiceStatus MotionDispatch::motionHome(uint32_t maxSpeedSps) {
    return mapMotionResult(Motion::home(maxSpeedSps));
}

Harness::ServiceStatus MotionDispatch::motionZero() {
    return mapMotionResult(Motion::zero());
}

} // namespace Services
