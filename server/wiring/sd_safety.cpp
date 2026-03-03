/**
 * @file sd_safety.cpp
 * @brief ServiceDispatcher — ISafetyDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/safety/safety_service.hpp"

void ServiceDispatcher::safetyEstop() {
    Services::Safety::emergencyStop();
}

Comms::ServiceStatus ServiceDispatcher::safetyClearFault(char* activeFaults, uint32_t bufSize) {
    auto r = Services::Safety::clearFault(activeFaults, bufSize);
    switch (r) {
        case Services::Safety::Result::OK:           return Comms::ServiceStatus::Ok;
        case Services::Safety::Result::FAULT_ACTIVE: return Comms::ServiceStatus::FaultActive;
        default:                                     return Comms::ServiceStatus::InvalidState;
    }
}

Comms::ServiceStatus ServiceDispatcher::safetyForceClearFault() {
    auto r = Services::Safety::forceClearFault();
    return r == Services::Safety::Result::OK
        ? Comms::ServiceStatus::Ok
        : Comms::ServiceStatus::InvalidState;
}

uint32_t ServiceDispatcher::safetySetHeartbeatTimeout(uint32_t ms) {
    return Services::Safety::setHeartbeatTimeout(ms);
}

void ServiceDispatcher::safetyHeartbeatReceived(uint32_t seq) {
    Services::Safety::heartbeatReceived(seq);
}

Comms::ISafetyDispatcher::HeartbeatStatus ServiceDispatcher::safetyGetHeartbeatStatus() {
    Services::Safety::HeartbeatStatus hb;
    Services::Safety::getHeartbeatStatus(hb);
    return {hb.enabled, hb.timeoutMs, hb.lastSeq, hb.remainingMs, hb.timedOut};
}
