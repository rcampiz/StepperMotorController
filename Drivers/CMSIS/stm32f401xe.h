#ifndef STM32F401xE_H
#define STM32F401xE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Interrupt Number Definition
typedef enum {
    NonMaskableInt_IRQn         = -14,
    HardFault_IRQn              = -13,
    MemoryManagement_IRQn       = -12,
    BusFault_IRQn               = -11,
    UsageFault_IRQn             = -10,
    SVCall_IRQn                 = -5,
    DebugMonitor_IRQn           = -4,
    PendSV_IRQn                 = -2,
    SysTick_IRQn                = -1,
    WWDG_IRQn                   = 0,
    EXTI16_PVD_IRQn             = 1,
    EXTI21_TAMP_STAMP_IRQn      = 2,
    EXTI22_RTC_WKUP_IRQn        = 3,
    FLASH_IRQn                  = 4,
    RCC_IRQn                    = 5,
} IRQn_Type;

// Processor and Core Peripherals
#define __CM4_REV                 0x0001U
#define __MPU_PRESENT             1U
#define __NVIC_PRIO_BITS          4U
#define __Vendor_SysTickConfig    0U
#define __FPU_PRESENT             1U

#include "core_cm4.h"

// Peripheral memory map
#define FLASH_BASE            0x08000000UL
#define SRAM_BASE             0x20000000UL
#define PERIPH_BASE           0x40000000UL

#define APB1PERIPH_BASE       PERIPH_BASE
#define APB2PERIPH_BASE       (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE       (PERIPH_BASE + 0x00020000UL)
#define AHB2PERIPH_BASE       (PERIPH_BASE + 0x10000000UL)

// AHB1 peripherals
#define GPIOA_BASE            (AHB1PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE            (AHB1PERIPH_BASE + 0x0400UL)
#define GPIOC_BASE            (AHB1PERIPH_BASE + 0x0800UL)
#define GPIOD_BASE            (AHB1PERIPH_BASE + 0x0C00UL)
#define GPIOE_BASE            (AHB1PERIPH_BASE + 0x1000UL)
#define GPIOH_BASE            (AHB1PERIPH_BASE + 0x1C00UL)
#define RCC_BASE              (AHB1PERIPH_BASE + 0x3800UL)

// APB1 peripherals
#define USART2_BASE           (APB1PERIPH_BASE + 0x4400UL)

// APB2 peripherals
#define USART1_BASE           (APB2PERIPH_BASE + 0x1000UL)
#define USART6_BASE           (APB2PERIPH_BASE + 0x1400UL)
#define ADC1_BASE             (APB2PERIPH_BASE + 0x2000UL)
#define ADC_COMMON_BASE       (APB2PERIPH_BASE + 0x2300UL)

// GPIO register structure
typedef struct {
    volatile uint32_t MODER;       // Mode register
    volatile uint32_t OTYPER;      // Output type register
    volatile uint32_t OSPEEDR;     // Output speed register
    volatile uint32_t PUPDR;       // Pull-up/pull-down register
    volatile uint32_t IDR;         // Input data register
    volatile uint32_t ODR;         // Output data register
    volatile uint32_t BSRR;        // Bit set/reset register
    volatile uint32_t LCKR;        // Configuration lock register
    volatile uint32_t AFR[2];      // Alternate function registers
} GPIO_TypeDef;

// RCC register structure
typedef struct {
    volatile uint32_t CR;          // Clock control register
    volatile uint32_t PLLCFGR;     // PLL configuration register
    volatile uint32_t CFGR;        // Clock configuration register
    volatile uint32_t CIR;         // Clock interrupt register
    volatile uint32_t AHB1RSTR;    // AHB1 peripheral reset register
    volatile uint32_t AHB2RSTR;    // AHB2 peripheral reset register
    uint32_t RESERVED0[2];
    volatile uint32_t APB1RSTR;    // APB1 peripheral reset register
    volatile uint32_t APB2RSTR;    // APB2 peripheral reset register
    uint32_t RESERVED1[2];
    volatile uint32_t AHB1ENR;     // AHB1 peripheral clock enable register
    volatile uint32_t AHB2ENR;     // AHB2 peripheral clock enable register
    uint32_t RESERVED2[2];
    volatile uint32_t APB1ENR;     // APB1 peripheral clock enable register
    volatile uint32_t APB2ENR;     // APB2 peripheral clock enable register
    uint32_t RESERVED3[2];
    volatile uint32_t AHB1LPENR;   // AHB1 peripheral clock enable in low power mode register
    volatile uint32_t AHB2LPENR;   // AHB2 peripheral clock enable in low power mode register
    uint32_t RESERVED4[2];
    volatile uint32_t APB1LPENR;   // APB1 peripheral clock enable in low power mode register
    volatile uint32_t APB2LPENR;   // APB2 peripheral clock enable in low power mode register
    uint32_t RESERVED5[2];
    volatile uint32_t BDCR;        // Backup domain control register
    volatile uint32_t CSR;         // Clock control & status register
    uint32_t RESERVED6[2];
    volatile uint32_t SSCGR;       // Spread spectrum clock generation register
    volatile uint32_t PLLI2SCFGR;  // PLLI2S configuration register
} RCC_TypeDef;

