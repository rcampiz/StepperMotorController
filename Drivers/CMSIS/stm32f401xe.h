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
    EXTI0_IRQn                  = 6,
    EXTI1_IRQn                  = 7,
    EXTI2_IRQn                  = 8,
    EXTI3_IRQn                  = 9,
    EXTI4_IRQn                  = 10,
    DMA1_Stream0_IRQn           = 11,
    DMA1_Stream1_IRQn           = 12,
    DMA1_Stream2_IRQn           = 13,
    DMA1_Stream3_IRQn           = 14,
    DMA1_Stream4_IRQn           = 15,
    DMA1_Stream5_IRQn           = 16,
    DMA1_Stream6_IRQn           = 17,
    ADC_IRQn                    = 18,
    EXTI9_5_IRQn                = 23,
    TIM1_BRK_TIM9_IRQn          = 24,
    TIM1_UP_TIM10_IRQn          = 25,
    TIM1_TRG_COM_TIM11_IRQn     = 26,
    TIM1_CC_IRQn                = 27,
    TIM2_IRQn                   = 28,
    TIM3_IRQn                   = 29,
    TIM4_IRQn                   = 30,
    I2C1_EV_IRQn                = 31,
    I2C1_ER_IRQn                = 32,
    I2C2_EV_IRQn                = 33,
    I2C2_ER_IRQn                = 34,
    SPI1_IRQn                   = 35,
    SPI2_IRQn                   = 36,
    USART1_IRQn                 = 37,
    USART2_IRQn                 = 38,
    EXTI15_10_IRQn              = 40,
    EXTI17_RTC_Alarm_IRQn       = 41,
    EXTI18_OTG_FS_WKUP_IRQn     = 42,
    DMA1_Stream7_IRQn           = 47,
    SDIO_IRQn                   = 49,
    TIM5_IRQn                   = 50,
    SPI3_IRQn                   = 51,
    DMA2_Stream0_IRQn           = 56,
    DMA2_Stream1_IRQn           = 57,
    DMA2_Stream2_IRQn           = 58,
    DMA2_Stream3_IRQn           = 59,
    DMA2_Stream4_IRQn           = 60,
    OTG_FS_IRQn                 = 67,
    DMA2_Stream5_IRQn           = 68,
    DMA2_Stream6_IRQn           = 69,
    DMA2_Stream7_IRQn           = 70,
    USART6_IRQn                 = 71,
    I2C3_EV_IRQn                = 72,
    I2C3_ER_IRQn                = 73,
    FPU_IRQn                    = 81,
    SPI4_IRQn                   = 84,
} IRQn_Type;

// Processor and Core Peripherals
#define __CM4_REV                 0x0001U
#define __MPU_PRESENT             1U
#define __NVIC_PRIO_BITS          4U
#define __Vendor_SysTickConfig    0U
#ifndef __FPU_PRESENT
  #define __FPU_PRESENT           1U
#endif

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
#define TIM2_BASE             (APB1PERIPH_BASE + 0x0000UL)
#define TIM3_BASE             (APB1PERIPH_BASE + 0x0400UL)
#define TIM4_BASE             (APB1PERIPH_BASE + 0x0800UL)
#define TIM5_BASE             (APB1PERIPH_BASE + 0x0C00UL)
#define SPI2_BASE             (APB1PERIPH_BASE + 0x3800UL)
#define SPI3_BASE             (APB1PERIPH_BASE + 0x3C00UL)
#define USART2_BASE           (APB1PERIPH_BASE + 0x4400UL)
#define I2C1_BASE             (APB1PERIPH_BASE + 0x5400UL)
#define I2C2_BASE             (APB1PERIPH_BASE + 0x5800UL)
#define I2C3_BASE             (APB1PERIPH_BASE + 0x5C00UL)

// APB2 peripherals
#define TIM1_BASE             (APB2PERIPH_BASE + 0x0000UL)
#define USART1_BASE           (APB2PERIPH_BASE + 0x1000UL)
#define USART6_BASE           (APB2PERIPH_BASE + 0x1400UL)
#define ADC1_BASE             (APB2PERIPH_BASE + 0x2000UL)
#define ADC_COMMON_BASE       (APB2PERIPH_BASE + 0x2300UL)
#define SPI1_BASE             (APB2PERIPH_BASE + 0x3000UL)
#define SPI4_BASE             (APB2PERIPH_BASE + 0x3400UL)
#define SYSCFG_BASE           (APB2PERIPH_BASE + 0x3800UL)
#define EXTI_BASE             (APB2PERIPH_BASE + 0x3C00UL)
#define TIM9_BASE             (APB2PERIPH_BASE + 0x4000UL)
#define TIM10_BASE            (APB2PERIPH_BASE + 0x4400UL)
#define TIM11_BASE            (APB2PERIPH_BASE + 0x4800UL)

