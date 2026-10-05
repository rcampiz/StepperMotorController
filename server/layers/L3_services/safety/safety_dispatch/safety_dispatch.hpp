/**
 * @file safety_dispatch.hpp
 * @brief L3 implementation of ISafetyDispatcher
 */

#pragma once

#include "L3_services/boundary/isafety_dispatcher.hpp"

namespace Services {

class SafetyDispatch : public Services::ISafetyDispatcher {
public:
    void safetyEstop() override;
    Harness::ServiceStatus safetyClearFault(char* activeFaults, uint32_t bufSize) override;
    Harness::ServiceStatus safetyForceClearFault() override;
    uint32_t safetySetHeartbeatTimeout(uint32_t ms) override;
    void safetyHeartbeatReceived(uint32_t seq) override;
    HeartbeatStatus safetyGetHeartbeatStatus() override;
};

} // namespace Services
