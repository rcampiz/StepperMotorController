/**
 * @file system_init.cpp
 * @brief System initialization — service wiring (RTOS-agnostic)
 *
 * Uses Platform:: for FreeRTOS primitives. All service wiring uses
 * abstract interfaces (ILock*, IClock*, IQueue*).
 * FreeRTOS-specific code lives in platform_init.cpp.
 */

#include "system_init.hpp"
#include "F_platform/rtos/platform_init.hpp"
#include "F_platform/hw/early_debug.hpp"

// CMSIS (for SystemClock_Config register access)
#include "X_vendor/CMSIS/stm32f401xe.h"

// Foundation — adapters
#include "F_platform/adapters/motor_command_sink.hpp"
#include "F_platform/adapters/safety_actions.hpp"
#include "F_util/interface_trace.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "F_platform/adapters/traced/traced_safety_actions.hpp"
#include "F_platform/adapters/traced/traced_command_sink.hpp"
#endif

// Foundation — tasks
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "F_platform/tasks/motor_task.hpp"

// Layers
#include "L2_protocol/telemetry.hpp"
#include "L3_services/config/device_config.hpp"
#include "L3_services/dispatch/command_queue.hpp"
#include "L3_services/dispatch/event_service.hpp"
#include "L3_services/infra/flash_image_service.hpp"
#include "L3_services/infra/tick_timer.hpp"
#include "L3_services/motion/control_mode.hpp"
#include "L3_services/motion/motor_config.hpp"
#include "L3_services/ui/ui_mode.hpp"
#include "L4_drivers/devices/flash_nor.hpp"
#include "L5_board/spi/spi_manager.hpp"

// --- Globals (composition root owns these) ---
Services::IMotorCommandSink* Services::g_motorCommandSink = nullptr;
Services::ISafetyActions*    Services::g_safetyActions     = nullptr;

static SPIFlash* s_norFlash        = nullptr;
static bool      s_encoderAvailable = false;

// --- Helpers ---

static void initOrHalt(const char* name, bool ok) {
    EarlyDebug::print(name);  EarlyDebug::print("... ");
    if (!ok) { EarlyDebug::println("FAIL!"); while (1) {} }
    EarlyDebug::println("OK");
}

static void logOk(const char* name) {
    EarlyDebug::print(name);  EarlyDebug::println("... OK");
}

/// HSE 8 MHz → PLL → 84 MHz SYSCLK, APB1 42 MHz, APB2 84 MHz
static void SystemClock_Config()
{
    RCC->CR |= RCC_CR_HSEON;
    while (!(RCC->CR & RCC_CR_HSERDY)) {}

    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSE |
                   (8 << RCC_PLLCFGR_PLLM_Pos) |
                   (336 << RCC_PLLCFGR_PLLN_Pos) |
                   (1 << RCC_PLLCFGR_PLLP_Pos));

    RCC->CR |= RCC_CR_PLLON;
    while (!(RCC->CR & RCC_CR_PLLRDY)) {}

    FLASH->ACR = FLASH_ACR_LATENCY_2WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC->CFGR = (RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1);
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}

    SystemCoreClock = 84000000;
}

static SPIFlash* probeNorFlash() {
    ISPIBus* spi2 = g_spiManager.getSPI2();
    static SPIBus   flashBus(*spi2);
    static SPIFlash norFlash(flashBus);
    norFlash.init();

    auto jedec = norFlash.readJEDEC();
    if (jedec.manufacturer == 0x00 || jedec.manufacturer == 0xFF) {
        EarlyDebug::println("NOR Flash... NO DEVICE");
        return nullptr;
    }
    EarlyDebug::print("NOR Flash... OK (");
    EarlyDebug::printUint(norFlash.capacityBytes() / 1024);
    EarlyDebug::println("KB)");
    return &norFlash;
}

// --- System namespace ---
namespace System {

void initHardware()
{
    SystemClock_Config();
    EarlyDebug::initUART();
    EarlyDebug::println("\n=== Early Debug ===");
    logOk("Clock config");
    Services::TickTimer_Init();
    logOk("TickTimer");
    ITrace::init();
}

void initPlatform()
{
    Platform::init();
    auto& r = Platform::resources();
    initOrHalt("SPI Manager", g_spiManager.init(*r.spi1Lock, *r.spi2Lock));

    s_norFlash = probeNorFlash();
    if (s_norFlash && Services::g_flashImageService.init(s_norFlash)) {
        EarlyDebug::print("FlashImageService... OK (");
        EarlyDebug::printUint(Services::g_flashImageService.maxSlots());
        EarlyDebug::println(" slots)");
    }
}

void initServices()
{
    auto& r = Platform::resources();
    initOrHalt("ControlMode", Services::g_controlMode.init(*r.controlModeLock));
    Services::g_controlMode.setMode(Services::ControlMode::OPEN_LOOP);

    Comms::g_telemetry.init(*r.telemetryLock, *r.clock);
    logOk("Telemetry");

    Services::Event::init(*r.eventQueue);
    logOk("EventService");

    initOrHalt("UIMode", UI::g_uiMode.init(*r.uiModeLock));
}

void initTasks()
{
    auto& r = Platform::resources();

    s_encoderAvailable = Tasks::EncoderTask_Init();
    EarlyDebug::println(s_encoderAvailable ? "EncoderTask... OK" : "EncoderTask... SKIP");

    initOrHalt("MotorTask", Tasks::MotorTask_Init());

    static MotorCommandSinkImpl motorSink(Tasks::g_motorCmdQueue);
#ifdef ENABLE_INTERFACE_TRACE
    static TracedMotorCommandSink tracedSink(motorSink);
    Services::g_motorCommandSink = &tracedSink;
#else
    Services::g_motorCommandSink = &motorSink;
#endif
    initOrHalt("CommandQueue",
               Services::g_commandQueue.init(*r.commandQueueLock, *r.clock, motorSink));

    if (s_encoderAvailable) {
        const auto& cfg = Services::g_motorConfig.getConfig();
        Tasks::EncoderTask_SetFilter(cfg.encFilterType, cfg.encFilterParam);
    }

    initOrHalt("DisplayTask", Tasks::DisplayTask_Init());
    initOrHalt("CommsTask",   Tasks::CommsTask_Init(Tasks::TransportType::VCP_UART));

    static SafetyActionsImpl safetyActions;
#ifdef ENABLE_INTERFACE_TRACE
    static TracedSafetyActions tracedSafety(safetyActions);
    Services::g_safetyActions = &tracedSafety;
#else
    Services::g_safetyActions = &safetyActions;
#endif
    logOk("JoyCallback");
    Tasks::CommsTask_RegisterJoyCallback();
}

void createTasks()
{
    Platform::createTasks(s_encoderAvailable);
}

void start()
{
    Platform::startScheduler();
}

} // namespace System