// AHB1 peripherals (additional)
#define FLASH_R_BASE          (AHB1PERIPH_BASE + 0x3C00UL)

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

// TIM register structure
typedef struct {
    volatile uint32_t CR1;         // Control register 1
    volatile uint32_t CR2;         // Control register 2
    volatile uint32_t SMCR;        // Slave mode control register
    volatile uint32_t DIER;        // DMA/Interrupt enable register
    volatile uint32_t SR;          // Status register
    volatile uint32_t EGR;         // Event generation register
    volatile uint32_t CCMR1;       // Capture/compare mode register 1
    volatile uint32_t CCMR2;       // Capture/compare mode register 2
    volatile uint32_t CCER;        // Capture/compare enable register
    volatile uint32_t CNT;         // Counter
    volatile uint32_t PSC;         // Prescaler
    volatile uint32_t ARR;         // Auto-reload register
    volatile uint32_t RCR;         // Repetition counter register
    volatile uint32_t CCR1;        // Capture/compare register 1
    volatile uint32_t CCR2;        // Capture/compare register 2
    volatile uint32_t CCR3;        // Capture/compare register 3
    volatile uint32_t CCR4;        // Capture/compare register 4
    volatile uint32_t BDTR;        // Break and dead-time register
    volatile uint32_t DCR;         // DMA control register
    volatile uint32_t DMAR;        // DMA address for full transfer
    volatile uint32_t OR;          // Option register
} TIM_TypeDef;

// SPI register structure
typedef struct {
    volatile uint32_t CR1;         // Control register 1
    volatile uint32_t CR2;         // Control register 2
    volatile uint32_t SR;          // Status register
    volatile uint32_t DR;          // Data register
    volatile uint32_t CRCPR;       // CRC polynomial register
    volatile uint32_t RXCRCR;      // RX CRC register
    volatile uint32_t TXCRCR;      // TX CRC register
    volatile uint32_t I2SCFGR;     // I2S configuration register
    volatile uint32_t I2SPR;       // I2S prescaler register
} SPI_TypeDef;

// EXTI register structure
typedef struct {
    volatile uint32_t IMR;         // Interrupt mask register
    volatile uint32_t EMR;         // Event mask register
    volatile uint32_t RTSR;        // Rising trigger selection register
    volatile uint32_t FTSR;        // Falling trigger selection register
    volatile uint32_t SWIER;       // Software interrupt event register
    volatile uint32_t PR;          // Pending register
} EXTI_TypeDef;

// SYSCFG register structure
typedef struct {
    volatile uint32_t MEMRMP;      // Memory remap register
    volatile uint32_t PMC;         // Peripheral mode configuration register
    volatile uint32_t EXTICR[4];   // External interrupt configuration registers
    uint32_t RESERVED[2];
    volatile uint32_t CMPCR;       // Compensation cell control register
} SYSCFG_TypeDef;

// FLASH register structure
typedef struct {
    volatile uint32_t ACR;         // Access control register
    volatile uint32_t KEYR;        // Key register
    volatile uint32_t OPTKEYR;     // Option key register
    volatile uint32_t SR;          // Status register
    volatile uint32_t CR;          // Control register
    volatile uint32_t OPTCR;       // Option control register
} FLASH_TypeDef;

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
#define TIM1                ((TIM_TypeDef *) TIM1_BASE)
#define TIM2                ((TIM_TypeDef *) TIM2_BASE)
#define TIM3                ((TIM_TypeDef *) TIM3_BASE)
#define TIM4                ((TIM_TypeDef *) TIM4_BASE)
#define TIM5                ((TIM_TypeDef *) TIM5_BASE)
#define TIM9                ((TIM_TypeDef *) TIM9_BASE)
#define TIM10               ((TIM_TypeDef *) TIM10_BASE)
#define TIM11               ((TIM_TypeDef *) TIM11_BASE)
#define SPI1                ((SPI_TypeDef *) SPI1_BASE)
#define SPI2                ((SPI_TypeDef *) SPI2_BASE)
#define SPI3                ((SPI_TypeDef *) SPI3_BASE)
#define SPI4                ((SPI_TypeDef *) SPI4_BASE)
#define EXTI                ((EXTI_TypeDef *) EXTI_BASE)
#define SYSCFG              ((SYSCFG_TypeDef *) SYSCFG_BASE)
#define FLASH               ((FLASH_TypeDef *) FLASH_R_BASE)

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