// ADC register structure
typedef struct {
    volatile uint32_t SR;          // Status register
    volatile uint32_t CR1;         // Control register 1
    volatile uint32_t CR2;         // Control register 2
    volatile uint32_t SMPR1;       // Sample time register 1
    volatile uint32_t SMPR2;       // Sample time register 2
    volatile uint32_t JOFR1;       // Injected channel data offset register 1
    volatile uint32_t JOFR2;       // Injected channel data offset register 2
    volatile uint32_t JOFR3;       // Injected channel data offset register 3
    volatile uint32_t JOFR4;       // Injected channel data offset register 4
    volatile uint32_t HTR;         // Watchdog higher threshold register
    volatile uint32_t LTR;         // Watchdog lower threshold register
    volatile uint32_t SQR1;        // Regular sequence register 1
    volatile uint32_t SQR2;        // Regular sequence register 2
    volatile uint32_t SQR3;        // Regular sequence register 3
    volatile uint32_t JSQR;        // Injected sequence register
    volatile uint32_t JDR1;        // Injected data register 1
    volatile uint32_t JDR2;        // Injected data register 2
    volatile uint32_t JDR3;        // Injected data register 3
    volatile uint32_t JDR4;        // Injected data register 4
    volatile uint32_t DR;          // Regular data register
} ADC_TypeDef;

// ADC Common register structure
typedef struct {
    volatile uint32_t CSR;         // Common status register
    volatile uint32_t CCR;         // Common control register
    volatile uint32_t CDR;         // Common regular data register
} ADC_Common_TypeDef;

// USART register structure
typedef struct {
    volatile uint32_t SR;          // Status register
    volatile uint32_t DR;          // Data register
    volatile uint32_t BRR;         // Baud rate register
    volatile uint32_t CR1;         // Control register 1
    volatile uint32_t CR2;         // Control register 2
    volatile uint32_t CR3;         // Control register 3
    volatile uint32_t GTPR;        // Guard time and prescaler register
} USART_TypeDef;

// Peripheral declarations
#define GPIOA               ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB               ((GPIO_TypeDef *) GPIOB_BASE)
#define GPIOC               ((GPIO_TypeDef *) GPIOC_BASE)
#define GPIOD               ((GPIO_TypeDef *) GPIOD_BASE)
#define GPIOE               ((GPIO_TypeDef *) GPIOE_BASE)
#define GPIOH               ((GPIO_TypeDef *) GPIOH_BASE)
#define RCC                 ((RCC_TypeDef *) RCC_BASE)
#define ADC1                ((ADC_TypeDef *) ADC1_BASE)
#define ADC_COMMON          ((ADC_Common_TypeDef *) ADC_COMMON_BASE)
#define USART1              ((USART_TypeDef *) USART1_BASE)
#define USART2              ((USART_TypeDef *) USART2_BASE)
#define USART6              ((USART_TypeDef *) USART6_BASE)

