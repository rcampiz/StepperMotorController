/**
 * @file system_init.cpp
 * @brief System initialization — composition root
 *
 * Five phases: hardware, platform, L3 shared state, subsystems + tasks, start.
 * L3 drives every init cascade (L3 -> L4 -> L5 and L3 -> L2 -> L1); this file only
 * sequences the phases, hands out Platform resources, and publishes the task handles
 * (H_TASKS_L3). Includes: harness, F_platform, L3 init headers.
 */

#include "system_init.hpp"

// Platform / hardware
#include "F_platform/hw/microsecond_timer_tim5.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/pins/early_debug_board.hpp"
#include "harness/pins/platform_board.hpp"
#include "harness/pins/spi_init.hpp"
#include "harness/taps/apply_taps.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "harness/defs/harness_l5_l1_bytes.hpp"

// L3 service inits
#include "L3_services/comms/comms_service_init.hpp"
#include "L3_services/display/display_service_init.hpp"
#include "L3_services/infra/board_init/board_init.hpp"
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
  Services::initClockBoard();
  Services::initDebugOutput();
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
  initOrHalt("SPI Manager",
             Harness::initSPIManager(
                 Harness::applyTap<Harness::TracedLock, 0>(*r.spi1Lock),
                 Harness::applyTap<Harness::TracedLock, 1>(*r.spi2Lock)));
}

// Phase 3: L3 shared state — control mode, events, telemetry store, UI mode
void initServices() {
  const auto &r = s_platform;
  Platform::setTaskStatsProvider(r.taskStats);
  initOrHalt("L3 Services",
             Services::initSystemServices(*r.controlModeLock, *r.eventQueue,
                                          *r.telemetryLock, *r.uiModeLock, *r.clock));
}

// Phase 4: Subsystems — L3 init cascades + RTOS task creation
void initTasks() {
  const auto &r = s_platform;

  Harness::debugPrint("Motor... ");
  Services::MotorSubsystemResult motorResult;
  if (!Services::initMotorSubsystem(*r.motorCmdQueue, *r.clock, *r.scheduler,
                                    *r.commandQueueLock, r.taskStats, motorResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");

  Harness::debugPrint("Encoder... ");
  Services::EncoderSubsystemResult encResult;
  if (!Services::initEncoderSubsystem(*r.clock, *r.scheduler, *r.critical, *r.usClock,
                                      encResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln(encResult.encoderAvailable ? "OK" : "SKIP");

  Harness::debugPrint("Display... ");
  Services::DisplaySubsystemResult dispResult;
  if (!Services::initDisplaySubsystem(*r.clock, *r.scheduler, dispResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");

  Harness::debugPrint("Comms... ");
  Services::CommsServiceDeps deps;
  deps.encoder = encResult.encoder;
  deps.encFilter = encResult.filterControl;
  deps.motorCtrl = motorResult.motorCtrl;
  deps.remoteDisplay = dispResult.remoteDisplay;
  if (!Services::initCommsSubsystem(deps, *r.clock, *r.scheduler, r.notifier)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");

  // H_TASKS_L3 downward signal: opaque task handles for L3 suspend/resume via Platform
  Harness::TaskHandles handles;
  handles.motor = Scheduler::g_motorTaskHandle;
  handles.comms = Scheduler::g_commsTaskHandle;
  handles.display = Scheduler::g_displayTaskHandle;
  Harness::setTaskHandles(handles);

  // X_L5_L1 option: USART2 RX wakes the Comms task (10 ms poll remains the fallback)
  Harness::setCommsRxWake(r.notifier, Scheduler::g_commsTaskHandle);
}

// Phase 5: Start scheduler — never returns
[[noreturn]] void start() {
  s_platform.scheduler->start();
  __builtin_unreachable();
}

} // namespace System