// RCC CR register bits
#define RCC_CR_HSION_Pos              (0U)
#define RCC_CR_HSION                  (1UL << RCC_CR_HSION_Pos)
#define RCC_CR_HSIRDY_Pos             (1U)
#define RCC_CR_HSIRDY                 (1UL << RCC_CR_HSIRDY_Pos)
#define RCC_CR_HSEON_Pos              (16U)
#define RCC_CR_HSEON                  (1UL << RCC_CR_HSEON_Pos)
#define RCC_CR_HSERDY_Pos             (17U)
#define RCC_CR_HSERDY                 (1UL << RCC_CR_HSERDY_Pos)
#define RCC_CR_PLLON_Pos              (24U)
#define RCC_CR_PLLON                  (1UL << RCC_CR_PLLON_Pos)
#define RCC_CR_PLLRDY_Pos             (25U)
#define RCC_CR_PLLRDY                 (1UL << RCC_CR_PLLRDY_Pos)

// RCC PLLCFGR register bits
#define RCC_PLLCFGR_PLLM_Pos          (0U)
#define RCC_PLLCFGR_PLLM              (0x3FUL << RCC_PLLCFGR_PLLM_Pos)
#define RCC_PLLCFGR_PLLN_Pos          (6U)
#define RCC_PLLCFGR_PLLN              (0x1FFUL << RCC_PLLCFGR_PLLN_Pos)
#define RCC_PLLCFGR_PLLP_Pos          (16U)
#define RCC_PLLCFGR_PLLP              (0x3UL << RCC_PLLCFGR_PLLP_Pos)
#define RCC_PLLCFGR_PLLSRC_Pos        (22U)
#define RCC_PLLCFGR_PLLSRC            (1UL << RCC_PLLCFGR_PLLSRC_Pos)
#define RCC_PLLCFGR_PLLSRC_HSE        RCC_PLLCFGR_PLLSRC
#define RCC_PLLCFGR_PLLQ_Pos          (24U)
#define RCC_PLLCFGR_PLLQ              (0xFUL << RCC_PLLCFGR_PLLQ_Pos)

// RCC CFGR register bits
#define RCC_CFGR_SW_Pos               (0U)
#define RCC_CFGR_SW                   (0x3UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSI               (0x0UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_HSE               (0x1UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SW_PLL               (0x2UL << RCC_CFGR_SW_Pos)
#define RCC_CFGR_SWS_Pos              (2U)
#define RCC_CFGR_SWS                  (0x3UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSI              (0x0UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_HSE              (0x1UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_SWS_PLL              (0x2UL << RCC_CFGR_SWS_Pos)
#define RCC_CFGR_HPRE_Pos             (4U)
#define RCC_CFGR_HPRE                 (0xFUL << RCC_CFGR_HPRE_Pos)
#define RCC_CFGR_HPRE_DIV1            (0x0UL << RCC_CFGR_HPRE_Pos)
#define RCC_CFGR_PPRE1_Pos            (10U)
#define RCC_CFGR_PPRE1                (0x7UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE1_DIV1           (0x0UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE1_DIV2           (0x4UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE1_DIV4           (0x5UL << RCC_CFGR_PPRE1_Pos)
#define RCC_CFGR_PPRE2_Pos            (13U)
#define RCC_CFGR_PPRE2                (0x7UL << RCC_CFGR_PPRE2_Pos)
#define RCC_CFGR_PPRE2_DIV1           (0x0UL << RCC_CFGR_PPRE2_Pos)
#define RCC_CFGR_PPRE2_DIV2           (0x4UL << RCC_CFGR_PPRE2_Pos)

// RCC APB1ENR additional bits
#define RCC_APB1ENR_TIM2EN_Pos        (0U)
#define RCC_APB1ENR_TIM2EN            (1UL << RCC_APB1ENR_TIM2EN_Pos)
#define RCC_APB1ENR_TIM3EN_Pos        (1U)
#define RCC_APB1ENR_TIM3EN            (1UL << RCC_APB1ENR_TIM3EN_Pos)
#define RCC_APB1ENR_TIM4EN_Pos        (2U)
#define RCC_APB1ENR_TIM4EN            (1UL << RCC_APB1ENR_TIM4EN_Pos)
#define RCC_APB1ENR_TIM5EN_Pos        (3U)
#define RCC_APB1ENR_TIM5EN            (1UL << RCC_APB1ENR_TIM5EN_Pos)
#define RCC_APB1ENR_SPI2EN_Pos        (14U)
#define RCC_APB1ENR_SPI2EN            (1UL << RCC_APB1ENR_SPI2EN_Pos)
#define RCC_APB1ENR_SPI3EN_Pos        (15U)
#define RCC_APB1ENR_SPI3EN            (1UL << RCC_APB1ENR_SPI3EN_Pos)

