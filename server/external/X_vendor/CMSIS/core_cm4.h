#ifndef __CORE_CM4_H_GENERIC
#define __CORE_CM4_H_GENERIC

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// IO definitions
#ifdef __cplusplus
  #define   __I     volatile
#else
  #define   __I     volatile const
#endif
#define     __O     volatile
#define     __IO    volatile

// NVIC structure
typedef struct {
    __IO uint32_t ISER[8U];
    uint32_t RESERVED0[24U];
    __IO uint32_t ICER[8U];
    uint32_t RESERVED1[24U];
    __IO uint32_t ISPR[8U];
    uint32_t RESERVED2[24U];
    __IO uint32_t ICPR[8U];
    uint32_t RESERVED3[24U];
    __IO uint32_t IABR[8U];
    uint32_t RESERVED4[56U];
    __IO uint8_t  IP[240U];
    uint32_t RESERVED5[644U];
    __O  uint32_t STIR;
} NVIC_Type;

// SCB structure
typedef struct {
    __I  uint32_t CPUID;
    __IO uint32_t ICSR;
    __IO uint32_t VTOR;
    __IO uint32_t AIRCR;
    __IO uint32_t SCR;
    __IO uint32_t CCR;
    __IO uint8_t  SHP[12U];
    __IO uint32_t SHCSR;
    __IO uint32_t CFSR;
    __IO uint32_t HFSR;
    __IO uint32_t DFSR;
    __IO uint32_t MMFAR;
    __IO uint32_t BFAR;
    __IO uint32_t AFSR;
    __I  uint32_t PFR[2U];
    __I  uint32_t DFR;
    __I  uint32_t ADR;
    __I  uint32_t MMFR[4U];
    __I  uint32_t ISAR[5U];
    uint32_t RESERVED0[5U];
    __IO uint32_t CPACR;
} SCB_Type;

// SysTick structure
typedef struct {
    __IO uint32_t CTRL;
    __IO uint32_t LOAD;
    __IO uint32_t VAL;
    __I  uint32_t CALIB;
} SysTick_Type;

// Memory mapping
#define SCS_BASE            (0xE000E000UL)
#define ITM_BASE            (0xE0000000UL)
#define DWT_BASE            (0xE0001000UL)
#define TPI_BASE            (0xE0040000UL)
#define CoreDebug_BASE      (0xE000EDF0UL)
#define SysTick_BASE        (SCS_BASE +  0x0010UL)
#define NVIC_BASE           (SCS_BASE +  0x0100UL)
#define SCB_BASE            (SCS_BASE +  0x0D00UL)

#define SCB                 ((SCB_Type       *)     SCB_BASE)
#define SysTick             ((SysTick_Type   *)     SysTick_BASE)
#define NVIC                ((NVIC_Type      *)     NVIC_BASE)

// Core functions
static inline void __enable_irq(void)  { __asm volatile ("cpsie i" : : : "memory"); }
static inline void __disable_irq(void) { __asm volatile ("cpsid i" : : : "memory"); }
static inline void __NOP(void)         { __asm volatile ("nop"); }
static inline void __WFI(void)         { __asm volatile ("wfi"); }
static inline void __WFE(void)         { __asm volatile ("wfe"); }
static inline void __SEV(void)         { __asm volatile ("sev"); }
static inline void __ISB(void)         { __asm volatile ("isb 0xF":::"memory"); }
static inline void __DSB(void)         { __asm volatile ("dsb 0xF":::"memory"); }
static inline void __DMB(void)         { __asm volatile ("dmb 0xF":::"memory"); }

// NVIC functions
#define __NVIC_PRIO_BITS          4U  // STM32F4 uses 4 priority bits

static inline void NVIC_SetPriority(int IRQn, uint32_t priority)
{
    if (IRQn >= 0) {
        NVIC->IP[(uint32_t)IRQn] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & 0xFFUL);
    } else {
        SCB->SHP[(((uint32_t)IRQn) & 0xFUL) - 4UL] = (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & 0xFFUL);
    }
}

static inline void NVIC_EnableIRQ(int IRQn)
{
    if (IRQn >= 0) {
        NVIC->ISER[(uint32_t)IRQn >> 5UL] = (1UL << ((uint32_t)IRQn & 0x1FUL));
    }
}

static inline void NVIC_DisableIRQ(int IRQn)
{
    if (IRQn >= 0) {
        NVIC->ICER[(uint32_t)IRQn >> 5UL] = (1UL << ((uint32_t)IRQn & 0x1FUL));
    }
}

static inline void NVIC_ClearPendingIRQ(int IRQn)
{
    if (IRQn >= 0) {
        NVIC->ICPR[(uint32_t)IRQn >> 5UL] = (1UL << ((uint32_t)IRQn & 0x1FUL));
    }
}

#ifdef __cplusplus
}
#endif

#endif // __CORE_CM4_H_GENERIC