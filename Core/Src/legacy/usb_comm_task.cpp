/**
 * @file usb_comm_task.cpp
 * @brief USB communication task implementation
 */

#include "usb_comm_task.hpp"
#include "uart.hpp"
#include "adc.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <ctype.h>
#include <stdint.h>

// External global objects
extern UART *usb;
extern ADC *adc;
extern volatile bool inverted;

// Command buffer
#define CMD_BUFFER_SIZE 64
static char cmdBuffer[CMD_BUFFER_SIZE];
static uint8_t cmdIndex = 0;

/**
 * @brief Compare strings (case-insensitive)
 */
static bool strEqual(const char *str1, const char *str2) {
  while (*str1 && *str2) {
    if (tolower(*str1) != tolower(*str2)) {
      return false;
    }
    str1++;
    str2++;
  }
  return *str1 == *str2;
}

/**
 * @brief Print help message
 */
static void printHelp() {
  usb->println("\r\n=== LED Array Command Interface ===");
  usb->println("Available commands:");
  usb->println("  help        - Display this help message");
  usb->println("  status      - Show current system status");
  usb->println("  adc         - Read current ADC value");
  usb->println("  inverted    - Show if direction is inverted");
  usb->println("  invert      - Toggle direction inversion");
  usb->println("  toggle      - Toggle direction (same as invert)");
  usb->println("  uptime      - Show system uptime in ticks");
  usb->println("\r\n");
}

/**
 * @brief Print system status
 */
static void printStatus() {
  // Read current ADC value
  uint16_t adcValue = adc->read(0);
  uint32_t voltage = adc->readVoltage(0, 3100);  // 3.1V max

  // Read inverted flag
  bool isInverted;
  taskENTER_CRITICAL();
  isInverted = inverted;
  taskEXIT_CRITICAL();

  usb->println("\r\n=== System Status ===");

  usb->print("ADC Value:    ");
  usb->print(static_cast<uint32_t>(adcValue));
  usb->println(" / 4095");

  usb->print("Voltage:      ");
  usb->print(static_cast<uint32_t>(voltage));
  usb->println(" mV");

  usb->print("Direction:    ");
  usb->println(isInverted ? "Inverted (Reversed)" : "Normal (Forward)");

  usb->print("Uptime:       ");
  usb->print(xTaskGetTickCount());
  usb->println(" ticks");

  usb->println("\r\n");
}

/**
 * @brief Process received command
 */
static void processCommand(const char *cmd) {
  // Skip leading whitespace
  while (*cmd && isspace(*cmd)) {
    cmd++;
  }

  // Empty command
  if (!*cmd) {
    return;
  }

  // Extract command word (first token)
  char cmdWord[CMD_BUFFER_SIZE];
  int i = 0;
  while (*cmd && !isspace(*cmd) && i < CMD_BUFFER_SIZE - 1) {
    cmdWord[i++] = *cmd++;
  }
  cmdWord[i] = '\0';

  // Skip whitespace before arguments
  while (*cmd && isspace(*cmd)) {
    cmd++;
  }

  // Process command
  if (strEqual(cmdWord, "help") || strEqual(cmdWord, "?")) {
    printHelp();
  } else if (strEqual(cmdWord, "status")) {
    printStatus();
  } else if (strEqual(cmdWord, "adc")) {
    uint16_t adcValue = adc->read(0);
    usb->print("ADC: ");
    usb->print(static_cast<uint32_t>(adcValue));
    usb->println("");
  } else if (strEqual(cmdWord, "inverted")) {
    bool isInverted;
    taskENTER_CRITICAL();
    isInverted = inverted;
    taskEXIT_CRITICAL();
    usb->println(isInverted ? "Direction: Inverted" : "Direction: Normal");
  } else if (strEqual(cmdWord, "invert") || strEqual(cmdWord, "toggle")) {
    taskENTER_CRITICAL();
    inverted = !inverted;
    taskEXIT_CRITICAL();
    usb->println("Direction toggled");
  } else if (strEqual(cmdWord, "uptime")) {
    usb->print("Uptime: ");
    usb->print(xTaskGetTickCount());
    usb->println(" ticks");
  } else {
    usb->print("Unknown command: ");
    usb->println(cmdWord);
    usb->println("Type 'help' for available commands");
  }
}

/**
 * @brief USB communication task
 */
void vUSBCommTask(void *pvParameters) {
  (void)pvParameters;

  // Wait a bit for UART to initialize
  vTaskDelay(pdMS_TO_TICKS(100));

  // Print welcome message
  usb->println("\r\n");
  usb->println("========================================");
  usb->println("   LED Array Control System v1.0");
  usb->println("========================================");
  usb->println("Type 'help' for available commands");
  usb->println("");

  TickType_t lastWakeTime = xTaskGetTickCount();

  while (true) {
    // Check for incoming data
    if (usb->available()) {
      uint8_t rxByte;
      if (usb->receive(&rxByte, 10000)) {
        // Handle backspace
        if (rxByte == '\b' || rxByte == 127) {
          if (cmdIndex > 0) {
            cmdIndex--;
            usb->print("\b \b");  // Erase character on terminal
          }
        }
        // Handle newline (command terminator)
        else if (rxByte == '\r' || rxByte == '\n') {
          if (cmdIndex > 0) {
            usb->println("");  // Echo newline
            cmdBuffer[cmdIndex] = '\0';
            processCommand(cmdBuffer);
            cmdIndex = 0;  // Reset buffer
          }
        }
        // Handle printable characters
        else if (rxByte >= 32 && rxByte < 127) {
          if (cmdIndex < CMD_BUFFER_SIZE - 1) {
            cmdBuffer[cmdIndex++] = rxByte;
            usb->print((char)rxByte);  // Echo character
          }
        }
      }
    }

    // Small delay to prevent busy-waiting
    vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(10));
  }
}