/**
 * @file usb_comm_task.hpp
 * @brief USB communication task for command/response interface
 *
 * Provides a command-line interface via USB CDC (ST-LINK VCP).
 * Supports querying system status and sending control commands.
 *
 * Available commands:
 *   help        - Display available commands
 *   status      - Show current system status
 *   adc         - Read current ADC value
 *   speed       - Read current pattern speed (ms delay)
 *   direction   - Show current direction (forward/reverse)
 *   invert      - Toggle direction inversion
 *   pattern <n> - Set pattern index (0-7)
 *
 * Communication protocol:
 *   - 115200 baud, 8N1
 *   - Commands terminated by newline (\n or \r\n)
 *   - Responses end with newline
 *   - Echo is disabled by default
 */

#ifndef USB_COMM_TASK_HPP
#define USB_COMM_TASK_HPP

#include "FreeRTOS.h"
#include "task.h"

/**
 * @brief USB communication task entry point
 * @param pvParameters Task parameters (unused)
 */
void vUSBCommTask(void *pvParameters);

#endif // USB_COMM_TASK_HPP