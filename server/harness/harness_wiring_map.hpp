#pragma once
// =================================================================
// Harness Wiring Map -- auto-validated by check_harness_wiring.py
// =================================================================
//
// The harness is a wiring harness: it connects layers through
// boundary interfaces, adapts mismatches, and taps for trace.
// This file documents every file in harness/ and its role.
//
// Tags parsed by check_harness_wiring.py:
//   "CONNECTOR:", "REDIRECT:", "OWNED:", "INIT:",
//   "ACCESSOR:", "TAP:", "TRACE:", "TOPLEVEL:"

// -----------------------------------------------------------------
// Interface Connectors (pins/)
// -----------------------------------------------------------------
// Thin redirects: harness/pins/<file> -> <layer>/boundary/<file>
// Consumers include the harness path; the real definition lives
// in the owning layer's boundary/ directory.

// Board:: (L5_board) -- 5 interfaces
// CONNECTOR: igpio.hpp                     -> L5_board/boundary/              [Board::IGPIO]
// CONNECTOR: ispi_bus.hpp                  -> L5_board/boundary/              [Board::ISPIBus]
// CONNECTOR: itimer_counter.hpp            -> L5_board/boundary/              [Board::ITimerCounter]
// CONNECTOR: iencoder_sampler.hpp          -> L5_board/boundary/              [Board::IEncoderSampler]
// CONNECTOR: imicrosecond_clock.hpp        -> L5_board/boundary/              [Board::IMicrosecondClock]

// Drivers:: (L4_drivers) -- 5 interfaces
// CONNECTOR: imotor_driver.hpp             -> L3_services/boundary/           [Services::IMotorDriver (consumer port)]
// CONNECTOR: icanvas.hpp                   -> L4_drivers/boundary/            [Drivers::ICanvas]
// CONNECTOR: ijoystick.hpp                 -> L4_drivers/boundary/            [Drivers::IJoystick]
// CONNECTOR: iflash.hpp                    -> L4_drivers/boundary/            [Drivers::IFlash]
// CONNECTOR: iencoder_hardware.hpp         -> L4_drivers/boundary/            [Drivers::IEncoderHardware]

// Platform:: (F_platform) -- 6 interfaces
// CONNECTOR: iclock.hpp                    -> F_platform/boundary/            [Platform::IClock]
// CONNECTOR: ilock.hpp                     -> F_platform/boundary/            [Platform::ILock]
// CONNECTOR: iqueue.hpp                    -> F_platform/boundary/            [Platform::IQueue]
// CONNECTOR: ischeduler.hpp                -> F_platform/boundary/            [Platform::IScheduler]
// CONNECTOR: itask_stats.hpp               -> F_platform/boundary/            [Platform::ITaskStats]
// CONNECTOR: icritical_section.hpp         -> F_platform/boundary/            [Platform::ICriticalSection]

// Transport:: (L1_transport) -- 2 interfaces
// CONNECTOR: ibyte_channel.hpp             -> L1_transport/boundary/          [Transport::IByteChannel]
// CONNECTOR: itransport.hpp                -> L1_transport/boundary/          [Transport::ITransport]

// Protocol:: (L2_protocol) -- 1 interface
// CONNECTOR: icommand_processor.hpp        -> L2_protocol/boundary/           [Protocol::ICommandProcessor]

// Scheduler:: (F_platform/tasks) -- 12 interfaces
// CONNECTOR: iencoder.hpp                  -> L3_services/boundary/           [Services::IEncoder (consumer port)]
// CONNECTOR: iencoder_filter_control.hpp   -> F_platform/tasks/boundary/      [Scheduler::IEncoderFilterControl]
// CONNECTOR: iemergency_stop.hpp           -> F_platform/tasks/boundary/      [Scheduler::IEmergencyStop]
// CONNECTOR: isafety_actions.hpp           -> F_platform/tasks/boundary/      [Scheduler::ISafetyActions]
// CONNECTOR: imotor_command_sink.hpp       -> F_platform/tasks/boundary/      [Scheduler::IMotorCommandSink]
// CONNECTOR: imotor_task_control.hpp       -> F_platform/tasks/boundary/      [Scheduler::IMotorTaskControl]
// CONNECTOR: idebug_commands.hpp           -> F_platform/tasks/boundary/      [Scheduler::IDebugCommands]
// CONNECTOR: iremote_display.hpp           -> F_platform/tasks/boundary/      [Scheduler::IRemoteDisplay]
// CONNECTOR: idispatch_stats.hpp           -> F_platform/tasks/boundary/      [Scheduler::IDispatchStats]
// CONNECTOR: isnapshot_writer.hpp          -> F_platform/tasks/boundary/      [Scheduler::ISnapshotWriter]
// CONNECTOR: itelemetry.hpp               -> F_platform/tasks/boundary/      [Scheduler::ITelemetry]
// CONNECTOR: iui_mode.hpp                  -> F_platform/tasks/boundary/      [Scheduler::IUIModeProvider]

