/**
 * @file freertos_clock.hpp
 * @brief FreeRTOS implementation of IClock
 *
 * This file is the ONLY place FreeRTOS tick/delay primitives should
 * appear for task-level timing. All other code accesses timing
 * through the IClock interface.
 */

#ifndef PLATFORM_FREERTOS_CLOCK_HPP
#define PLATFORM_FREERTOS_CLOCK_HPP

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include "harness/pins/iclock.hpp"

namespace Platform {

class FreeRTOSClock : public Harness::IClock {
public:
    uint32_t getTickMs() const override {
        return xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    uint32_t getTickMsFromISR() const override {
        return xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
    }

    void delayMs(uint32_t ms) override {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void sleepUntilMs(uint32_t& lastWakeMs, uint32_t periodMs) override {
        TickType_t lastWakeTick = pdMS_TO_TICKS(lastWakeMs);
        vTaskDelayUntil(&lastWakeTick, pdMS_TO_TICKS(periodMs));
        lastWakeMs = static_cast<uint32_t>(lastWakeTick * portTICK_PERIOD_MS);
    }
};

} // namespace Platform

#endif // PLATFORM_FREERTOS_CLOCK_HPP
