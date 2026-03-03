/**
 * @file freertos_hooks.cpp
 * @brief FreeRTOS hooks + HardFault diagnostic handler
 *
 * Stack overflow, malloc failure, idle hooks, and Cortex-M4 fault handler.
 * Linked by name — FreeRTOS / NVIC call these automatically.
 */

#include "F_platform/hw/early_debug.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/FreeRTOS.h"
#include "X_middlewares/Third_Party/FreeRTOS-Kernel/include/task.h"

static void printHex32(uint32_t val) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[9];
    for (int i = 7; i >= 0; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[8] = '\0';
    EarlyDebug::print(buf);
}

extern "C" {

// HardFault handler — dumps both PSP (task) and MSP (handler) stacked frames.
// If the fault occurred in an ISR (handler mode), the real fault PC is on MSP,
// while PSP shows where the preempted task was.
void HardFault_Handler(void) {
    EarlyDebug::println("\r\n!!! HARD FAULT !!!");

    // Fault status registers
    EarlyDebug::print("HFSR=0x"); printHex32(SCB->HFSR); EarlyDebug::println("");
    EarlyDebug::print("CFSR=0x"); printHex32(SCB->CFSR); EarlyDebug::println("");
    EarlyDebug::print("BFAR=0x"); printHex32(SCB->BFAR); EarlyDebug::println("");

    // Read both stack pointers
    uint32_t psp, msp;
    __asm volatile("mrs %0, psp" : "=r" (psp));
    __asm volatile("mrs %0, msp" : "=r" (msp));

    // --- PSP frame (task context — where the task was when interrupted) ---
    EarlyDebug::print("PSP=0x"); printHex32(psp); EarlyDebug::println("");
    auto *pf = reinterpret_cast<uint32_t*>(psp);
    EarlyDebug::print("pPC=0x");  printHex32(pf[6]);
    EarlyDebug::print(" pLR=0x"); printHex32(pf[5]); EarlyDebug::println("");

    // --- MSP dump (handler context — contains the actual fault frame) ---
    // Current MSP has been modified by: compiler prologue pushes + HardFault
    // exception frame (32 bytes). Dump 24 words upward to find both frames.
    EarlyDebug::print("MSP=0x"); printHex32(msp); EarlyDebug::println("");
    EarlyDebug::println("MSP dump (24 words):");
    auto *mp = reinterpret_cast<uint32_t*>(msp);
    for (int i = 0; i < 24; i++) {
        if (i % 4 == 0) {
            EarlyDebug::print("+");
            // Print offset as 2-digit hex
            char off[3];
            off[0] = "0123456789ABCDEF"[(i * 4) >> 4];
            off[1] = "0123456789ABCDEF"[(i * 4) & 0xF];
            off[2] = '\0';
            EarlyDebug::print(off);
            EarlyDebug::print(": ");
        }
        printHex32(mp[i]);
        if (i % 4 == 3) EarlyDebug::println("");
        else EarlyDebug::print(" ");
    }

    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        TaskHandle_t cur = xTaskGetCurrentTaskHandle();
        if (cur != nullptr) {
            EarlyDebug::print("Task: ");
            EarlyDebug::println(pcTaskGetName(cur));
        }
    }

    while (1) {}
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
{
    (void)xTask;
    EarlyDebug::print("!!! STACK OVERFLOW: ");
    EarlyDebug::println(pcTaskName);
    while (1) {}
}

void vApplicationMallocFailedHook(void)
{
    EarlyDebug::println("!!! MALLOC FAILED !!!");
    while (1) {}
}

static volatile uint32_t s_idleCount = 0;

void vApplicationIdleHook(void)
{
    s_idleCount++;
    static bool once = false;
    if (!once) {
        once = true;
        EarlyDebug::println("[IDLE] First idle tick");
    }
    __WFI();
}

} // extern "C"
