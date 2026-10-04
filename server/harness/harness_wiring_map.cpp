/**
 * @file harness_wiring_map.cpp
 * @brief Composition root wiring — all cross-layer connections made here
 *
 * system_init.cpp sequences the init phases; this file makes the actual
 * connections (accessor set*() calls, traced wrapper insertion, cross-
 * subsystem dep passing).  No #ifdef ENABLE_INTERFACE_TRACE should
 * exist outside this file and harness/taps/.
 */

#include "harness/harness_wiring_map.hpp"

// Concrete types for wire*() functions
#include "F_platform/types/system_snapshot.hpp"
#include "F_platform/ui/ui_mode.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "L3_services/comms/comms_service_init.hpp"
#include "L3_services/motion/motor_service_init.hpp"
#include "L3_services/motion/encoder_service_init.hpp"
#include "L3_services/display/display_service_init.hpp"

// Accessor headers for wire*() functions
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/itask_stats.hpp"
#include "harness/adapters/task_stats_adapter.hpp"
#include "harness/pins/itelemetry.hpp"
#include "harness/pins/isnapshot_writer.hpp"
#include "harness/pins/iui_mode.hpp"
#include "harness/pins/idispatch_stats.hpp"
#include "harness/pins/platform_board.hpp"
#include "harness/pins/spi_init.hpp"

// Harness template (Ports & Adapters wiring)
#include "harness/harness_template.hpp"

// All adapters (each includes its own provider + consumer port headers)
#include "harness/adapters/spi_adapter.hpp"
#include "harness/adapters/gpio_adapter.hpp"
#include "harness/adapters/timer_counter_adapter.hpp"
#include "harness/adapters/encoder_sampler_adapter.hpp"
#include "harness/adapters/microsecond_clock_adapter.hpp"
#include "harness/adapters/motor_driver_adapter.hpp"
#include "harness/adapters/canvas_adapter.hpp"
#include "harness/adapters/joystick_adapter.hpp"
#include "harness/adapters/flash_adapter.hpp"
#include "harness/adapters/encoder_hardware_adapter.hpp"
#include "harness/adapters/clock_adapter.hpp"
#include "harness/adapters/lock_adapter.hpp"
#include "harness/adapters/scheduler_adapter.hpp"
#include "harness/adapters/task_stats_adapter.hpp"
#include "harness/adapters/critical_section_adapter.hpp"
#include "harness/adapters/transport_adapter.hpp"
#include "harness/adapters/command_processor_adapter.hpp"
#include "harness/adapters/encoder_adapter.hpp"
#include "harness/adapters/encoder_filter_control_adapter.hpp"
#include "harness/adapters/emergency_stop_adapter.hpp"
#include "harness/adapters/safety_actions_adapter.hpp"
#include "harness/adapters/motor_command_sink_adapter.hpp"
#include "harness/adapters/motor_task_control_adapter.hpp"
#include "harness/adapters/debug_commands_adapter.hpp"
#include "harness/adapters/remote_display_adapter.hpp"
#include "harness/adapters/dispatch_stats_adapter.hpp"
#include "harness/adapters/snapshot_writer_adapter.hpp"
#include "harness/adapters/telemetry_adapter.hpp"
#include "harness/adapters/ui_mode_provider_adapter.hpp"
#include "harness/adapters/motion_dispatcher_adapter.hpp"
#include "harness/adapters/motor_config_dispatcher_adapter.hpp"
#include "harness/adapters/safety_dispatcher_adapter.hpp"
#include "harness/adapters/queue_dispatcher_adapter.hpp"
#include "harness/adapters/control_mode_dispatcher_adapter.hpp"
#include "harness/adapters/supervisor_trim_dispatcher_adapter.hpp"
#include "harness/adapters/encoder_dispatcher_adapter.hpp"
#include "harness/adapters/motor_driver_dispatcher_adapter.hpp"
#include "harness/adapters/display_dispatcher_adapter.hpp"
#include "harness/adapters/flash_image_dispatcher_adapter.hpp"
#include "harness/adapters/system_dispatcher_adapter.hpp"
#include "harness/adapters/trace_dispatcher_adapter.hpp"
#include "harness/adapters/motor_control_service_adapter.hpp"
#include "harness/adapters/motor_event_receiver_adapter.hpp"
#include "harness/adapters/encoder_processing_adapter.hpp"
#include "harness/adapters/encoder_status_sink_adapter.hpp"
#include "harness/adapters/indicator_renderer_adapter.hpp"
#include "harness/adapters/flash_image_access_adapter.hpp"
#include "harness/adapters/trace_sink_adapter.hpp"

