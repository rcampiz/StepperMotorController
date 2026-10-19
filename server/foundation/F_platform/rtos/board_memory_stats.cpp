/**
 * @file board_memory_stats.cpp
 * @brief Memory statistics from the linker script (STM32F401RETx_FLASH.ld), the FreeRTOS static
 *        stack pool and the main stack.
 *
 * Flash used = everything up to the .data load image plus that image (= text + data of
 * arm-none-eabi-size). Static RAM = .data + .bss. The main stack (MSP) runs the boot code and,
 * once the scheduler starts (it resets MSP to _estack), every interrupt; it has no guard, only
 * the linker's reserve (_Min_Stack_Size) at the top of RAM. Its peak is measured by painting
 * the free RAM between the heap reserve and the current stack pointer at boot and finding,
 * later, the lowest word that is no longer the paint.
 */

#include "F_platform/rtos/board_memory_stats.hpp"
#include "F_platform/rtos/freertos_scheduler.hpp"

#include <stdint.h>

extern "C" {
// Linker script symbols: only their addresses mean anything
extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _end[], _estack[];
extern uint32_t _Min_Stack_Size[], _Min_Heap_Size[];
extern uint32_t _flash_origin[], _flash_length[], _ram_length[];
void* _sbrk(int incr);   // syscalls.c: the newlib heap end
}

namespace Platform {

namespace {

constexpr uint32_t PAINT = 0xA5A5A5A5u;
uintptr_t s_paintBottom = 0;            // lowest painted word; 0 = not painted
BoardMemoryStats s_memoryStats;

inline uintptr_t addr(const void* p) { return reinterpret_cast<uintptr_t>(p); }

// noinline: the frame of the painter sits under the stack pointer it reads; the margin covers it
__attribute__((noinline)) void paintMainStack()
{
    uintptr_t sp;
    __asm volatile("mov %0, sp" : "=r"(sp));
    uintptr_t bottom = addr(_end) + addr(_Min_Heap_Size);       // keep clear of the heap reserve
    const uintptr_t heapEnd = addr(_sbrk(0));                    // and of any heap already taken
    if (heapEnd > bottom) bottom = heapEnd;
    bottom = (bottom + 3u) & ~static_cast<uintptr_t>(3u);
    const uintptr_t top = (sp - 64u) & ~static_cast<uintptr_t>(3u);
    if (top <= bottom) return;
    for (volatile uint32_t* p = reinterpret_cast<volatile uint32_t*>(bottom);
         p < reinterpret_cast<volatile uint32_t*>(top); ++p) {
        *p = PAINT;
    }
    s_paintBottom = bottom;
}

uint32_t mainStackPeak()
{
    if (s_paintBottom == 0) return 0;
    const volatile uint32_t* p = reinterpret_cast<const volatile uint32_t*>(s_paintBottom);
    const volatile uint32_t* end = reinterpret_cast<const volatile uint32_t*>(addr(_estack));
    while (p < end && *p == PAINT) ++p;
    return static_cast<uint32_t>(addr(_estack) - addr(const_cast<const uint32_t*>(p)));
}

} // namespace

bool BoardMemoryStats::getMemoryStats(MemoryStats& out)
{
    const uint32_t data = static_cast<uint32_t>(addr(_edata) - addr(_sdata));
    const uint32_t bss  = static_cast<uint32_t>(addr(_ebss) - addr(_sbss));
    out.flashUsed      = static_cast<uint32_t>(addr(_sidata) - addr(_flash_origin)) + data;
    out.flashTotal     = static_cast<uint32_t>(addr(_flash_length));
    out.ramStatic      = data + bss;
    out.ramTotal       = static_cast<uint32_t>(addr(_ram_length));
    const uint32_t poolWords = FreeRTOSScheduler::STACK_POOL_WORDS;
    out.stackPoolTotal = poolWords * static_cast<uint32_t>(sizeof(uint32_t));
    out.stackPoolUsed  = (poolWords - FreeRTOSScheduler::stackPoolFreeWords()) * static_cast<uint32_t>(sizeof(uint32_t));
    out.mainStackUsed  = mainStackPeak();
    out.mainStackTotal = static_cast<uint32_t>(addr(_Min_Stack_Size));
    return true;
}

IMemoryStats& initMemoryStats()
{
    paintMainStack();
    return s_memoryStats;
}

} // namespace Platform
