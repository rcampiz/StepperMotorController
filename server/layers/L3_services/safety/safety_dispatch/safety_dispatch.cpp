/**
 * @file safety_dispatch.cpp
 * @brief L3 ISafetyDispatcher implementation
 */

#include "safety_dispatch.hpp"
#include "L3_services/safety/safety_service/safety_service.hpp"

namespace Services {

void SafetyDispatch::safetyEstop() {
    Safety::emergencyStop();
}

Harness::ServiceStatus SafetyDispatch::safetyClearFault(char* activeFaults, uint32_t bufSize) {
    auto r = Safety::clearFault(activeFaults, bufSize);
    switch (r) {
        case Safety::Result::OK:           return Harness::ServiceStatus::Ok;
        case Safety::Result::FAULT_ACTIVE: return Harness::ServiceStatus::FaultActive;
        default:                           return Harness::ServiceStatus::InvalidState;
    }
}

Harness::ServiceStatus SafetyDispatch::safetyForceClearFault() {
    auto r = Safety::forceClearFault();
    return r == Safety::Result::OK
        ? Harness::ServiceStatus::Ok
        : Harness::ServiceStatus::InvalidState;
}

uint32_t SafetyDispatch::safetySetHeartbeatTimeout(uint32_t ms) {
    return Safety::setHeartbeatTimeout(ms);
}

void SafetyDispatch::safetyHeartbeatReceived(uint32_t seq) {
    Safety::heartbeatReceived(seq);
}

Harness::ISafetyDispatcher::HeartbeatStatus SafetyDispatch::safetyGetHeartbeatStatus() {
    Safety::HeartbeatStatus hb;
    Safety::getHeartbeatStatus(hb);
    return {hb.enabled, hb.timeoutMs, hb.lastSeq, hb.remainingMs, hb.timedOut};
}

} // namespace Services
