/**
 * @file motor_command_sink.hpp
 * @brief FreeRTOS queue adapter for IMotorCommandSink
 *
 * Wraps xQueueSendToBack to implement the motor command sink interface.
 * Created by main.cpp after MotorTask_Init creates the queue.
 */

#pragma once

#include "L3_services/dispatch/imotor_command_sink.hpp"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/queue.h"

class MotorCommandSinkImpl : public Services::IMotorCommandSink {
public:
    explicit MotorCommandSinkImpl(QueueHandle_t queue) : m_queue(queue) {}

    bool sendCommand(const Services::MotorCommand& cmd, uint32_t timeoutMs = 0) override {
        if (m_queue == nullptr) return false;
        TickType_t ticks = (timeoutMs >= 0xFFFFFFFF / portTICK_PERIOD_MS)
                             ? portMAX_DELAY
                             : pdMS_TO_TICKS(timeoutMs);
        return xQueueSendToBack(m_queue, &cmd, ticks) == pdTRUE;
    }

private:
    QueueHandle_t m_queue;
};
