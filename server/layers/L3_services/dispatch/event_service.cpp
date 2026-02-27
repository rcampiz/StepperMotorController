/**
 * @file event_service.cpp
 * @brief Event service — FreeRTOS queue, enable mask, counters
 *
 * See docs/PROTOCOL_EVENTS_V1.md for contract.
 */

#include "L3_services/dispatch/event_service.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/queue.h"

namespace Services::Event {

static constexpr uint8_t QUEUE_DEPTH = 8;

static QueueHandle_t s_queue = nullptr;
static uint8_t  s_enableMask   = 0;  // disabled at boot
static uint32_t s_sentCount    = 0;
static uint32_t s_lostCritical = 0;  // dropped FAULT/STALL/CLEAR
static uint32_t s_lostInfo     = 0;  // dropped MOTION_DONE

void init() {
    s_queue = xQueueCreate(QUEUE_DEPTH, sizeof(AsyncEvent));
    s_enableMask = 0;
    s_sentCount = 0;
    s_lostCritical = 0;
    s_lostInfo = 0;
}

void enable(uint8_t mask, uint16_t currentStatusReg) {
    s_enableMask = mask & EVT_MASK_ALL;

    // Snapshot-on-enable: emit synthetic events for any currently active conditions
    // so the client gets immediate awareness of existing faults/stalls.
    if (s_enableMask == 0) {
        return;
    }

    // Check fault conditions (active-low bits)
    bool hasFault = !(currentStatusReg & (1 << 13))   // OCD
                 || !(currentStatusReg & (1 << 9))     // UVLO
                 || ((currentStatusReg >> 11) & 0x03) >= 2;  // TH_SD

    if (hasFault) {
        post(EventType::FAULT, currentStatusReg);
    }

    // Check stall conditions (active-low bits)
    bool hasStall = !(currentStatusReg & (1 << 14))    // STALL_A
                 || !(currentStatusReg & (1 << 15));    // STALL_B

    if (hasStall) {
        post(EventType::STALL, currentStatusReg);
    }
}

void disable() {
    s_enableMask = 0;
}

uint8_t getMask() {
    return s_enableMask;
}

Stats getStats() {
    Stats s;
    s.sent = s_sentCount;
    s.lostCritical = s_lostCritical;
    s.lostInfo = s_lostInfo;
    s.enableMask = s_enableMask;
    s.queueDepth = (s_queue != nullptr)
        ? static_cast<uint8_t>(QUEUE_DEPTH - uxQueueSpacesAvailable(s_queue))
        : 0;
    return s;
}

bool post(EventType type, uint16_t statusReg) {
    if (s_queue == nullptr) {
        return false;
    }

    // Check if this event type is enabled
    uint8_t bit = eventTypeToMaskBit(type);
    if (!(s_enableMask & bit)) {
        return false;
    }

    bool critical = eventTypeIsCritical(type);

    // Reserved-slot policy: informational events only enqueued when depth < threshold
    if (!critical) {
        UBaseType_t spaces = uxQueueSpacesAvailable(s_queue);
        if (spaces <= (QUEUE_DEPTH - EVT_RESERVED_SLOT_THRESHOLD)) {
            s_lostInfo++;
            return false;
        }
    }

    AsyncEvent evt;
    evt.type = type;
    evt.statusReg = statusReg;

    if (xQueueSend(s_queue, &evt, 0) == pdTRUE) {
        s_sentCount++;
        return true;
    }

    // Queue full — drop newest, increment appropriate loss counter
    if (critical) {
        s_lostCritical++;
    } else {
        s_lostInfo++;
    }
    return false;
}

bool receive(AsyncEvent& evt) {
    if (s_queue == nullptr) {
        return false;
    }
    return xQueueReceive(s_queue, &evt, 0) == pdTRUE;
}

} // namespace Services::Event
