/**
 * @file led_pattern_task.cpp
 * @brief LED pattern task implementation
 */

#include "led_pattern_task.hpp"

// External global objects and state
extern SN74HC595 *shiftReg;
extern ADC *adc;
extern volatile bool inverted;

void vLEDPatternTask(void *pvParameters) {
  (void)pvParameters;

  int8_t currentPattern = 0;
  uint32_t lastUpdateTime = 0;
  TickType_t lastWakeTime = xTaskGetTickCount();

  while (true) {
    // Read ADC value
    uint16_t adcValue = adc->read(0);

    // Read inverted flag atomically
    bool isInverted;
    taskENTER_CRITICAL();
    isInverted = inverted;
    taskEXIT_CRITICAL();

    // Calculate distance from midpoint and direction
    uint16_t distanceFromMidpoint;
    int8_t direction;
    bool inDeadzone = false;

    if (adcValue < ADC_MIDPOINT) {
      distanceFromMidpoint = ADC_MIDPOINT - adcValue;
      direction = 1; // Forward
    } else {
      distanceFromMidpoint = adcValue - ADC_MIDPOINT;
      direction = -1; // Reverse
    }

    // Check if in deadzone (7.5% around midpoint)
    if (distanceFromMidpoint < ADC_DEADZONE) {
      inDeadzone = true;
    }

    // Invert direction if flag is set (this reverses the sweep direction)
    if (isInverted) {
      direction = -direction;
    }

    // Map distance to delay (500ms at midpoint to 1ms at extremes)
    uint32_t updateDelay =
        MAX_DELAY_MS -
        ((distanceFromMidpoint * (MAX_DELAY_MS - MIN_DELAY_MS)) / 2048);

    // Write current pattern to shift register
    shiftReg->writePattern(patterns[currentPattern]);

    // Update pattern when delay elapsed (skip if in deadzone)
    if (!inDeadzone) {
      lastUpdateTime++;
      if (lastUpdateTime >= updateDelay) {
        lastUpdateTime = 0;
        currentPattern += direction;

        // Handle wraparound
        if (currentPattern >= numPatterns) {
          currentPattern = 0;
        } else if (currentPattern < 0) {
          currentPattern = numPatterns - 1;
        }
      }
    } else {
      // Reset update counter when in deadzone
      lastUpdateTime = 0;
    }

    // Delay for 1ms
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1));
  }
}