// Services:: (L3_services) -- 20 interfaces
// CONNECTOR: icommand_dispatcher.hpp       -> L3_services/boundary/           [Services::ICommandDispatcher]
// CONNECTOR: imotion_dispatcher.hpp        -> L3_services/boundary/           [Services::IMotionDispatcher]
// CONNECTOR: imotor_config_dispatcher.hpp  -> L3_services/boundary/           [Services::IMotorConfigDispatcher]
// CONNECTOR: isafety_dispatcher.hpp        -> L3_services/boundary/           [Services::ISafetyDispatcher]
// CONNECTOR: iqueue_dispatcher.hpp         -> L3_services/boundary/           [Services::IQueueDispatcher]
// CONNECTOR: icontrol_mode_dispatcher.hpp  -> L3_services/boundary/           [Services::IControlModeDispatcher]
// CONNECTOR: isupervisor_trim_dispatcher.hpp -> L3_services/boundary/         [Services::ISupervisorTrimDispatcher]
// CONNECTOR: iencoder_dispatcher.hpp       -> L3_services/boundary/           [Services::IEncoderDispatcher]
// CONNECTOR: imotor_driver_dispatcher.hpp  -> L3_services/boundary/           [Services::IMotorDriverDispatcher]
// CONNECTOR: idisplay_dispatcher.hpp       -> L3_services/boundary/           [Services::IDisplayDispatcher]
// CONNECTOR: iflash_image_dispatcher.hpp   -> L3_services/boundary/           [Services::IFlashImageDispatcher]
// CONNECTOR: isystem_dispatcher.hpp        -> L3_services/boundary/           [Services::ISystemDispatcher]
// CONNECTOR: itrace_dispatcher.hpp         -> L3_services/boundary/           [Services::ITraceDispatcher]
// CONNECTOR: imotor_control_service.hpp    -> L3_services/boundary/           [Services::IMotorControlService]
// CONNECTOR: imotor_event_receiver.hpp     -> L3_services/boundary/           [Services::IMotorEventReceiver]
// CONNECTOR: iencoder_processing.hpp       -> L3_services/boundary/           [Services::IEncoderProcessing]
// CONNECTOR: iencoder_status_sink.hpp      -> L3_services/boundary/           [Services::IEncoderStatusSink]
// CONNECTOR: iindicator_renderer.hpp       -> L3_services/boundary/           [Services::IIndicatorRenderer]
// CONNECTOR: iflash_image_access.hpp       -> L3_services/boundary/           [Services::IFlashImageAccess]
// CONNECTOR: itrace_sink.hpp               -> L3_services/boundary/           [Services::ITraceSink]

// -----------------------------------------------------------------
// Non-Boundary Redirects (pins/)
// -----------------------------------------------------------------
// Thin redirects to non-boundary layer locations.

// REDIRECT: early_debug_board.hpp          -> F_platform/hw/early_debug_board.hpp          [early debug board config]
// REDIRECT: platform_board.hpp             -> F_platform/rtos/platform_board.hpp            [RTOS board config]
// REDIRECT: comms_task_init.hpp            -> F_platform/tasks/comms_task_init.hpp           [comms task init contract]
// REDIRECT: display_task_init.hpp          -> F_platform/tasks/display_task_init.hpp         [display task init contract]
// REDIRECT: encoder_task_init.hpp          -> F_platform/tasks/encoder_task_init.hpp         [encoder task init contract]
// REDIRECT: motor_task_init.hpp            -> F_platform/tasks/motor_task_init.hpp           [motor task init contract]
// REDIRECT: crc32.hpp                      -> F_util/crc32.hpp                              [CRC32 utility]

