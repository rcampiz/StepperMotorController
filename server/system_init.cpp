/**
 * @file system_init.cpp
 * @brief System initialization — composition root
 *
 * All init wiring lives here. Harness/init/ has no .cpp files.
 * Only includes harness contracts, L3 service inits, and trace infra.
 * No direct F_platform, L4, L2, or L1 includes.
 */

#include "system_init.hpp"

// Concrete types for init, then wired as harness interfaces
#include "F_platform/types/system_snapshot.hpp"
#include "F_platform/ui/ui_mode.hpp"
#include "harness/pins/itelemetry.hpp"
#include "harness/pins/isnapshot_writer.hpp"
#include "harness/pins/iui_mode.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/pins/idispatch_stats.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "F_platform/tasks/comms_task.hpp"

// L3 service inits
#include "L3_services/comms/comms_service_init.hpp"
#include "L3_services/display/display_service_init.hpp"
#include "L3_services/infra/board_init/board_init.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/infra/trace/trace.hpp"
#include "L3_services/motion/encoder_service_init.hpp"
#include "L3_services/motion/motor_service_init.hpp"
#include "L3_services/motion/system_service_init.hpp"

// Harness contracts (platform init, debug utilities)
#include "harness/pins/early_debug_board.hpp"
#include "harness/pins/platform_board.hpp"
#include "harness/pins/spi_init.hpp"
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_lock.hpp"
#endif

namespace System {

// Platform resources (populated in initPlatform, used by later phases)
static Harness::PlatformResources s_platform;

// ============================================================================
// Helpers
// ============================================================================

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

// ============================================================================
// Phase 1: Hardware — clocks, debug UART, tick timer, trace sink
// ============================================================================

void initHardware() {
  Services::initClockBoard();
  Services::initDebugOutput();
  Harness::debugPrintln("\n=== Early Debug ===");
  logOk("Clock config");
  Services::TickTimer_Init();
  Harness::setMicrosecondClock(Services::TickTimer_GetMicrosecondClock());
  logOk("TickTimer");
  Trace::initTraceSink();
}

// ============================================================================
// Phase 2: Platform — RTOS primitives, SPI manager
// ============================================================================

void initPlatform() {
  initOrHalt("Platform", Harness::initPlatformBoard(s_platform));
  const auto &r = s_platform;
#ifdef ENABLE_INTERFACE_TRACE
  static Harness::TracedLock s_tracedSpi1Lock(*r.spi1Lock);
  static Harness::TracedLock s_tracedSpi2Lock(*r.spi2Lock);
  initOrHalt("SPI Manager",
             Harness::initSPIManager(s_tracedSpi1Lock, s_tracedSpi2Lock));
#else
  initOrHalt("SPI Manager", Harness::initSPIManager(*r.spi1Lock, *r.spi2Lock));
#endif
}

// ============================================================================
// Phase 3: Services — L3 control mode, telemetry, UI mode
// ============================================================================

void initServices() {
  const auto &r = s_platform;

  // Task stats — wire platform's ITaskStats into harness accessor
  Harness::setTaskStatsProvider(r.taskStats);

  // System snapshot — tap-populated telemetry aggregator
  static SystemSnapshot s_snapshot;
  s_snapshot.init(*r.telemetryLock, *r.clock);
  Harness::setTelemetryProvider(&s_snapshot);
  Harness::setSnapshotWriter(&s_snapshot);

  // UI mode init — composition root owns the concrete type
  UI::g_uiMode.init(*r.uiModeLock);
  Harness::setUIModeProvider(&UI::g_uiMode);

  initOrHalt("L3 Services", Services::initSystemServices(
                                *r.controlModeLock, *r.eventQueue));
}

// ============================================================================
// Phase 4: Tasks — subsystem init cascades + RTOS task creation
// ============================================================================

void initTasks() {
  const auto &r = s_platform;

  // --- Motor ---
  Harness::debugPrint("Motor... ");
  Services::MotorSubsystemResult motorResult;
  if (!Services::initMotorSubsystem(*r.motorCmdQueue, *r.clock, *r.scheduler,
                                    *r.commandQueueLock, motorResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");

  // --- Encoder (optional — system operates in OPEN_LOOP if unavailable) ---
  Harness::debugPrint("Encoder... ");
  Services::EncoderSubsystemResult encResult;
  if (!Services::initEncoderSubsystem(*r.clock, *r.scheduler, encResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln(encResult.encoderAvailable ? "OK" : "SKIP");

  // --- Display ---
  Harness::debugPrint("Display... ");
  Services::DisplaySubsystemResult dispResult;
  if (!Services::initDisplaySubsystem(*r.clock, *r.scheduler, dispResult)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::debugPrintln("OK");

  // --- Comms (gathers interfaces from L3 init results) ---
  Harness::debugPrint("Comms... ");
  Services::CommsServiceDeps deps;
  deps.encoder = encResult.encoder;
  deps.encFilter = encResult.filterControl;
  deps.motorCtrl = motorResult.motorCtrl;
  deps.remoteDisplay = dispResult.remoteDisplay;
  if (!Services::initCommsSubsystem(deps, *r.clock, *r.scheduler)) {
    Harness::debugPrintln("FAIL!");
    while (true) {
    }
  }
  Harness::setDispatchStatsProvider(
      Scheduler::CommsTask_GetDispatchStatsInterface());
  Harness::debugPrintln("OK");
}

// ============================================================================
// Phase 5: Start scheduler — never returns
// ============================================================================

[[noreturn]] void start() {
  s_platform.scheduler->start();
  __builtin_unreachable();
}

} // namespace System