// RCC APB2ENR additional bits
#define RCC_APB2ENR_TIM1EN_Pos        (0U)
#define RCC_APB2ENR_TIM1EN            (1UL << RCC_APB2ENR_TIM1EN_Pos)
#define RCC_APB2ENR_SPI1EN_Pos        (12U)
#define RCC_APB2ENR_SPI1EN            (1UL << RCC_APB2ENR_SPI1EN_Pos)
#define RCC_APB2ENR_SPI4EN_Pos        (13U)
#define RCC_APB2ENR_SPI4EN            (1UL << RCC_APB2ENR_SPI4EN_Pos)
#define RCC_APB2ENR_SYSCFGEN_Pos      (14U)
#define RCC_APB2ENR_SYSCFGEN          (1UL << RCC_APB2ENR_SYSCFGEN_Pos)

// FLASH ACR register bits
#define FLASH_ACR_LATENCY_Pos         (0U)
#define FLASH_ACR_LATENCY             (0xFUL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_0WS         (0x0UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_1WS         (0x1UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_2WS         (0x2UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_LATENCY_3WS         (0x3UL << FLASH_ACR_LATENCY_Pos)
#define FLASH_ACR_PRFTEN_Pos          (8U)
#define FLASH_ACR_PRFTEN              (1UL << FLASH_ACR_PRFTEN_Pos)
#define FLASH_ACR_ICEN_Pos            (9U)
#define FLASH_ACR_ICEN                (1UL << FLASH_ACR_ICEN_Pos)
#define FLASH_ACR_DCEN_Pos            (10U)
#define FLASH_ACR_DCEN                (1UL << FLASH_ACR_DCEN_Pos)

// GPIO MODER register bits (all pins)
#define GPIO_MODER_MODER0_Pos         (0U)
#define GPIO_MODER_MODER0             (0x3UL << GPIO_MODER_MODER0_Pos)
#define GPIO_MODER_MODER0_0           (0x1UL << GPIO_MODER_MODER0_Pos)
#define GPIO_MODER_MODER0_1           (0x2UL << GPIO_MODER_MODER0_Pos)
#define GPIO_MODER_MODER1_Pos         (2U)
#define GPIO_MODER_MODER1             (0x3UL << GPIO_MODER_MODER1_Pos)
#define GPIO_MODER_MODER1_0           (0x1UL << GPIO_MODER_MODER1_Pos)
#define GPIO_MODER_MODER1_1           (0x2UL << GPIO_MODER_MODER1_Pos)
#define GPIO_MODER_MODER2_Pos         (4U)
#define GPIO_MODER_MODER2             (0x3UL << GPIO_MODER_MODER2_Pos)
#define GPIO_MODER_MODER3_Pos         (6U)
#define GPIO_MODER_MODER3             (0x3UL << GPIO_MODER_MODER3_Pos)
#define GPIO_MODER_MODER4_Pos         (8U)
#define GPIO_MODER_MODER4             (0x3UL << GPIO_MODER_MODER4_Pos)
#define GPIO_MODER_MODER5_Pos         (10U)
#define GPIO_MODER_MODER5             (0x3UL << GPIO_MODER_MODER5_Pos)
#define GPIO_MODER_MODER5_0           (0x1UL << GPIO_MODER_MODER5_Pos)
#define GPIO_MODER_MODER5_1           (0x2UL << GPIO_MODER_MODER5_Pos)
#define GPIO_MODER_MODER6_Pos         (12U)
#define GPIO_MODER_MODER6             (0x3UL << GPIO_MODER_MODER6_Pos)
#define GPIO_MODER_MODER6_0           (0x1UL << GPIO_MODER_MODER6_Pos)
#define GPIO_MODER_MODER6_1           (0x2UL << GPIO_MODER_MODER6_Pos)
#define GPIO_MODER_MODER7_Pos         (14U)
#define GPIO_MODER_MODER7             (0x3UL << GPIO_MODER_MODER7_Pos)
#define GPIO_MODER_MODER7_0           (0x1UL << GPIO_MODER_MODER7_Pos)
#define GPIO_MODER_MODER7_1           (0x2UL << GPIO_MODER_MODER7_Pos)
#define GPIO_MODER_MODER8_Pos         (16U)
#define GPIO_MODER_MODER8             (0x3UL << GPIO_MODER_MODER8_Pos)
#define GPIO_MODER_MODER8_0           (0x1UL << GPIO_MODER_MODER8_Pos)
#define GPIO_MODER_MODER8_1           (0x2UL << GPIO_MODER_MODER8_Pos)
#define GPIO_MODER_MODER9_Pos         (18U)
#define GPIO_MODER_MODER9             (0x3UL << GPIO_MODER_MODER9_Pos)
#define GPIO_MODER_MODER10_Pos        (20U)
#define GPIO_MODER_MODER10            (0x3UL << GPIO_MODER_MODER10_Pos)
#define GPIO_MODER_MODER11_Pos        (22U)
#define GPIO_MODER_MODER11            (0x3UL << GPIO_MODER_MODER11_Pos)
#define GPIO_MODER_MODER12_Pos        (24U)
#define GPIO_MODER_MODER12            (0x3UL << GPIO_MODER_MODER12_Pos)
#define GPIO_MODER_MODER13_Pos        (26U)
#define GPIO_MODER_MODER13            (0x3UL << GPIO_MODER_MODER13_Pos)
#define GPIO_MODER_MODER14_Pos        (28U)
#define GPIO_MODER_MODER14            (0x3UL << GPIO_MODER_MODER14_Pos)
#define GPIO_MODER_MODER15_Pos        (30U)
#define GPIO_MODER_MODER15            (0x3UL << GPIO_MODER_MODER15_Pos)