// Traced wrappers (all gated by ENABLE_INTERFACE_TRACE)
#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_lock.hpp"
#include "harness/taps/traced_spi_bus.hpp"
#include "harness/taps/traced_motor_driver.hpp"
#include "harness/taps/traced_transport.hpp"
#include "harness/taps/traced_encoder.hpp"
#include "harness/taps/traced_safety_actions.hpp"
#include "harness/taps/traced_command_sink.hpp"
#else
// Forward declarations (only used as template params when trace is off)
namespace Harness {
    class TracedSPIBus; class TracedMotorDriver; class TracedEncoder;
    class TracedTransport; class TracedSafetyActions; class TracedMotorCommandSink;
}
#endif

namespace Harness {

// ============================================================================
// Accessor wiring (wire*() functions — set global pointers, dep passing)
// ============================================================================

void wireHardwareClock() {
    setMicrosecondClock(Services::TickTimer_GetMicrosecondClock());
}

bool wireSPIManager(Platform::ILock& spi1Lock, Platform::ILock& spi2Lock) {
#ifdef ENABLE_INTERFACE_TRACE
    static TracedLock s_tracedSpi1Lock(spi1Lock);
    static TracedLock s_tracedSpi2Lock(spi2Lock);
    return initSPIManager(s_tracedSpi1Lock, s_tracedSpi2Lock);
#else
    return initSPIManager(spi1Lock, spi2Lock);
#endif
}

void wireServiceAccessors(const PlatformResources& r) {
    Platform::setTaskStatsProvider(r.taskStats);
    if (r.taskStats != nullptr) {
        static Harness::TaskStatsAdapter s_taskStatsAdapter(*r.taskStats);
        Scheduler::setTaskStatsProvider(&s_taskStatsAdapter);
    }

    static SystemSnapshot s_snapshot;
    s_snapshot.init(*r.telemetryLock, *r.clock);
    Scheduler::setTelemetryProvider(&s_snapshot);
    Scheduler::setSnapshotWriter(&s_snapshot);

    UI::g_uiMode.init(*r.uiModeLock);
    Scheduler::setUIModeProvider(&UI::g_uiMode);
}

void wireCommsDeps(Services::CommsServiceDeps& out,
                   const Services::MotorSubsystemResult& motor,
                   const Services::EncoderSubsystemResult& encoder,
                   const Services::DisplaySubsystemResult& display) {
    out.encoder = encoder.encoder;
    out.encFilter = encoder.filterControl;
    out.motorCtrl = motor.motorCtrl;
    out.remoteDisplay = display.remoteDisplay;
}

void wireDispatchStats() {
    Scheduler::setDispatchStatsProvider(
        Scheduler::CommsTask_GetDispatchStatsInterface());
}

// ============================================================================
// HARNESS CONNECTION TABLE — all cross-layer connections
// ============================================================================
//
// Every boundary connection in the firmware. Each line generates an
// adapt function: Consumer& adaptName(Provider& raw)
//
// HARNESS(Name, Provider, Consumer, Adapter, Tap)
//   Tap = TracedXxx (traced) or NoTap (passthrough)
//   To toggle tracing: change TracedXxx ↔ NoTap
//   Tap only active when ENABLE_INTERFACE_TRACE is defined.
//
#define HARNESS(Name, Provider, Consumer, Adapter, Tap) \
    Consumer& adapt##Name(Provider& raw) { \
        return harness<Provider, Consumer, Adapter, Tap>(raw); \
    }

