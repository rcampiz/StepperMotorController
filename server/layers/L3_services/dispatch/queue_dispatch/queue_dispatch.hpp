/**
 * @file queue_dispatch.hpp
 * @brief L3 implementation of IQueueDispatcher
 */

#pragma once

#include "harness/pins/iqueue_dispatcher.hpp"

namespace Services {

class QueueDispatch : public Harness::IQueueDispatcher {
public:
    Harness::ServiceStatus queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) override;
    Harness::ServiceStatus queueMove(int32_t signedSteps) override;
    Harness::ServiceStatus queueGoTo(int32_t position) override;
    Harness::ServiceStatus queueRun(uint32_t speedRaw, int32_t direction) override;
    Harness::ServiceStatus queueStop(bool hard) override;
    Harness::ServiceStatus queueHome() override;
    Harness::ServiceStatus queueZero() override;
    Harness::ServiceStatus queueArm() override;
    Harness::ServiceStatus queueStart() override;
    Harness::ServiceStatus queueStartAt(uint32_t targetTick) override;
    Harness::ServiceStatus queueClear() override;
    uint8_t getControllerState() override;
    uint32_t getQueueDepth() override;
};

} // namespace Services
