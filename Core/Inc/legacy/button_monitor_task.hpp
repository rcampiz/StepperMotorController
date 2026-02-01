/**
 * @file button_monitor_task.hpp
 * @brief Button monitor task for FreeRTOS
 *
 * Monitors D8 button with proper debouncing to toggle pattern direction
 * and potentiometer polarity.
 */

#ifndef BUTTON_MONITOR_TASK_HPP
#define BUTTON_MONITOR_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include "gpio.hpp"

/**
 * @brief Button monitor task - detects D8 button presses
 *
 * Monitors D8 (PA9) button with proper debouncing and edge detection.
 * When pressed, toggles the inverted flag which:
 *   - Inverts pattern direction
 *   - Flips potentiometer polarity (min becomes max, max becomes min)
 *
 * Visual feedback:
 *   - D12 LED (PA6) illuminates for duration of button press recognition
 *
 * Debouncing algorithm:
 *   1. Detects rising edge (press) - D12 LED turns on
 *   2. Waits for stable high state (debounce period)
 *   3. Triggers action once
 *   4. Waits for falling edge (release)
 *   5. Waits for stable low state before accepting next press - D12 LED turns off
 *
 * @param pvParameters Task parameters (expects pointer to GPIO object)
 */
void vButtonMonitorTask(void *pvParameters);

#endif // BUTTON_MONITOR_TASK_HPP