// GPIO PUPDR register bits
#define GPIO_PUPDR_PUPDR0_Pos         (0U)
#define GPIO_PUPDR_PUPDR0             (0x3UL << GPIO_PUPDR_PUPDR0_Pos)
#define GPIO_PUPDR_PUPDR0_0           (0x1UL << GPIO_PUPDR_PUPDR0_Pos)
#define GPIO_PUPDR_PUPDR0_1           (0x2UL << GPIO_PUPDR_PUPDR0_Pos)
#define GPIO_PUPDR_PUPDR4_Pos         (8U)
#define GPIO_PUPDR_PUPDR4             (0x3UL << GPIO_PUPDR_PUPDR4_Pos)
#define GPIO_PUPDR_PUPDR4_0           (0x1UL << GPIO_PUPDR_PUPDR4_Pos)
#define GPIO_PUPDR_PUPDR4_1           (0x2UL << GPIO_PUPDR_PUPDR4_Pos)

// GPIO OSPEEDR register bits
#define GPIO_OSPEEDER_OSPEEDR5_Pos    (10U)
#define GPIO_OSPEEDER_OSPEEDR5        (0x3UL << GPIO_OSPEEDER_OSPEEDR5_Pos)
#define GPIO_OSPEEDER_OSPEEDR7_Pos    (14U)
#define GPIO_OSPEEDER_OSPEEDR7        (0x3UL << GPIO_OSPEEDER_OSPEEDR7_Pos)

// GPIO AFR register bits
#define GPIO_AFRL_AFRL0_Pos           (0U)
#define GPIO_AFRL_AFRL0               (0xFUL << GPIO_AFRL_AFRL0_Pos)
#define GPIO_AFRL_AFRL1_Pos           (4U)
#define GPIO_AFRL_AFRL1               (0xFUL << GPIO_AFRL_AFRL1_Pos)
#define GPIO_AFRL_AFRL5_Pos           (20U)
#define GPIO_AFRL_AFRL5               (0xFUL << GPIO_AFRL_AFRL5_Pos)
#define GPIO_AFRL_AFRL6_Pos           (24U)
#define GPIO_AFRL_AFRL6               (0xFUL << GPIO_AFRL_AFRL6_Pos)
#define GPIO_AFRL_AFRL7_Pos           (28U)
#define GPIO_AFRL_AFRL7               (0xFUL << GPIO_AFRL_AFRL7_Pos)

// GPIO BSRR register bits
#define GPIO_BSRR_BS0                 (1UL << 0)
#define GPIO_BSRR_BS1                 (1UL << 1)
#define GPIO_BSRR_BS8                 (1UL << 8)
#define GPIO_BSRR_BR0                 (1UL << 16)
#define GPIO_BSRR_BR1                 (1UL << 17)
#define GPIO_BSRR_BR8                 (1UL << 24)

