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
#include "F_platform/interfaces/iclock.hpp"

class FreeRTOSClock : public IClock {
public:
    uint32_t getTickMs() const override {
        return xTaskGetTickCount() * portTICK_PERIOD_MS;
    }

    void delayMs(uint32_t ms) override {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
};

#endif // PLATFORM_FREERTOS_CLOCK_HPP
