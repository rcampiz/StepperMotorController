/**
 * @file led_pattern_task.hpp
 * @brief LED pattern task for FreeRTOS
 *
 * Controls shift register based on ADC input. Direction and speed vary
 * based on potentiometer position. Supports inversion mode.
 */

#ifndef LED_PATTERN_TASK_HPP
#define LED_PATTERN_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"
#include "adc.hpp"
#include "sn74hc595.hpp"

// LED patterns
static const uint8_t patterns[] = {0b10000000, 0b01000000, 0b00100000,
                                   0b00010000, 0b00001000, 0b00000100,
                                   0b00000010, 0b00000001};
static const uint8_t numPatterns = sizeof(patterns) / sizeof(patterns[0]);

// ADC constants for 3.1V potentiometer range
static const uint16_t ADC_MIDPOINT = 1922;  // 1.55V midpoint
static const uint16_t ADC_DEADZONE = 154;   // 7.5% of 2048 (half range) = deadzone width
static const uint32_t MIN_DELAY_MS = 1;     // Fastest
static const uint32_t MAX_DELAY_MS = 500;   // Slowest

/**
 * @brief LED pattern task - controls shift register based on ADC
 *
 * Reads potentiometer value and updates LED pattern accordingly.
 * Direction and speed vary based on potentiometer position.
 * When inverted flag is set, both direction and polarity are flipped.
 *
 * @param pvParameters Task parameters (expects struct containing pointers)
 */
void vLEDPatternTask(void *pvParameters);

#endif // LED_PATTERN_TASK_HPP