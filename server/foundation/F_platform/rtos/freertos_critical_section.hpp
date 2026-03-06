/**
 * @file freertos_critical_section.hpp
 * @brief FreeRTOS implementation of ICriticalSection
 */

#pragma once

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"
#include "harness/pins/icritical_section.hpp"

namespace Platform {

class FreeRTOSCriticalSection : public Harness::ICriticalSection {
public:
    void enter() override { taskENTER_CRITICAL(); }
    void exit() override { taskEXIT_CRITICAL(); }
};

} // namespace Platform
