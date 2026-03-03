/**
 * @file sd_motion.cpp
 * @brief ServiceDispatcher — IMotionDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/motion/motion_service.hpp"

static Comms::ServiceStatus mapMotionResult(Services::Motion::Result r) {
    switch (r) {
        case Services::Motion::Result::OK:            return Comms::ServiceStatus::Ok;
        case Services::Motion::Result::FAULT:         return Comms::ServiceStatus::Fault;
        case Services::Motion::Result::QUEUE_FULL:    return Comms::ServiceStatus::QueueFull;
        case Services::Motion::Result::INVALID_PARAM: return Comms::ServiceStatus::InvalidParam;
        default:                                      return Comms::ServiceStatus::InvalidParam;
    }
}

Comms::ServiceStatus ServiceDispatcher::motionRun(uint32_t stepsPerSec, bool forward) {
    return mapMotionResult(Services::Motion::run(stepsPerSec, forward));
}

Comms::ServiceStatus ServiceDispatcher::motionMove(int32_t steps, uint32_t maxSpeedSps) {
    return mapMotionResult(Services::Motion::move(steps, maxSpeedSps));
}

Comms::ServiceStatus ServiceDispatcher::motionGoTo(int32_t position, uint32_t maxSpeedSps) {
    return mapMotionResult(Services::Motion::goTo(position, maxSpeedSps));
}

Comms::ServiceStatus ServiceDispatcher::motionStop(bool hard) {
    return mapMotionResult(Services::Motion::stop(hard));
}

Comms::ServiceStatus ServiceDispatcher::motionEnable() {
    return mapMotionResult(Services::Motion::enable());
}

Comms::ServiceStatus ServiceDispatcher::motionDisable() {
    return mapMotionResult(Services::Motion::disable());
}

Comms::ServiceStatus ServiceDispatcher::motionHome(uint32_t maxSpeedSps) {
    return mapMotionResult(Services::Motion::home(maxSpeedSps));
}

Comms::ServiceStatus ServiceDispatcher::motionZero() {
    return mapMotionResult(Services::Motion::zero());
}
