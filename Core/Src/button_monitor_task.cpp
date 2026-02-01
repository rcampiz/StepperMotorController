/**
 * @file button_monitor_task.cpp
 * @brief Button monitor task implementation
 */

#include "button_monitor_task.hpp"
#include "led.hpp"

// External global objects and state
extern GPIO *buttonD8;
extern LED *buttonLED;
extern volatile bool inverted;

void vButtonMonitorTask(void *pvParameters) {
  (void)pvParameters;

  enum ButtonState {
    IDLE,            // Waiting for button press
    DEBOUNCE_PRESS,  // Debouncing press
    PRESSED,         // Button confirmed pressed, waiting for release
    DEBOUNCE_RELEASE // Debouncing release
  };

  ButtonState state = IDLE;
  uint32_t debounceCounter = 0;
  const uint32_t DEBOUNCE_DELAY = 50; // 50ms debounce time

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (true) {
    // Read button state (active high with pull-down)
    bool buttonPressed = buttonD8->read(); // true = pressed, false = released

    switch (state) {
    case IDLE:
      if (buttonPressed) {
        // Button pressed - start debouncing and turn on LED
        buttonLED->on();
        state = DEBOUNCE_PRESS;
        debounceCounter = 0;
      }
      break;

    case DEBOUNCE_PRESS:
      if (buttonPressed) {
        debounceCounter++;
        if (debounceCounter >= DEBOUNCE_DELAY) {
          // Button press confirmed - toggle inverted flag
          taskENTER_CRITICAL();
          inverted = !inverted;
          taskEXIT_CRITICAL();

          state = PRESSED;
          debounceCounter = 0;
        }
      } else {
        // Button released before debounce complete - false trigger
        buttonLED->off();
        state = IDLE;
        debounceCounter = 0;
      }
      break;

    case PRESSED:
      if (!buttonPressed) {
        // Button released - start debouncing release
        state = DEBOUNCE_RELEASE;
        debounceCounter = 0;
      }
      // Ignore button while held - prevents retriggering
      break;

    case DEBOUNCE_RELEASE:
      if (!buttonPressed) {
        debounceCounter++;
        if (debounceCounter >= DEBOUNCE_DELAY) {
          // Release confirmed - turn off LED and ready for next press
          buttonLED->off();
          state = IDLE;
          debounceCounter = 0;
        }
      } else {
        // Button pressed again before debounce complete
        state = PRESSED;
        debounceCounter = 0;
      }
      break;
    }

    // Check button every 1ms
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1));
  }
}