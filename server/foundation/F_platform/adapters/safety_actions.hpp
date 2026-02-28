/**
 * @file safety_actions.hpp
 * @brief Foundation implementation of ISafetyActions
 *
 * Delegates to MotorTask and CommsTask APIs.
 */

#pragma once

#include "L3_services/safety/isafety_actions.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "F_platform/tasks/comms_task.hpp"

class SafetyActionsImpl : public Services::ISafetyActions {
public:
    uint16_t getMotorStatusReg() override {
        Tasks::MotorDebugInfo info;
        if (Tasks::MotorTask_GetDebugInfo(info)) {
            return info.status;
        }
        return 0;
    }

    void clearCommsTimeout() override {
        Tasks::CommsTask_ClearCommsTimeout();
    }

    uint32_t setHeartbeatTimeout(uint32_t ms) override {
        return Tasks::CommsTask_SetHeartbeatTimeout(ms);
    }

    void heartbeatReceived(uint32_t seq) override {
        Tasks::CommsTask_HeartbeatReceived(seq);
    }

    void getHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                             uint32_t& lastSeq, uint32_t& remainingMs,
                             bool& timedOut) override {
        Tasks::CommsTask_GetHeartbeatStatus(
            enabled, timeoutMs, lastSeq, remainingMs, timedOut);
    }
};
