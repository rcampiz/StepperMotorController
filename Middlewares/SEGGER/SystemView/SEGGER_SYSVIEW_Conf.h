/**
 * @file SEGGER_SYSVIEW_Conf.h
 * @brief SEGGER SystemView configuration for STM32F401RE
 *
 * This file configures SystemView for use with FreeRTOS on Cortex-M4.
 *
 * IMPORTANT: This is a placeholder. After downloading SEGGER SystemView,
 * you may need to merge this with the official SEGGER_SYSVIEW_Conf.h template.
 */

#ifndef SEGGER_SYSVIEW_CONF_H
#define SEGGER_SYSVIEW_CONF_H

/*********************************************************************
 * Defines, fixed
 *********************************************************************/

// RTT channel for SystemView data (Channel 0 reserved for console I/O)
#define SEGGER_SYSVIEW_RTT_CHANNEL              1

// Retrieve CPU core clock from FreeRTOSConfig
#include "FreeRTOSConfig.h"
#define SEGGER_SYSVIEW_CPU_FREQ                 configCPU_CLOCK_HZ

// RAM base address for STM32F401RE (SRAM starts at 0x20000000)
#define SEGGER_SYSVIEW_RAM_BASE                 (0x20000000)

// Number of bytes used for SystemView timestamp (default 4)
#define SEGGER_SYSVIEW_TIMESTAMP_BITS           32

// Use DWT cycle counter for high-resolution timestamps
// This requires enabling DWT in startup code:
//   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
//   DWT->CYCCNT = 0;
//   DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
#define SEGGER_SYSVIEW_GET_TIMESTAMP()          (*(volatile uint32_t*)(0xE0001004)) // DWT->CYCCNT

// Alternative: Use SysTick for timestamp (less precise but always available)
// #define SEGGER_SYSVIEW_GET_TIMESTAMP()       ((*(volatile uint32_t*)(0xE000E018)) ^ 0xFFFFFF)

// Interrupt ID offset (Cortex-M4 specific)
// External interrupts start at ID 16 (after internal exceptions)
#define SEGGER_SYSVIEW_GET_INTERRUPT_ID()       ((*(volatile uint32_t*)(0xE000ED04)) & 0x1FF)

/*********************************************************************
 * Application-specific defines
 *********************************************************************/

// Application name shown in SystemView
#define SEGGER_SYSVIEW_APP_NAME                 "Stepper Motor Controller"

// Device name
#define SEGGER_SYSVIEW_DEVICE_NAME              "STM32F401RE"

// Unique system ID (optional, can be used to distinguish multiple targets)
#define SEGGER_SYSVIEW_ID_BASE                  0x10000000

// Maximum number of tasks to display (matches configMAX_PRIORITIES)
#define SEGGER_SYSVIEW_SYSDESC0                 "I#15=SysTick"

/*********************************************************************
 * RTT Buffer configuration
 *********************************************************************/

// Size of RTT up buffer for SystemView (default 1024)
#define SEGGER_SYSVIEW_RTT_BUFFER_SIZE          1024

// Post-mortem mode: Set to 1 to enable recording without host connection
// Data will be stored in RTT buffer and can be read after a crash
#define SEGGER_SYSVIEW_POST_MORTEM_MODE         0

#endif // SEGGER_SYSVIEW_CONF_H