// RCC AHB1ENR register bits
#define RCC_AHB1ENR_GPIOAEN_Pos       (0U)
#define RCC_AHB1ENR_GPIOAEN           (1UL << RCC_AHB1ENR_GPIOAEN_Pos)
#define RCC_AHB1ENR_GPIOBEN_Pos       (1U)
#define RCC_AHB1ENR_GPIOBEN           (1UL << RCC_AHB1ENR_GPIOBEN_Pos)
#define RCC_AHB1ENR_GPIOCEN_Pos       (2U)
#define RCC_AHB1ENR_GPIOCEN           (1UL << RCC_AHB1ENR_GPIOCEN_Pos)
#define RCC_AHB1ENR_GPIODEN_Pos       (3U)
#define RCC_AHB1ENR_GPIODEN           (1UL << RCC_AHB1ENR_GPIODEN_Pos)
#define RCC_AHB1ENR_GPIOEEN_Pos       (4U)
#define RCC_AHB1ENR_GPIOEEN           (1UL << RCC_AHB1ENR_GPIOEEN_Pos)
#define RCC_AHB1ENR_GPIOHEN_Pos       (7U)
#define RCC_AHB1ENR_GPIOHEN           (1UL << RCC_AHB1ENR_GPIOHEN_Pos)

// GPIO MODER register bits
#define GPIO_MODER_MODE0_Pos          (0U)
#define GPIO_MODER_MODE0              (0x3UL << GPIO_MODER_MODE0_Pos)
#define GPIO_MODER_MODE5_Pos          (10U)
#define GPIO_MODER_MODE5              (0x3UL << GPIO_MODER_MODE5_Pos)

// GPIO mode definitions
#define GPIO_MODE_INPUT               0x00U
#define GPIO_MODE_OUTPUT              0x01U
#define GPIO_MODE_AF                  0x02U
#define GPIO_MODE_ANALOG              0x03U

// RCC APB2ENR register bits
#define RCC_APB2ENR_ADC1EN_Pos        (8U)
#define RCC_APB2ENR_ADC1EN            (1UL << RCC_APB2ENR_ADC1EN_Pos)

// ADC CR2 register bits
#define ADC_CR2_ADON_Pos              (0U)
#define ADC_CR2_ADON                  (1UL << ADC_CR2_ADON_Pos)
#define ADC_CR2_CONT_Pos              (1U)
#define ADC_CR2_CONT                  (1UL << ADC_CR2_CONT_Pos)
#define ADC_CR2_SWSTART_Pos           (30U)
#define ADC_CR2_SWSTART               (1UL << ADC_CR2_SWSTART_Pos)

// ADC SR register bits
#define ADC_SR_EOC_Pos                (1U)
#define ADC_SR_EOC                    (1UL << ADC_SR_EOC_Pos)

// ADC SQR3 register bits (first 6 conversions)
#define ADC_SQR3_SQ1_Pos              (0U)
#define ADC_SQR3_SQ1_Msk              (0x1FUL << ADC_SQR3_SQ1_Pos)

// ADC SMPR2 register bits (channels 0-9 sample time)
#define ADC_SMPR2_SMP0_Pos            (0U)
#define ADC_SMPR2_SMP0_Msk            (0x7UL << ADC_SMPR2_SMP0_Pos)

// ADC Common CCR register bits
#define ADC_CCR_ADCPRE_Pos            (16U)
#define ADC_CCR_ADCPRE_Msk            (0x3UL << ADC_CCR_ADCPRE_Pos)

// RCC APB1ENR register bits
#define RCC_APB1ENR_USART2EN_Pos      (17U)
#define RCC_APB1ENR_USART2EN          (1UL << RCC_APB1ENR_USART2EN_Pos)

// RCC APB2ENR register bits
#define RCC_APB2ENR_USART1EN_Pos      (4U)
#define RCC_APB2ENR_USART1EN          (1UL << RCC_APB2ENR_USART1EN_Pos)
#define RCC_APB2ENR_USART6EN_Pos      (5U)
#define RCC_APB2ENR_USART6EN          (1UL << RCC_APB2ENR_USART6EN_Pos)

// USART SR register bits
#define USART_SR_PE_Pos               (0U)
#define USART_SR_PE                   (1UL << USART_SR_PE_Pos)
#define USART_SR_FE_Pos               (1U)
#define USART_SR_FE                   (1UL << USART_SR_FE_Pos)
#define USART_SR_NF_Pos               (2U)
#define USART_SR_NF                   (1UL << USART_SR_NF_Pos)
#define USART_SR_ORE_Pos              (3U)
#define USART_SR_ORE                  (1UL << USART_SR_ORE_Pos)
#define USART_SR_IDLE_Pos             (4U)
#define USART_SR_IDLE                 (1UL << USART_SR_IDLE_Pos)
#define USART_SR_RXNE_Pos             (5U)
#define USART_SR_RXNE                 (1UL << USART_SR_RXNE_Pos)
#define USART_SR_TC_Pos               (6U)
#define USART_SR_TC                   (1UL << USART_SR_TC_Pos)
#define USART_SR_TXE_Pos              (7U)
#define USART_SR_TXE                  (1UL << USART_SR_TXE_Pos)
#define USART_SR_LBD_Pos              (8U)
#define USART_SR_LBD                  (1UL << USART_SR_LBD_Pos)
#define USART_SR_CTS_Pos              (9U)
#define USART_SR_CTS                  (1UL << USART_SR_CTS_Pos)

