/**
 * @file motion_dispatch.hpp
 * @brief L3 implementation of IMotionDispatcher
 *
 * Routes motion commands to Services::Motion namespace functions.
 * Result mapping: Services::Motion::Result → Harness::ServiceStatus.
 */

#pragma once

#include "L3_services/boundary/imotion_dispatcher.hpp"

namespace Services {

class MotionDispatch : public Services::IMotionDispatcher {
public:
    Harness::ServiceStatus motionRun(uint32_t stepsPerSec, bool forward) override;
    Harness::ServiceStatus motionMove(int32_t steps, uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionGoTo(int32_t position, uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionStop(bool hard) override;
    Harness::ServiceStatus motionEnable() override;
    Harness::ServiceStatus motionDisable() override;
    Harness::ServiceStatus motionHome(uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionZero() override;
};

} // namespace Services