// -----------------------------------------------------------------
// Cross-Layer Types (pins/) -- harness-owned
// -----------------------------------------------------------------
// Shared between multiple layers; no single namespace owns them.

// OWNED: dispatch_result.hpp               [Harness::ServiceStatus enum + statusToString()]
// OWNED: dispatch_interfaces.hpp           [Harness::DispatchInterfaces aggregate struct]
// OWNED: async_event.hpp                   [EventType enum + AsyncEvent struct + EVT_MASK constants]
// OWNED: telemetry_types.hpp               [Protocol::TelemetrySnapshot + sub-structs]
// OWNED: ui_types.hpp                      [UI::UIMode, JoyDirection, JoyEvent, JoyEventCallback]
// OWNED: velocity_quality.hpp              [VelocityQuality enum (global scope)]
// OWNED: encoder_filter_config.hpp         [Harness::EncoderFilterConfig + ENC_FILT constants]
// OWNED: encoder_status.hpp                [Harness::EncoderStatus enum]
// OWNED: motor_conversions.hpp             [Harness::rawSpeedToStepsPerSec, signExtendAbsPos]
// OWNED: motor_debug_params.hpp            [Scheduler::MotorDebugParams struct]
// OWNED: itrace_context.hpp               [Harness::ServiceId, TaskId + trace context functions]
// OWNED: interrupt_guard.hpp               [InterruptGuard RAII class (ARM PRIMASK)]
// OWNED: ite_sync.hpp                      [Harness::initTearingEffectSync, waitForTearingEffect]
// OWNED: iinternal_flash.hpp               [Harness::internalFlashEraseSector, internalFlashWriteData]
// OWNED: exti_callbacks.hpp                [Harness::ExtiCallback + setExtiCallback, getExtiCallback]

// -----------------------------------------------------------------
// Init Contracts (pins/) -- harness-owned
// -----------------------------------------------------------------
// Declared in harness, implemented by the owning layer.
// Bridge layers at startup (composition root calls these).

// INIT: clock_board.hpp                    [L5 implements Harness::initClockBoard()]
// INIT: comms_board.hpp                    [L5 implements Harness::initCommsBoard()]
// INIT: display_board.hpp                  [L5 implements Harness::initLCDBoard, initJoystickBoard, initFlashBoard]
// INIT: encoder_board.hpp                  [L5 implements Harness::initEncoderBoard()]
// INIT: motor_board.hpp                    [L5 implements Harness::initMotorBoard()]
// INIT: spi_init.hpp                       [Harness::initSPIManager(ILock&, ILock&)]
// INIT: microsecond_timer_init.hpp         [L5 implements Harness::initMicrosecondTimer()]
// INIT: byte_channel_init.hpp              [L4 implements Drivers::initByteChannel()]
// INIT: motor_driver_init.hpp              [L4 implements Drivers::initMotorDriver()]
// INIT: display_driver_init.hpp            [L4 implements Drivers::initDisplayDrivers()]
// INIT: encoder_driver_init.hpp            [L4 implements Drivers::initEncoderDrivers()]
// INIT: transport_init.hpp                 [L1 implements Transport::initTransport()]
// INIT: comms_protocol_init.hpp            [L2 implements Protocol::initCommsProtocol()]

// -----------------------------------------------------------------
// Global Accessors (pins/*.cpp) -- composition-root wiring
// -----------------------------------------------------------------
// Static pointer + getter/setter pairs. The .hpp declares, the .cpp stores.

// ACCESSOR: imicrosecond_clock.cpp         [Harness::setMicrosecondClock / microsecondClock]
// ACCESSOR: itrace_context.cpp             [Harness::setTraceTaskId/ServiceId, getTraceTaskId/ServiceId]
// ACCESSOR: exti_callbacks.cpp             [Harness::setExtiCallback / getExtiCallback]
// ACCESSOR: idispatch_stats.cpp            [Scheduler::setDispatchStatsProvider / dispatchStats]
// ACCESSOR: isnapshot_writer.cpp           [Scheduler::setSnapshotWriter / snapshotWriter]
// ACCESSOR: itelemetry.cpp                 [Scheduler::setTelemetryProvider / telemetry]
// ACCESSOR: iui_mode.cpp                   [Scheduler::setUIModeProvider / uiMode]
// ACCESSOR: itask_stats.cpp               [Platform::setTaskStatsProvider / taskStatsProvider]

