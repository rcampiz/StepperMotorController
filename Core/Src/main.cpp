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

// Drivers
#include "drivers/spi_manager.hpp"
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
#include <string.h>

// NOR flash driver (uses SPI manager internally now)
static SPIFlash* s_norFlash = nullptr;

// ============================================================================
// Early Debug UART - for debugging init failures before FreeRTOS starts
// ============================================================================
namespace EarlyDebug {
    static bool s_initialized = false;

    static void initUART() {
        // Enable clocks
        RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
        RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

        // Brief delay
        volatile uint32_t dummy = RCC->APB1ENR;
        (void)dummy;

        // Configure PA2 (TX) as alternate function
        GPIOA->MODER &= ~(3UL << (2 * 2));
        GPIOA->MODER |= (2UL << (2 * 2));   // AF mode
        GPIOA->OSPEEDR |= (3UL << (2 * 2)); // High speed
        GPIOA->AFR[0] &= ~(0xFUL << (2 * 4));
        GPIOA->AFR[0] |= (7UL << (2 * 4));  // AF7 = USART2

        // Configure PA3 (RX) as alternate function
        GPIOA->MODER &= ~(3UL << (3 * 2));
        GPIOA->MODER |= (2UL << (3 * 2));   // AF mode
        GPIOA->PUPDR &= ~(3UL << (3 * 2));
        GPIOA->PUPDR |= (1UL << (3 * 2));   // Pull-up
        GPIOA->AFR[0] &= ~(0xFUL << (3 * 4));
        GPIOA->AFR[0] |= (7UL << (3 * 4));  // AF7 = USART2

        // Disable USART for config
        USART2->CR1 = 0;

        // Baud rate: 115200 at 42 MHz APB1
        // BRR = 42000000 / (16 * 115200) = 22.786
        USART2->BRR = (22 << 4) | 13;  // Mantissa=22, Fraction=13

        // Enable TX, RX, USART
        USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

        s_initialized = true;
    }

    static void putChar(char c) {
        while ((USART2->SR & USART_SR_TXE) == 0) {}
        USART2->DR = c;
    }

    static void print(const char* str) {
        if (!s_initialized) return;
        while (*str) {
            putChar(*str++);
        }
    }

    static void println(const char* str) {
        print(str);
        print("\r\n");
    }
} // namespace EarlyDebug

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

// Hardware SPI init functions removed - now using bit-banged SPI via g_spiManager
// CS pins are initialized by SPIManager::init()