// SPI CR1 register bits
#define SPI_CR1_CPHA_Pos              (0U)
#define SPI_CR1_CPHA                  (1UL << SPI_CR1_CPHA_Pos)
#define SPI_CR1_CPOL_Pos              (1U)
#define SPI_CR1_CPOL                  (1UL << SPI_CR1_CPOL_Pos)
#define SPI_CR1_MSTR_Pos              (2U)
#define SPI_CR1_MSTR                  (1UL << SPI_CR1_MSTR_Pos)
#define SPI_CR1_BR_Pos                (3U)
#define SPI_CR1_BR                    (0x7UL << SPI_CR1_BR_Pos)
#define SPI_CR1_BR_0                  (0x1UL << SPI_CR1_BR_Pos)
#define SPI_CR1_BR_1                  (0x2UL << SPI_CR1_BR_Pos)
#define SPI_CR1_BR_2                  (0x4UL << SPI_CR1_BR_Pos)
#define SPI_CR1_SPE_Pos               (6U)
#define SPI_CR1_SPE                   (1UL << SPI_CR1_SPE_Pos)
#define SPI_CR1_SSI_Pos               (8U)
#define SPI_CR1_SSI                   (1UL << SPI_CR1_SSI_Pos)
#define SPI_CR1_SSM_Pos               (9U)
#define SPI_CR1_SSM                   (1UL << SPI_CR1_SSM_Pos)

// SPI SR register bits
#define SPI_SR_RXNE_Pos               (0U)
#define SPI_SR_RXNE                   (1UL << SPI_SR_RXNE_Pos)
#define SPI_SR_TXE_Pos                (1U)
#define SPI_SR_TXE                    (1UL << SPI_SR_TXE_Pos)
#define SPI_SR_BSY_Pos                (7U)
#define SPI_SR_BSY                    (1UL << SPI_SR_BSY_Pos)

// TIM CR1 register bits
#define TIM_CR1_CEN_Pos               (0U)
#define TIM_CR1_CEN                   (1UL << TIM_CR1_CEN_Pos)

// TIM SMCR register bits
#define TIM_SMCR_SMS_Pos              (0U)
#define TIM_SMCR_SMS                  (0x7UL << TIM_SMCR_SMS_Pos)
#define TIM_SMCR_SMS_0                (0x1UL << TIM_SMCR_SMS_Pos)
#define TIM_SMCR_SMS_1                (0x2UL << TIM_SMCR_SMS_Pos)
#define TIM_SMCR_SMS_2                (0x4UL << TIM_SMCR_SMS_Pos)

// TIM CCMR1 register bits
#define TIM_CCMR1_CC1S_Pos            (0U)
#define TIM_CCMR1_CC1S                (0x3UL << TIM_CCMR1_CC1S_Pos)
#define TIM_CCMR1_CC1S_0              (0x1UL << TIM_CCMR1_CC1S_Pos)
#define TIM_CCMR1_CC2S_Pos            (8U)
#define TIM_CCMR1_CC2S                (0x3UL << TIM_CCMR1_CC2S_Pos)
#define TIM_CCMR1_CC2S_0              (0x1UL << TIM_CCMR1_CC2S_Pos)

// TIM EGR register bits
#define TIM_EGR_UG_Pos                (0U)
#define TIM_EGR_UG                    (1UL << TIM_EGR_UG_Pos)

// EXTI register bits
#define EXTI_IMR_MR4_Pos              (4U)
#define EXTI_IMR_MR4                  (1UL << EXTI_IMR_MR4_Pos)
#define EXTI_FTSR_TR4_Pos             (4U)
#define EXTI_FTSR_TR4                 (1UL << EXTI_FTSR_TR4_Pos)
#define EXTI_PR_PR4_Pos               (4U)
#define EXTI_PR_PR4                   (1UL << EXTI_PR_PR4_Pos)

// SYSCFG EXTICR register bits
#define SYSCFG_EXTICR2_EXTI4_Pos      (0U)
#define SYSCFG_EXTICR2_EXTI4          (0xFUL << SYSCFG_EXTICR2_EXTI4_Pos)
#define SYSCFG_EXTICR2_EXTI4_PC       (0x2UL << SYSCFG_EXTICR2_EXTI4_Pos)

#ifdef __cplusplus
}
#endif

#endif // STM32F401xE_H