// -----------------------------------------------------------------
// Traced Wrappers (taps/)
// -----------------------------------------------------------------
// Decorator pattern: wraps a real interface, logs boundary crossings
// via Harness::ITrace::log(). Gated by #ifdef ENABLE_INTERFACE_TRACE.

// TAP: traced_spi_bus.hpp                  wraps Board::ISPIBus                [L4_L5_SPI]
// TAP: traced_transport.hpp                wraps Transport::ITransport         [L1_L2_TRANSPORT]
// TAP: traced_motor_driver.hpp             wraps Drivers::IMotorDriver         [L3_L4_MOTOR]
// TAP: traced_encoder.hpp                  wraps Scheduler::IEncoder           [L3_L4_ENCODER]
// TAP: traced_safety_actions.hpp           wraps Scheduler::ISafetyActions     [L3_F_SAFETY]
// TAP: traced_lock.hpp                     wraps Platform::ILock               [L5_F_LOCK]
// TAP: traced_command_sink.hpp             wraps Scheduler::IMotorCommandSink  [L3_CMD_SINK]

// -----------------------------------------------------------------
// Trace Infrastructure (trace/)
// -----------------------------------------------------------------
// Ring buffer + RTT output for interface boundary tracing.

// TRACE: interface_trace.hpp               [Harness::ITrace:: Boundary enum, log/init/setSink]
// TRACE: interface_trace.cpp               [Harness::ITrace:: implementation]

// -----------------------------------------------------------------
// Top-Level Harness Files (harness/)
// -----------------------------------------------------------------
// Composition-root level: can freely include CMSIS/board headers.

// TOPLEVEL: debug_commands.hpp             [DebugCommandHandler : Scheduler::IDebugCommands]
// TOPLEVEL: debug_commands.cpp             [DebugCommandHandler implementation]
// TOPLEVEL: harness_wiring_map.cpp         [Composition root wiring implementation]
// TOPLEVEL: harness_template.hpp           [harness<> template + NoTap + TapApplier]

// -----------------------------------------------------------------
// Composition Root Wiring Functions
// -----------------------------------------------------------------
// Called by system_init.cpp at the right moment. Each function makes
// the actual connections (set*() calls, adapter insertion, dep passing).
// Implementation in harness_wiring_map.cpp.
//
// WIRING: wireHardwareClock        [setMicrosecondClock <- TickTimer]
// WIRING: wireSPIManager           [initSPIManager + TracedLock insertion]
// WIRING: wireServiceAccessors     [setTaskStats, setTelemetry, setSnapshot, setUIMode]
// WIRING: wireCommsDeps            [encoder->comms, motor->comms, display->comms]
// WIRING: wireDispatchStats        [setDispatchStatsProvider <- CommsTask]

// -----------------------------------------------------------------
// Harness Connections (Ports & Adapters)
// -----------------------------------------------------------------
// The connection table is the single source of truth for every
// boundary connection. See harness_wiring_map.cpp for the full table.
//
// Format: HARNESS(Name, Provider, Consumer, Adapter, Tap)
//   Tap = TracedXxx (traced) or NoTap (passthrough)
//
// Note: Platform::IQueue is template-parameterized — wired manually.