// Board:: → Drivers::/Scheduler:: ─────────────────────────────────
HARNESS(SPIBus,           Board::ISPIBus,            Drivers::ISPIBus,              SPIAdapter,              TracedSPIBus)
HARNESS(GPIO,             Board::IGPIO,              Drivers::IGPIO,                GPIOAdapter,             NoTap)
HARNESS(TimerCounter,     Board::ITimerCounter,      Scheduler::ITimerCounter,      TimerCounterAdapter,     NoTap)
HARNESS(EncoderSampler,   Board::IEncoderSampler,    Scheduler::IEncoderSampler,    EncoderSamplerAdapter,   NoTap)
HARNESS(MicrosecondClock, Board::IMicrosecondClock,  Scheduler::IMicrosecondClock,  MicrosecondClockAdapter, NoTap)

// Drivers:: → Services::/Scheduler:: ──────────────────────────────
HARNESS(MotorDriver,      Drivers::IMotorDriver,     Services::IMotorDriver,        MotorDriverAdapter,      TracedMotorDriver)
HARNESS(Canvas,           Drivers::ICanvas,          Services::ICanvas,             CanvasAdapter,           NoTap)
HARNESS(Joystick,         Drivers::IJoystick,        Services::IJoystick,           JoystickAdapter,         NoTap)
HARNESS(Flash,            Drivers::IFlash,           Services::IFlash,              FlashAdapter,            NoTap)
HARNESS(EncoderHardware,  Drivers::IEncoderHardware, Scheduler::IEncoderHardware,   EncoderHardwareAdapter,  NoTap)

// Platform:: → Services::/Scheduler:: ─────────────────────────────
HARNESS(Clock,            Platform::IClock,            Services::IClock,              ClockAdapter,            NoTap)
HARNESS(Lock,             Platform::ILock,             Services::ILock,               LockAdapter,             NoTap)
HARNESS(Scheduler,        Platform::IScheduler,        Services::IScheduler,          SchedulerAdapter,        NoTap)
HARNESS(TaskStats,        Platform::ITaskStats,        Scheduler::ITaskStats,         TaskStatsAdapter,        NoTap)
HARNESS(CriticalSection,  Platform::ICriticalSection,  Scheduler::ICriticalSection,   CriticalSectionAdapter,  NoTap)
// Note: Platform::IQueue is template-parameterized — wired manually per instantiation

// Transport:: → Protocol:: ────────────────────────────────────────
HARNESS(Transport,        Transport::ITransport,     Protocol::ITransport,          TransportAdapter,        TracedTransport)

// Protocol:: → Services:: ─────────────────────────────────────────
HARNESS(CommandProcessor, Protocol::ICommandProcessor, Services::ICommandProcessor, CommandProcessorAdapter, NoTap)

// Scheduler:: → Services:: ────────────────────────────────────────
HARNESS(Encoder,              Scheduler::IEncoder,              Services::IEncoder,              EncoderAdapter,              TracedEncoder)
HARNESS(EncoderFilterControl, Scheduler::IEncoderFilterControl, Services::IEncoderFilterControl, EncoderFilterControlAdapter, NoTap)
HARNESS(EmergencyStop,        Scheduler::IEmergencyStop,        Services::IEmergencyStop,        EmergencyStopAdapter,        NoTap)
HARNESS(SafetyActions,        Scheduler::ISafetyActions,        Services::ISafetyActions,        SafetyActionsAdapter,        TracedSafetyActions)
HARNESS(MotorCommandSink,     Scheduler::IMotorCommandSink,     Services::IMotorCommandSink,     MotorCommandSinkAdapter,     TracedMotorCommandSink)
HARNESS(MotorTaskControl,     Scheduler::IMotorTaskControl,     Services::IMotorTaskControl,     MotorTaskControlAdapter,     NoTap)
HARNESS(DebugCommands,        Scheduler::IDebugCommands,        Services::IDebugCommands,        DebugCommandsAdapter,        NoTap)
HARNESS(RemoteDisplay,        Scheduler::IRemoteDisplay,        Services::IRemoteDisplay,        RemoteDisplayAdapter,        NoTap)
HARNESS(DispatchStats,        Scheduler::IDispatchStats,        Services::IDispatchStats,        DispatchStatsAdapter,        NoTap)
HARNESS(SnapshotWriter,       Scheduler::ISnapshotWriter,       Services::ISnapshotWriter,       SnapshotWriterAdapter,       NoTap)
HARNESS(Telemetry,            Scheduler::ITelemetry,            Services::ITelemetry,            TelemetryAdapter,            NoTap)
HARNESS(UIModeProvider,       Scheduler::IUIModeProvider,       Services::IUIModeProvider,       UIModeProviderAdapter,       NoTap)

