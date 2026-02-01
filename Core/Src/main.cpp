/**
 * @file main.cpp
 * @brief Main application for NUCLEO-F401RE with FreeRTOS
 *
 * Demonstrates FreeRTOS multitasking with shift register control.
 * Four tasks run concurrently:
 *   1. LED pattern task - Updates shift register based on ADC input
 *   2. Heartbeat task - Blinks onboard LED to show system is running
 *   3. Button monitor task - Toggles direction/polarity when D8 is pressed
 *   4. USB communication task - Command/response interface via ST-LINK VCP
 *
 * Direction and speed controlled by analog input on A0 (PA0):
 *   - Minimum voltage (0V): Forward direction, 1ms update (fastest)
 *   - Near midpoint (~1.55V): Deadzone - no pattern change
 *   - Maximum voltage (3.1V): Reverse direction, 1ms update (fastest)
 *
 * Button on D8 (PA9):
 *   - Press to toggle: sweep direction reversed (left-to-right ↔ right-to-left)
 *   - D12 LED illuminates during button press recognition
 *
 * USB Commands (115200 baud):
 *   - help      : Display available commands
 *   - status    : Show system status
 *   - adc       : Read ADC value
 *   - invert    : Toggle direction
 *
 * All pin assignments are centralized in board_config.hpp
 */

#include "FreeRTOS.h"
#include "task.h"

#include "adc.hpp"
#include "board_config.hpp"
#include "gpio.hpp"
#include "led.hpp"
#include "sn74hc595.hpp"
#include "uart.hpp"

#include "button_monitor_task.hpp"
#include "heartbeat_task.hpp"
#include "led_pattern_task.hpp"
#include "usb_comm_task.hpp"

// Global objects (initialized in main)
SN74HC595 *shiftReg = nullptr;
ADC *adc = nullptr;
LED *userLED = nullptr;
LED *buttonLED = nullptr;
GPIO *buttonD8 = nullptr;
UART *usb = nullptr;

// Shared state variables (protected by FreeRTOS critical sections)
volatile bool inverted = false; // Toggle sweep direction

int main() {
  // Initialize hardware using pin definitions from board_config.hpp
  using namespace BoardConfig;

  userLED = new LED(LEDPins::USER_PORT, LEDPins::USER_PIN);
  buttonLED =
      new LED(LEDPins::BUTTON_FEEDBACK_PORT, LEDPins::BUTTON_FEEDBACK_PIN);

  shiftReg = new SN74HC595(
      ShiftRegister::SER_PORT, ShiftRegister::SER_PIN, ShiftRegister::NOE_PORT,
      ShiftRegister::NOE_PIN, ShiftRegister::RCLK_PORT, ShiftRegister::RCLK_PIN,
      ShiftRegister::SRCLK_PORT, ShiftRegister::SRCLK_PIN,
      ShiftRegister::NSRCLR_PORT, ShiftRegister::NSRCLR_PIN);

  adc = new ADC(ADC1);
  adc->init(ADCPrescaler::Div4, ADCSampleTime::Cycles56);
  adc->configurePin(ADCPins::A0_PORT, ADCPins::A0_PIN);

  // Initialize button on D8 as input with pull-down
  buttonD8 = new GPIO(ButtonPins::D8_PORT, ButtonPins::D8_PIN);
  buttonD8->setMode(GPIOMode::Input);
  buttonD8->setPull(GPIOPull::PullDown);

  // Initialize USB UART (USART2 on ST-LINK VCP)
  usb = new UART(USART2, 42000000); // APB1 clock = 42MHz
  usb->init(UARTBaudRate::Baud115200);

  // Create FreeRTOS tasks
  xTaskCreate(vLEDPatternTask,      // Task function
              "LEDPattern",         // Task name
              256,                  // Stack size (words)
              NULL,                 // Parameters
              tskIDLE_PRIORITY + 2, // Priority
              NULL                  // Task handle
  );

  xTaskCreate(vHeartbeatTask,       // Task function
              "Heartbeat",          // Task name
              128,                  // Stack size (words)
              NULL,                 // Parameters
              tskIDLE_PRIORITY + 1, // Priority
              NULL                  // Task handle
  );

  xTaskCreate(vButtonMonitorTask,   // Task function
              "ButtonMonitor",      // Task name
              128,                  // Stack size (words)
              NULL,                 // Parameters
              tskIDLE_PRIORITY + 2, // Priority (same as LED task)
              NULL                  // Task handle
  );

  xTaskCreate(vUSBCommTask,         // Task function
              "USBComm",            // Task name
              512,                  // Stack size (words) - larger for buffers
              NULL,                 // Parameters
              tskIDLE_PRIORITY + 3, // Priority (higher for responsiveness)
              NULL                  // Task handle
  );

  // Start the FreeRTOS scheduler
  vTaskStartScheduler();

  // Should never reach here
  while (1) {
    // Error: scheduler failed to start
  }

  return 0;
}

// FreeRTOS application hook stubs
extern "C" {
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
  (void)xTask;
  (void)pcTaskName;
  // Stack overflow detected - halt
  while (1)
    ;
}

void vApplicationMallocFailedHook(void) {
  // Memory allocation failed - halt
  while (1)
    ;
}
}