int main(void)
{
    // 1. Hardware initialization
    SystemClock_Config();

    // Initialize early debug UART first for visibility
    EarlyDebug::initUART();
    EarlyDebug::println("");
    EarlyDebug::println("=== Early Debug ===");
    EarlyDebug::println("Clock config: OK");

    // Initialize microsecond tick timer (TIM5)
    Services::TickTimer_Init();
    EarlyDebug::println("TickTimer: OK");

    // 2. SystemView initialization (optional)
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Conf();
    EarlyDebug::println("SystemView: OK");
#endif

    // 3. Initialize services
    // Control mode manager (must be before encoder init)
    EarlyDebug::print("ControlMode init... ");
    if (!Services::g_controlMode.init()) {
        // Control mode init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // Initialize SPI manager (bit-banged SPI for both buses)
    // SPI1: Motor (Mode3) + LCD (Mode0), ~1 MHz
    // SPI2: NOR Flash (Mode0), ~1 MHz
    EarlyDebug::print("SPI Manager init... ");
    if (!g_spiManager.init(1000000, 1000000)) {
        // SPI manager init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // NOR flash driver for persistent config (uses SPI manager internally)
    // TODO: Update SPIFlash to use g_spiManager
    // For now, we skip NOR flash initialization until SPIFlash is updated
    // static SPIFlash norFlash(...);
    // s_norFlash = &norFlash;
    // norFlash.init();

    // Device config (persistent storage in NOR flash)
    // TODO: Re-enable when SPIFlash is updated to use g_spiManager
    // For now, use defaults
    // if (!Services::g_deviceConfig.init(norFlash)) {
    //     // Device config init failed - continue with defaults
    // }

    // Apply default control mode (using defaults until flash is available)
    Services::g_controlMode.setMode(Services::ControlMode::OPEN_LOOP);

    // Telemetry manager must be initialized before any task uses it
    Comms::g_telemetry.init();
    EarlyDebug::println("Telemetry: OK");

    // Command queue for ARM/START synchronization
    EarlyDebug::print("CommandQueue init... ");
    if (!Services::g_commandQueue.init()) {
        // Command queue init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // 4. Initialize tasks (order matters for dependencies)
    // EncoderTask: OPTIONAL - system runs in OPEN_LOOP if encoder unavailable
    EarlyDebug::print("EncoderTask init... ");
    bool encoderAvailable = Tasks::EncoderTask_Init();
    EarlyDebug::println(encoderAvailable ? "OK" : "SKIP (optional)");
    // EncoderTask_Init updates g_controlMode encoder status internally

    // MotorTask: Creates command queue, uses g_spiManager
    EarlyDebug::print("MotorTask init... ");
    if (!Tasks::MotorTask_Init()) {
        // Motor init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // DisplayTask: Initializes LCD driver, uses g_spiManager
    EarlyDebug::print("DisplayTask init... ");
    if (!Tasks::DisplayTask_Init()) {
        // Display init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // CommsTask: Initializes transport (UART or RTT)
    EarlyDebug::print("CommsTask init... ");
    if (!Tasks::CommsTask_Init(Tasks::TransportType::VCP_UART)) {
        // Comms init failed - halt
        EarlyDebug::println("FAIL!");
        while (1) {}
    }
    EarlyDebug::println("OK");

    // Register joystick callback for REMOTE mode event forwarding
    // (must be after both DisplayTask and CommsTask init)
    Tasks::CommsTask_RegisterJoyCallback();
    EarlyDebug::println("JoyCallback: OK");

    EarlyDebug::println("Creating FreeRTOS tasks...");

    // 5. Create FreeRTOS tasks
    // Priority order: Motor (4) > Comms (3) > Encoder (2) > Display (1)

    // Encoder task only created if hardware initialized successfully
    if (encoderAvailable) {
        xTaskCreate(
            Tasks::vEncoderTask,    // Task function
            "Encoder",              // Task name
            Tasks::ENCODER_TASK_STACK_SIZE,  // Stack size (words)
            nullptr,                // Parameters
            Tasks::ENCODER_TASK_PRIORITY,    // Priority
            nullptr                 // Task handle
        );
    }

    xTaskCreate(
        Tasks::vMotorTask,
        "Motor",
        Tasks::MOTOR_TASK_STACK_SIZE,    // Stack size (words)
        nullptr,
        Tasks::MOTOR_TASK_PRIORITY,      // Highest priority
        &Tasks::g_motorTaskHandle  // Capture task handle for suspend/resume
    );

    xTaskCreate(
        Tasks::vDisplayTask,
        "Display",
        Tasks::DISPLAY_TASK_STACK_SIZE,  // Stack size (words)
        nullptr,
        Tasks::DISPLAY_TASK_PRIORITY,    // Lowest priority
        &Tasks::g_displayTaskHandle  // Capture handle for suspend/resume
    );

    xTaskCreate(
        Tasks::vCommsTask,
        "Comms",
        Tasks::COMMS_TASK_STACK_SIZE,
        nullptr,
        Tasks::COMMS_TASK_PRIORITY,
        nullptr
    );

    // 6. Start SystemView recording (optional)
#ifdef ENABLE_SEGGER_SYSTEMVIEW
    SEGGER_SYSVIEW_Start();
#endif

    EarlyDebug::println("Starting scheduler...");
    EarlyDebug::println("===================");

    // 7. Start the FreeRTOS scheduler
    vTaskStartScheduler();

    // Should never reach here - scheduler returned means failure
    EarlyDebug::println("!!! SCHEDULER RETURNED !!!");
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
    EarlyDebug::print("!!! STACK OVERFLOW: ");
    EarlyDebug::println(pcTaskName);
    // Stack overflow detected - halt
    // In debug builds, set a breakpoint here
    while (1) {}
}

void vApplicationMallocFailedHook(void)
{
    EarlyDebug::println("!!! MALLOC FAILED !!!");
    // Memory allocation failed - halt
    // In debug builds, set a breakpoint here
    while (1) {}
}

// Debug: count idle calls
static volatile uint32_t s_idleCount = 0;

// Idle hook can be used for low-power modes
void vApplicationIdleHook(void)
{
    s_idleCount++;
    // Print once to show we reached idle
    static bool once = false;
    if (!once) {
        once = true;
        EarlyDebug::println("[IDLE] First idle tick");
    }
    // Enter low-power mode when idle
    __WFI();
}

} // extern "C"
