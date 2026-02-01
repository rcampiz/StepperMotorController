/**
 * @file main.cpp
 * @brief Main application for Stepper Motor Controller
 *
 * FreeRTOS-based stepper motor controller with:
 *   - Raspberry Pi upstream communication (VCP UART or RTT)
 *   - powerSTEP01 motor control via X-NUCLEO-IHM03A1
 *   - LCD display via X-NUCLEO-GFX01M2
 *   - Quadrature encoder feedback with index pulse
 *   - Optional SEGGER SystemView integration
 *
 * Tasks:
 *   - MotorTask (Pri 4): Motor command processing
 *   - CommsTask (Pri 3): Upstream communication
 *   - EncoderTask (Pri 2): Encoder sampling at 100Hz
 *   - DisplayTask (Pri 1): LCD refresh at 10Hz
 */

#include "FreeRTOS.h"
#include "task.h"

// Board and peripheral configuration
#include "board/board_pins.hpp"

// Communication layer
#include "comms/telemetry.hpp"

// Services
#include "services/tick_timer.hpp"
#include "services/command_queue.hpp"
#include "services/device_config.hpp"
#include "services/control_mode.hpp"

// Drivers for NOR flash
#include "drivers/spi_bus.hpp"
#include "drivers/flash_nor.hpp"

// Task headers
#include "tasks/motor_task.hpp"
#include "tasks/encoder_task.hpp"
#include "tasks/display_task.hpp"
#include "tasks/comms_task.hpp"

// Optional SystemView support
#ifdef ENABLE_SEGGER_SYSTEMVIEW
#include "SEGGER_SYSVIEW.h"
#endif

// CMSIS device header (provides RCC, GPIO, SPI, etc.)
#include "stm32f401xe.h"

// Static SPI2 bus and NOR flash instances for persistent configuration
static SPIBus* s_spi2Bus = nullptr;
static SPIFlash* s_norFlash = nullptr;

/**
 * @brief Configure system clocks
 *
 * Sets up:
 *   - HSE as PLL source (8 MHz from ST-LINK)
 *   - PLL to 84 MHz system clock
 *   - APB1 at 42 MHz, APB2 at 84 MHz
 */
static void SystemClock_Config(void)
{
    // Enable HSE
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    // Configure PLL: HSE/M * N / P = 8/8 * 336 / 4 = 84 MHz
    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSE |
                   (8 << RCC_PLLCFGR_PLLM_Pos) |
                   (336 << RCC_PLLCFGR_PLLN_Pos) |
                   (1 << RCC_PLLCFGR_PLLP_Pos));  // PLLP = 4

    // Enable PLL
    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    // Configure flash latency for 84 MHz (2 wait states)
    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    // Configure bus clocks: AHB = SYSCLK, APB1 = SYSCLK/2, APB2 = SYSCLK
    RCC->CFGR = (RCC_CFGR_HPRE_DIV1 |
                 RCC_CFGR_PPRE1_DIV2 |
                 RCC_CFGR_PPRE2_DIV1);

    // Switch to PLL
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    // Update SystemCoreClock variable
    SystemCoreClock = 84000000;
}

/**
 * @brief Initialize SPI1 peripheral
 *
 * SPI1 is shared between powerSTEP01 motor driver and LCD.
 * Configured for ~5 MHz at 84 MHz APB2 (prescaler /16).
 */
static void SPI1_Init(void)
{
    // Enable clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;

    // Configure PA5 (SCK), PA6 (MISO), PA7 (MOSI) as AF5
    GPIOA->MODER &= ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOA->MODER |= (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1);
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFRL5 | GPIO_AFRL_AFRL6 | GPIO_AFRL_AFRL7);
    GPIOA->AFR[0] |= ((5 << GPIO_AFRL_AFRL5_Pos) |
                      (5 << GPIO_AFRL_AFRL6_Pos) |
                      (5 << GPIO_AFRL_AFRL7_Pos));
    GPIOA->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR5 | GPIO_OSPEEDER_OSPEEDR7);

    // Configure SPI1: Master, 8-bit, CPOL=1, CPHA=1, prescaler /16
    SPI1->CR1 = (SPI_CR1_MSTR |
                 SPI_CR1_BR_1 | SPI_CR1_BR_0 |  // /16
                 SPI_CR1_CPOL | SPI_CR1_CPHA |
                 SPI_CR1_SSM | SPI_CR1_SSI);
    SPI1->CR1 |= SPI_CR1_SPE;
}

/**
 * @brief Initialize SPI2 peripheral
 *
 * SPI2 is used for NOR flash on X-NUCLEO-GFX01M2.
 * Configured for ~5 MHz at 42 MHz APB1 (prescaler /8).
 */
static void SPI2_Init(void)
{
    // Enable clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;

    // Configure PB13 (SCK), PB14 (MISO), PB15 (MOSI) as AF5
    // MODER: bits 26-27 (pin13), 28-29 (pin14), 30-31 (pin15)
    GPIOB->MODER &= ~((0x3UL << 26) | (0x3UL << 28) | (0x3UL << 30));
    GPIOB->MODER |= ((0x2UL << 26) | (0x2UL << 28) | (0x2UL << 30));  // AF mode

    // AFR[1] (AFRH): pin13=bits20-23, pin14=bits24-27, pin15=bits28-31
    GPIOB->AFR[1] &= ~((0xFUL << 20) | (0xFUL << 24) | (0xFUL << 28));
    GPIOB->AFR[1] |= ((5UL << 20) | (5UL << 24) | (5UL << 28));  // AF5

    // High speed for SCK and MOSI
    GPIOB->OSPEEDR |= ((0x3UL << 26) | (0x3UL << 30));

    // Configure SPI2: Master, 8-bit, CPOL=0, CPHA=0, prescaler /8 (~5 MHz)
    SPI2->CR1 = (SPI_CR1_MSTR |
                 SPI_CR1_BR_1 |  // /8
                 SPI_CR1_SSM | SPI_CR1_SSI);
    SPI2->CR1 |= SPI_CR1_SPE;
}

