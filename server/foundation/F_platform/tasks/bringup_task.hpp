/**
 * @file bringup_task.hpp
 * @brief Hardware bring-up and test task
 */

#ifndef BRINGUP_TASK_HPP
#define BRINGUP_TASK_HPP

#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

// Task configuration
#define BRINGUP_TASK_STACK_SIZE  512
#define BRINGUP_TASK_PRIORITY    (tskIDLE_PRIORITY + 2)

/**
 * @brief FreeRTOS task function for hardware bring-up
 * @param pvParameters Unused
 */
void bringupTask(void* pvParameters);

#endif // BRINGUP_TASK_HPP