// Forward declarations for all types used in the connection table
namespace Board {
    class ISPIBus; class IGPIO; class ITimerCounter;
    class IEncoderSampler; class IMicrosecondClock;
}
namespace Drivers {
    class IMotorDriver; class ICanvas; class IJoystick;
    class IFlash; class IEncoderHardware;
    class ISPIBus; class IGPIO;  // consumer ports
}
namespace Platform {
    class IClock; class ILock; class IScheduler;
    class ITaskStats; class ICriticalSection;
}
namespace Transport { class ITransport; }
namespace Protocol {
    class ICommandProcessor; class ITransport;
    class ICommandDispatcher; class IMotionDispatcher;
    class IMotorConfigDispatcher; class ISafetyDispatcher;
    class IQueueDispatcher; class IControlModeDispatcher;
    class ISupervisorTrimDispatcher; class IEncoderDispatcher;
    class IMotorDriverDispatcher; class IDisplayDispatcher;
    class IFlashImageDispatcher; class ISystemDispatcher;
    class ITraceDispatcher;
}
namespace Scheduler {
    class IEncoder; class IEncoderFilterControl; class IEmergencyStop;
    class ISafetyActions; class IMotorCommandSink; class IMotorTaskControl;
    class IDebugCommands; class IRemoteDisplay; class IDispatchStats;
    class ISnapshotWriter; class ITelemetry; class IUIModeProvider;
    class ITimerCounter; class IEncoderSampler; class IMicrosecondClock;
    class IEncoderHardware; class ITaskStats; class ICriticalSection;
    class IMotorControlService; class IMotorEventReceiver;
    class IEncoderProcessing; class IEncoderStatusSink;
    class IIndicatorRenderer; class IFlashImageAccess; class ITraceSink;
}
namespace Services {
    class IMotorDriver; class ICanvas; class IJoystick; class IFlash;
    class IClock; class ILock; class IScheduler; class ICommandProcessor;
    class IEncoder; class IEncoderFilterControl; class IEmergencyStop;
    class ISafetyActions; class IMotorCommandSink; class IMotorTaskControl;
    class IDebugCommands; class IRemoteDisplay; class IDispatchStats;
    class ISnapshotWriter; class ITelemetry; class IUIModeProvider;
    class ICommandDispatcher; class IMotionDispatcher;
    class IMotorConfigDispatcher; class ISafetyDispatcher;
    class IQueueDispatcher; class IControlModeDispatcher;
    class ISupervisorTrimDispatcher; class IEncoderDispatcher;
    class IMotorDriverDispatcher; class IDisplayDispatcher;
    class IFlashImageDispatcher; class ISystemDispatcher;
    class ITraceDispatcher; class IMotorControlService;
    class IMotorEventReceiver; class IEncoderProcessing;
    class IEncoderStatusSink; class IIndicatorRenderer;
    class IFlashImageAccess; class ITraceSink;
    struct MotorSubsystemResult;
    struct EncoderSubsystemResult;
    struct DisplaySubsystemResult;
    struct CommsServiceDeps;
}