/**
 * @brief Initialize chip select GPIOs
 *
 * Configures CS pins for motor driver, LCD, and NOR flash as outputs, initially high.
 */
static void ChipSelect_Init(void)
{
    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    // Motor CS (PC8) - output, high
    GPIOC->MODER &= ~GPIO_MODER_MODER8;
    GPIOC->MODER |= GPIO_MODER_MODER8_0;
    GPIOC->BSRR = GPIO_BSRR_BS8;

    // LCD CS (PB8) - output, high
    GPIOB->MODER &= ~GPIO_MODER_MODER8;
    GPIOB->MODER |= GPIO_MODER_MODER8_0;
    GPIOB->BSRR = GPIO_BSRR_BS8;

    // NOR Flash CS (PC6) - output, high
    // MODER6 = bits 12-13
    GPIOC->MODER &= ~(0x3UL << 12);
    GPIOC->MODER |= (0x1UL << 12);  // Output mode
    GPIOC->BSRR = (1UL << 6);       // Set PC6 high
}

int main(void)
{
    // 1. Hardware initialization
    SystemClock_Config();
    SPI1_Init();
    SPI2_Init();       // NOR flash
    ChipSelect_Init();

    // Initialize microsecond tick timer (TIM5)
    Services::TickTimer_Init();

    // 2. SystemView initialization (optional)
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();
#endif

    // 3. Initialize services
    // Control mode manager (must be before encoder init)
    if (!Services::g_controlMode.init()) {
        // Control mode init failed - halt
        while (1) {}
    }

    // Create SPI2 bus and NOR flash driver for persistent config
    // Note: SPIBus constructor configures the peripheral and pins
    static SPIBus spi2Bus(SPI2, SPIBus::Prescaler::Div8);  // ~5 MHz at 42 MHz APB1
    static SPIFlash norFlash(spi2Bus);
    s_spi2Bus = &spi2Bus;
    s_norFlash = &norFlash;
    norFlash.init();

    // Device config (persistent storage in NOR flash)
    if (!Services::g_deviceConfig.init(norFlash)) {
        // Device config init failed - continue with defaults
        // (flash might not be present or corrupted)
    }

    // Apply default control mode from saved config
    Services::g_controlMode.setMode(
        static_cast<Services::ControlMode>(Services::g_deviceConfig.getDefaultMode()));

    // Telemetry manager must be initialized before any task uses it
    Comms::g_telemetry.init();

    // Command queue for ARM/START synchronization
    if (!Services::g_commandQueue.init()) {
        // Command queue init failed - halt
        while (1) {}
    }

    // 4. Initialize tasks (order matters for dependencies)
    // EncoderTask: OPTIONAL - system runs in OPEN_LOOP if encoder unavailable
    bool encoderAvailable = Tasks::EncoderTask_Init();
    // EncoderTask_Init updates g_controlMode encoder status internally

    // MotorTask: Creates command queue
    if (!Tasks::MotorTask_Init()) {
        // Motor init failed - halt
        while (1) {}
    }

    // DisplayTask: Initializes LCD driver
    if (!Tasks::DisplayTask_Init()) {
        // Display init failed - halt
        while (1) {}
    }

    // CommsTask: Initializes transport (UART or RTT)
    if (!Tasks::CommsTask_Init(Tasks::TransportType::VCP_UART)) {
        // Comms init failed - halt
        while (1) {}
    }

    // 5. Create FreeRTOS tasks
    // Priority order: Motor (4) > Comms (3) > Encoder (2) > Display (1)

    // Encoder task only created if hardware initialized successfully
    if (encoderAvailable) {
        xTaskCreate(
            Tasks::vEncoderTask,    // Task function
            "Encoder",              // Task name
            128,                    // Stack size (words)
            nullptr,                // Parameters
            2,                      // Priority
            nullptr                 // Task handle
        );
    }

    xTaskCreate(
        Tasks::vMotorTask,
        "Motor",
        256,                    // Larger stack for SPI transactions
        nullptr,
        4,                      // Highest priority
        nullptr
    );

    xTaskCreate(
        Tasks::vDisplayTask,
        "Display",
        256,                    // Larger stack for LCD buffer operations
        nullptr,
        1,                      // Lowest priority
        nullptr
    );

    xTaskCreate(
        Tasks::vCommsTask,
        "Comms",
        512,                    // Largest stack for command parsing buffers
        nullptr,
        3,
        nullptr
    );

    // 6. Start SystemView recording (optional)
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Start();
#endif

    // 7. Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here
    while (1) {
        // Error: scheduler failed to start
    }

    return 0;
}

// FreeRTOS application hook implementations
extern "C" {

void vApplicationStackOverflowHook(TaskHandle_t xTask, char* pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    // Stack overflow detected - halt
    // In debug builds, set a breakpoint here
    while (1) {}
}

void vApplicationMallocFailedHook(void)
{
    // Memory allocation failed - halt
    // In debug builds, set a breakpoint here
    while (1) {}
}

// Idle hook can be used for low-power modes
void vApplicationIdleHook(void)
{
    // Enter low-power mode when idle
    __WFI();
}

} // extern "C"
