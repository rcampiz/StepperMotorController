/**
 * @file SEGGER_SYSVIEW_Config_FreeRTOS.c
 * @brief SystemView configuration callbacks for FreeRTOS on STM32F401RE
 *
 * This file implements the required SystemView callbacks for use with FreeRTOS.
 *
 * IMPORTANT: This is a placeholder/template. After downloading SEGGER SystemView,
 * compare with the official template in SystemView/Sample/FreeRTOSVxx/ and merge
 * any missing functionality.
 */

#ifdef ENABLE_SEGGER_SYSTEMVIEW

#include "FreeRTOS.h"
#include "SEGGER_SYSVIEW.h"
#include "SEGGER_SYSVIEW_Conf.h"
#include "stm32f401xe.h"

/*********************************************************************
 * External data
 *********************************************************************/

extern const SEGGER_SYSVIEW_OS_API SYSVIEW_X_OS_TraceAPI;

/*********************************************************************
 * Local defines
 *********************************************************************/

// System core clock (from system_stm32f4xx.c)
extern uint32_t SystemCoreClock;

/*********************************************************************
 * Global functions
 *********************************************************************/

/**
 * @brief Initialize SystemView timestamp source
 *
 * Configures the DWT cycle counter for high-resolution timestamps.
 * Called during SEGGER_SYSVIEW_Conf().
 */
void SEGGER_SYSVIEW_Conf(void)
{
    // Enable DWT cycle counter for timestamps
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    // Configure SystemView
    SEGGER_SYSVIEW_Init(
        SEGGER_SYSVIEW_CPU_FREQ,           // CPU frequency
        SEGGER_SYSVIEW_CPU_FREQ,           // Timestamp frequency (same as CPU for DWT)
        &SYSVIEW_X_OS_TraceAPI,            // FreeRTOS trace API
        _cbSendSystemDesc                   // System description callback
    );

    SEGGER_SYSVIEW_SetRAMBase(SEGGER_SYSVIEW_RAM_BASE);
}

/**
 * @brief Get current timestamp
 *
 * Returns the current DWT cycle counter value.
 * Used by SystemView for precise timing measurements.
 *
 * @return Current cycle count
 */
U32 SEGGER_SYSVIEW_X_GetTimestamp(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief Get current interrupt ID
 *
 * Returns the currently executing interrupt number.
 * Used by SystemView to track ISR execution.
 *
 * @return Interrupt number (0 if not in ISR)
 */
U32 SEGGER_SYSVIEW_X_GetInterruptId(void)
{
    // Read IPSR register (Interrupt Program Status Register)
    // Lower 9 bits contain the exception number
    // 0 = Thread mode, 1-15 = Cortex-M exceptions, 16+ = external interrupts
    return ((*(volatile U32*)(0xE000ED04)) & 0x1FF);
}

/*********************************************************************
 * Local functions
 *********************************************************************/

/**
 * @brief Send system description to SystemView
 *
 * This callback is invoked by SystemView to retrieve information about
 * the system configuration for display in the SystemView application.
 */
static void _cbSendSystemDesc(void)
{
    SEGGER_SYSVIEW_SendSysDesc("N=" SEGGER_SYSVIEW_APP_NAME);
    SEGGER_SYSVIEW_SendSysDesc("D=" SEGGER_SYSVIEW_DEVICE_NAME);
    SEGGER_SYSVIEW_SendSysDesc("O=FreeRTOS");

    // Describe interrupt sources
    SEGGER_SYSVIEW_SendSysDesc("I#15=SysTick");
    SEGGER_SYSVIEW_SendSysDesc("I#20=EXTI4");  // Encoder index
}

#endif // ENABLE_SEGGER_SYSTEMVIEW