namespace Harness {

struct PlatformResources;

// --- Accessor wiring (set*() calls, dep passing) ---
void wireHardwareClock();
bool wireSPIManager(Platform::ILock& spi1Lock, Platform::ILock& spi2Lock);
void wireServiceAccessors(const PlatformResources& platform);
void wireCommsDeps(Services::CommsServiceDeps& out,
                   const Services::MotorSubsystemResult& motor,
                   const Services::EncoderSubsystemResult& encoder,
                   const Services::DisplaySubsystemResult& display);
void wireDispatchStats();

// --- Harness adapt function declarations ---
// Board:: → Drivers::/Scheduler::
Drivers::ISPIBus&              adaptSPIBus(Board::ISPIBus& raw);
Drivers::IGPIO&                adaptGPIO(Board::IGPIO& raw);
Scheduler::ITimerCounter&      adaptTimerCounter(Board::ITimerCounter& raw);
Scheduler::IEncoderSampler&    adaptEncoderSampler(Board::IEncoderSampler& raw);
Scheduler::IMicrosecondClock&  adaptMicrosecondClock(Board::IMicrosecondClock& raw);

// Drivers:: → Services::/Scheduler::
Services::IMotorDriver&        adaptMotorDriver(Drivers::IMotorDriver& raw);
Services::ICanvas&             adaptCanvas(Drivers::ICanvas& raw);
Services::IJoystick&           adaptJoystick(Drivers::IJoystick& raw);
Services::IFlash&              adaptFlash(Drivers::IFlash& raw);
Scheduler::IEncoderHardware&   adaptEncoderHardware(Drivers::IEncoderHardware& raw);

// Platform:: → Services::/Scheduler::
Services::IClock&              adaptClock(Platform::IClock& raw);
Services::ILock&               adaptLock(Platform::ILock& raw);
Services::IScheduler&          adaptScheduler(Platform::IScheduler& raw);
Scheduler::ITaskStats&         adaptTaskStats(Platform::ITaskStats& raw);
Scheduler::ICriticalSection&   adaptCriticalSection(Platform::ICriticalSection& raw);

// Transport:: → Protocol::
Protocol::ITransport&          adaptTransport(Transport::ITransport& raw);

// Protocol:: → Services::
Services::ICommandProcessor&   adaptCommandProcessor(Protocol::ICommandProcessor& raw);

// Scheduler:: → Services::
Services::IEncoder&              adaptEncoder(Scheduler::IEncoder& raw);
Services::IEncoderFilterControl& adaptEncoderFilterControl(Scheduler::IEncoderFilterControl& raw);
Services::IEmergencyStop&        adaptEmergencyStop(Scheduler::IEmergencyStop& raw);
Services::ISafetyActions&        adaptSafetyActions(Scheduler::ISafetyActions& raw);
Services::IMotorCommandSink&     adaptMotorCommandSink(Scheduler::IMotorCommandSink& raw);
Services::IMotorTaskControl&     adaptMotorTaskControl(Scheduler::IMotorTaskControl& raw);
Services::IDebugCommands&        adaptDebugCommands(Scheduler::IDebugCommands& raw);
Services::IRemoteDisplay&        adaptRemoteDisplay(Scheduler::IRemoteDisplay& raw);
Services::IDispatchStats&        adaptDispatchStats(Scheduler::IDispatchStats& raw);
Services::ISnapshotWriter&       adaptSnapshotWriter(Scheduler::ISnapshotWriter& raw);
Services::ITelemetry&            adaptTelemetry(Scheduler::ITelemetry& raw);
Services::IUIModeProvider&       adaptUIModeProvider(Scheduler::IUIModeProvider& raw);

// Services:: → Protocol:: (12 dispatchers)
Protocol::IMotionDispatcher&          adaptMotionDispatcher(Services::IMotionDispatcher& raw);
Protocol::IMotorConfigDispatcher&     adaptMotorConfigDispatcher(Services::IMotorConfigDispatcher& raw);
Protocol::ISafetyDispatcher&          adaptSafetyDispatcher(Services::ISafetyDispatcher& raw);
Protocol::IQueueDispatcher&           adaptQueueDispatcher(Services::IQueueDispatcher& raw);
Protocol::IControlModeDispatcher&     adaptControlModeDispatcher(Services::IControlModeDispatcher& raw);
Protocol::ISupervisorTrimDispatcher&  adaptSupervisorTrimDispatcher(Services::ISupervisorTrimDispatcher& raw);
Protocol::IEncoderDispatcher&         adaptEncoderDispatcher(Services::IEncoderDispatcher& raw);
Protocol::IMotorDriverDispatcher&     adaptMotorDriverDispatcher(Services::IMotorDriverDispatcher& raw);
Protocol::IDisplayDispatcher&         adaptDisplayDispatcher(Services::IDisplayDispatcher& raw);
Protocol::IFlashImageDispatcher&      adaptFlashImageDispatcher(Services::IFlashImageDispatcher& raw);
Protocol::ISystemDispatcher&          adaptSystemDispatcher(Services::ISystemDispatcher& raw);
Protocol::ITraceDispatcher&           adaptTraceDispatcher(Services::ITraceDispatcher& raw);

// Services:: → Scheduler:: (task dependencies)
Scheduler::IMotorControlService& adaptMotorControlService(Services::IMotorControlService& raw);
Scheduler::IMotorEventReceiver&  adaptMotorEventReceiver(Services::IMotorEventReceiver& raw);
Scheduler::IEncoderProcessing&   adaptEncoderProcessing(Services::IEncoderProcessing& raw);
Scheduler::IEncoderStatusSink&   adaptEncoderStatusSink(Services::IEncoderStatusSink& raw);
Scheduler::IIndicatorRenderer&   adaptIndicatorRenderer(Services::IIndicatorRenderer& raw);
Scheduler::IFlashImageAccess&    adaptFlashImageAccess(Services::IFlashImageAccess& raw);
Scheduler::ITraceSink&           adaptTraceSink(Services::ITraceSink& raw);

} // namespace Harness
