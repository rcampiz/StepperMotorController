/**
 * @file motor_event_service.cpp
 * @brief Motor event service — queue, enable mask, counters
 *
 * See docs/PROTOCOL_EVENTS_V1.md for contract.
 */

#include "L3_services/motion/motor_event_service/motor_event_service.hpp"
#include "harness/pins/motor_state.hpp"

namespace Services {

MotorEventManager g_motorEvent;

void MotorEventManager::init(Platform::IQueue<AsyncEvent, 8>& queue) {
    m_queue = &queue;
    m_queue->reset();
    m_enableMask = 0;
    m_sentCount = 0;
    m_lostCritical = 0;
    m_lostInfo = 0;
}

void MotorEventManager::enable(uint8_t mask, uint8_t currentFaults, uint16_t currentStatusReg) {
    m_enableMask = mask & EVT_MASK_ALL;

    if (m_enableMask == 0) {
        return;
    }

    // Snapshot-on-enable: emit synthetic events for any currently active conditions
    if ((currentFaults & Harness::DriverFault::SHUTDOWN) != 0u) {
        post(EventType::FAULT, currentStatusReg, currentFaults);
    }
    if ((currentFaults & Harness::DriverFault::STALL) != 0u) {
        post(EventType::STALL, currentStatusReg, currentFaults);
    }
}

void MotorEventManager::disable() {
    m_enableMask = 0;
}

uint8_t MotorEventManager::getMask() const {
    return m_enableMask;
}

MotorEventStats MotorEventManager::getStats() const {
    MotorEventStats s;
    s.sent = m_sentCount;
    s.lostCritical = m_lostCritical;
    s.lostInfo = m_lostInfo;
    s.enableMask = m_enableMask;
    s.queueDepth = (m_queue != nullptr)
        ? static_cast<uint8_t>(m_queue->available())
        : 0;
    return s;
}

bool MotorEventManager::post(EventType type, uint16_t statusReg, uint8_t faults) {
    if (m_queue == nullptr) {
        return false;
    }

    uint8_t bit = eventTypeToMaskBit(type);
    if (!eventTypeAlwaysDelivered(type) && !(m_enableMask & bit)) {
        return false;
    }

    bool critical = eventTypeIsCritical(type);

    // Reserved-slot policy: informational events only enqueued when depth < threshold
    if (!critical) {
        size_t spaces = m_queue->freeSlots();
        if (spaces <= (QUEUE_DEPTH - EVT_RESERVED_SLOT_THRESHOLD)) {
            m_lostInfo++;
            return false;
        }
    }

    AsyncEvent evt;
    evt.type = type;
    evt.statusReg = statusReg;
    evt.faults = faults;

    if (m_queue->send(evt, 0)) {
        m_sentCount++;
        return true;
    }

    if (critical) {
        m_lostCritical++;
    } else {
        m_lostInfo++;
    }
    return false;
}

bool MotorEventManager::receive(AsyncEvent& evt) {
    if (m_queue == nullptr) {
        return false;
    }
    return m_queue->receive(evt, 0);
}

} // namespace Services