// Services:: → Protocol:: (12 dispatchers) ────────────────────────
// Note: ICommandDispatcher is a composite of all 12 — no separate adapter
HARNESS(MotionDispatcher,          Services::IMotionDispatcher,          Protocol::IMotionDispatcher,          MotionDispatcherAdapter,          NoTap)
HARNESS(MotorConfigDispatcher,     Services::IMotorConfigDispatcher,     Protocol::IMotorConfigDispatcher,     MotorConfigDispatcherAdapter,     NoTap)
HARNESS(SafetyDispatcher,          Services::ISafetyDispatcher,          Protocol::ISafetyDispatcher,          SafetyDispatcherAdapter,          NoTap)
HARNESS(QueueDispatcher,           Services::IQueueDispatcher,           Protocol::IQueueDispatcher,           QueueDispatcherAdapter,           NoTap)
HARNESS(ControlModeDispatcher,     Services::IControlModeDispatcher,     Protocol::IControlModeDispatcher,     ControlModeDispatcherAdapter,     NoTap)
HARNESS(SupervisorTrimDispatcher,  Services::ISupervisorTrimDispatcher,  Protocol::ISupervisorTrimDispatcher,  SupervisorTrimDispatcherAdapter,  NoTap)
HARNESS(EncoderDispatcher,         Services::IEncoderDispatcher,         Protocol::IEncoderDispatcher,         EncoderDispatcherAdapter,         NoTap)
HARNESS(MotorDriverDispatcher,     Services::IMotorDriverDispatcher,     Protocol::IMotorDriverDispatcher,     MotorDriverDispatcherAdapter,     NoTap)
HARNESS(DisplayDispatcher,         Services::IDisplayDispatcher,         Protocol::IDisplayDispatcher,         DisplayDispatcherAdapter,         NoTap)
HARNESS(FlashImageDispatcher,      Services::IFlashImageDispatcher,      Protocol::IFlashImageDispatcher,      FlashImageDispatcherAdapter,      NoTap)
HARNESS(SystemDispatcher,          Services::ISystemDispatcher,          Protocol::ISystemDispatcher,          SystemDispatcherAdapter,          NoTap)
HARNESS(TraceDispatcher,           Services::ITraceDispatcher,           Protocol::ITraceDispatcher,           TraceDispatcherAdapter,           NoTap)

// Services:: → Scheduler:: (task dependencies) ───────────────────
HARNESS(MotorControlService, Services::IMotorControlService, Scheduler::IMotorControlService, MotorControlServiceAdapter, NoTap)
HARNESS(MotorEventReceiver,  Services::IMotorEventReceiver,  Scheduler::IMotorEventReceiver,  MotorEventReceiverAdapter,  NoTap)
HARNESS(EncoderProcessing,   Services::IEncoderProcessing,   Scheduler::IEncoderProcessing,   EncoderProcessingAdapter,   NoTap)
HARNESS(EncoderStatusSink,   Services::IEncoderStatusSink,   Scheduler::IEncoderStatusSink,   EncoderStatusSinkAdapter,   NoTap)
HARNESS(IndicatorRenderer,   Services::IIndicatorRenderer,   Scheduler::IIndicatorRenderer,   IndicatorRendererAdapter,   NoTap)
HARNESS(FlashImageAccess,    Services::IFlashImageAccess,    Scheduler::IFlashImageAccess,    FlashImageAccessAdapter,    NoTap)
HARNESS(TraceSink,           Services::ITraceSink,           Scheduler::ITraceSink,           TraceSinkAdapter,           NoTap)

#undef HARNESS

} // namespace Harness
