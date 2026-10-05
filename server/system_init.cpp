/**
 * @file system_init.cpp
 * @brief System initialization — composition root
 *
 * Five phases: hardware, platform, L3 shared state, subsystems + tasks, start.
 * L3 drives every init cascade (L3 -> L4 -> L5 and L3 -> L2 -> L1); this file sequences
 * the phases, hands out Platform resources, and creates the RTOS tasks (rule 1: the task
 * sheet sits above L3, so task creation belongs here, not in L3). The composition root
 * may see every sheet.
 */

#include "system_init.hpp"

// Platform / hardware
#include "F_platform/hw/microsecond_timer_tim5.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/motor_task_init.hpp"
#include "F_platform/tasks/comms_task_init.hpp"
#include "F_platform/tasks/encoder_task_init.hpp"
#include "F_platform/tasks/display_task_init.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/pins/early_debug_board.hpp"
#include "harness/pins/clock_board.hpp"
#include "harness/pins/platform_board.hpp"
#include "harness/pins/spi_init.hpp"
#include "harness/defs/harness_l5_l1_bytes.hpp"

// L3 service inits
#include "L3_services/comms/comms_service_init.hpp"
#include "L3_services/display/display_service_init.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/motion/encoder_service_init.hpp"
#include "L3_services/motion/motor_service_init.hpp"
#include "L3_services/motion/system_service_init.hpp"

namespace System {

static Harness::PlatformResources s_platform;

static void initOrHalt(const char *name, bool ok) {
  Harness::debugPrint(name);
  Harness::debugPrint("... ");
  if (!ok) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");
}

static void logOk(const char *name) {
  Harness::debugPrint(name);
  Harness::debugPrintln("... OK");
}

// Phase 1: Hardware — clocks, debug UART, microsecond clock, trace sink
void initHardware() {
  // Composition root may see every sheet: call the L5 / Platform init contracts directly
  Harness::initClockBoard();
  Harness::initDebugOutput();
  Harness::debugPrintln("\n=== Early Debug ===");
  logOk("Clock config");
  Platform::IMicrosecondClock &usClock = Platform::initMicrosecondClockTIM5();
  Services::TickTimer_Init(usClock);
  Harness::setMicrosecondClock(&usClock);
  logOk("TickTimer");
  Trace::initTraceSink();
}

// Phase 2: Platform — RTOS primitives, SPI manager
void initPlatform() {
  initOrHalt("Platform", Harness::initPlatformBoard(s_platform));
  const auto &r = s_platform;
  initOrHalt("SPI Manager", Harness::connectSPIManager(*r.spi1Lock, *r.spi2Lock));
}

// Phase 3: L3 shared state — control mode, events, telemetry store, UI mode
void initServices() {
  const auto &r = s_platform;
  Platform::setTaskStatsProvider(r.taskStats);
  initOrHalt("L3 Services",
             Services::initSystemServices(*r.controlModeLock, *r.eventQueue,
                                          *r.telemetryLock, *r.uiModeLock, *r.clock));
}

// Phase 4: Subsystems — L3 init cascades, then the task sheet is wired to each service
void initTasks() {
  const auto &r = s_platform;

  // Motor: L3 cascade, then the Motor task over its command queue
  Services::MotorSubsystemResult motorResult;
  initOrHalt("Motor", Services::initMotorSubsystem(*r.motorCmdQueue, *r.clock,
                                                   *r.commandQueueLock, r.taskStats,
                                                   motorResult));
  initOrHalt("Motor task", Scheduler::initMotorTask(*motorResult.motorService,
                                                    *r.motorCmdQueue, *r.clock, *r.scheduler));

  // Encoder: optional hardware; no task without it
  Services::EncoderSubsystemResult encResult;
  initOrHalt("Encoder", Services::initEncoderSubsystem(*r.clock, *r.critical, *r.usClock,
                                                       encResult));
  if (encResult.encoderService != nullptr) {
    initOrHalt("Encoder task", Scheduler::initEncoderTask(*encResult.encoderService,
                                                          *r.clock, *r.scheduler));
  } else {
    Harness::debugPrintln("Encoder task... SKIP");
  }

  // Display
  Services::DisplaySubsystemResult dispResult;
  initOrHalt("Display", Services::initDisplaySubsystem(*r.clock, dispResult));
  initOrHalt("Display task", Scheduler::initDisplayTask(*dispResult.displayService,
                                                        *r.clock, *r.scheduler));

  // Comms: dispatchers + L2/L1 cascade, then the Comms task with the USART2 RX wake
  Services::CommsServiceDeps deps;
  deps.encoder = encResult.encoder;
  deps.encFilter = encResult.filterControl;
  deps.motorCtrl = motorResult.motorCtrl;
  deps.remoteDisplay = dispResult.remoteDisplay;
  deps.displayControl = dispResult.displayControl;
  Services::CommsSubsystemResult commsResult;
  initOrHalt("Comms", Services::initCommsSubsystem(deps, *r.clock, commsResult));
  initOrHalt("Comms task", Scheduler::initCommsTask(*commsResult.commsService, *r.clock,
                                                    *r.scheduler, r.notifier));

  // X_L5_L1 option: USART2 RX wakes the Comms task (10 ms poll remains the fallback)
  Harness::setCommsRxWake(r.notifier, Scheduler::g_commsTaskHandle);
}

// Phase 5: Start scheduler — never returns
[[noreturn]] void start() {
  s_platform.scheduler->start();
  __builtin_unreachable();
}

} // namespace System