// USART CR1 register bits
#define USART_CR1_SBK_Pos             (0U)
#define USART_CR1_SBK                 (1UL << USART_CR1_SBK_Pos)
#define USART_CR1_RWU_Pos             (1U)
#define USART_CR1_RWU                 (1UL << USART_CR1_RWU_Pos)
#define USART_CR1_RE_Pos              (2U)
#define USART_CR1_RE                  (1UL << USART_CR1_RE_Pos)
#define USART_CR1_TE_Pos              (3U)
#define USART_CR1_TE                  (1UL << USART_CR1_TE_Pos)
#define USART_CR1_IDLEIE_Pos          (4U)
#define USART_CR1_IDLEIE              (1UL << USART_CR1_IDLEIE_Pos)
#define USART_CR1_RXNEIE_Pos          (5U)
#define USART_CR1_RXNEIE              (1UL << USART_CR1_RXNEIE_Pos)
#define USART_CR1_TCIE_Pos            (6U)
#define USART_CR1_TCIE                (1UL << USART_CR1_TCIE_Pos)
#define USART_CR1_TXEIE_Pos           (7U)
#define USART_CR1_TXEIE               (1UL << USART_CR1_TXEIE_Pos)
#define USART_CR1_PEIE_Pos            (8U)
#define USART_CR1_PEIE                (1UL << USART_CR1_PEIE_Pos)
#define USART_CR1_PS_Pos              (9U)
#define USART_CR1_PS                  (1UL << USART_CR1_PS_Pos)
#define USART_CR1_PCE_Pos             (10U)
#define USART_CR1_PCE                 (1UL << USART_CR1_PCE_Pos)
#define USART_CR1_WAKE_Pos            (11U)
#define USART_CR1_WAKE                (1UL << USART_CR1_WAKE_Pos)
#define USART_CR1_M_Pos               (12U)
#define USART_CR1_M                   (1UL << USART_CR1_M_Pos)
#define USART_CR1_UE_Pos              (13U)
#define USART_CR1_UE                  (1UL << USART_CR1_UE_Pos)
#define USART_CR1_OVER8_Pos           (15U)
#define USART_CR1_OVER8               (1UL << USART_CR1_OVER8_Pos)

// USART CR2 register bits
#define USART_CR2_ADD_Pos             (0U)
#define USART_CR2_ADD_Msk             (0xFUL << USART_CR2_ADD_Pos)
#define USART_CR2_LBDL_Pos            (5U)
#define USART_CR2_LBDL                (1UL << USART_CR2_LBDL_Pos)
#define USART_CR2_LBDIE_Pos           (6U)
#define USART_CR2_LBDIE               (1UL << USART_CR2_LBDIE_Pos)
#define USART_CR2_LBCL_Pos            (8U)
#define USART_CR2_LBCL                (1UL << USART_CR2_LBCL_Pos)
#define USART_CR2_CPHA_Pos            (9U)
#define USART_CR2_CPHA                (1UL << USART_CR2_CPHA_Pos)
#define USART_CR2_CPOL_Pos            (10U)
#define USART_CR2_CPOL                (1UL << USART_CR2_CPOL_Pos)
#define USART_CR2_CLKEN_Pos           (11U)
#define USART_CR2_CLKEN               (1UL << USART_CR2_CLKEN_Pos)
#define USART_CR2_STOP_Pos            (12U)
#define USART_CR2_STOP_Msk            (0x3UL << USART_CR2_STOP_Pos)
#define USART_CR2_STOP                USART_CR2_STOP_Msk
#define USART_CR2_LINEN_Pos           (14U)
#define USART_CR2_LINEN               (1UL << USART_CR2_LINEN_Pos)

#ifdef __cplusplus
}
#endif

#endif // STM32F401xE_H
