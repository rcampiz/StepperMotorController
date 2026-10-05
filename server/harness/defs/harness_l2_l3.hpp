/**
 * @file harness_l2_l3.hpp
 * @brief H_L2_L3 — the ONLY header through which L2 Protocol and L3 Services see each other.
 *
 * Downward (L2 calls L3, Flow::Down, Sync, Comms task context):
 *   thirteen dispatcher interfaces, each a single definition owned by L3
 *   (L3_services/boundary/i*_dispatcher.hpp), bundled in L3Dispatchers. Arguments are by
 *   value or caller-owned buffers consumed before the call returns; every call returns
 *   Harness::ServiceStatus or plain data. New in the target state:
 *     ISystemDispatcher::getTelemetry  — serves the telemetry reads L2 used to take from
 *                                        Scheduler::telemetry() directly
 *     IDebugDispatcher                 — bring-up commands, gated by DBG:UNLOCK
 *
 * Upward (L3 calls L2, Flow::Up, Sync, Comms task context):
 *   Protocol::ICommandProcessor — owned by L2 (L2_protocol/boundary/icommand_processor.hpp).
 *   L2 owns the transport (H_L1_L2), the line buffer and all host-facing formatting.
 *
 * Shared PODs live in harness/pins/: ServiceStatus, AsyncEvent, TelemetrySnapshot,
 * DispatchStats, IResponseSink, EncoderFilterConfig, MotorDebugParams, UI types.
 *
 * ISR safety: none. Buffer ownership: the caller owns every pointer argument.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "harness/pins/dispatch_result.hpp"
#include "harness/pins/dispatch_stats.hpp"
#include "harness/pins/telemetry_types.hpp"
#include "harness/pins/async_event.hpp"
#include "harness/pins/iresponse_sink.hpp"
#include "L3_services/boundary/imotion_dispatcher.hpp"
#include "L3_services/boundary/imotor_config_dispatcher.hpp"
#include "L3_services/boundary/isafety_dispatcher.hpp"
#include "L3_services/boundary/iqueue_dispatcher.hpp"
#include "L3_services/boundary/icontrol_mode_dispatcher.hpp"
#include "L3_services/boundary/isupervisor_trim_dispatcher.hpp"
#include "L3_services/boundary/iencoder_dispatcher.hpp"
#include "L3_services/boundary/imotor_driver_dispatcher.hpp"
#include "L3_services/boundary/idisplay_dispatcher.hpp"
#include "L3_services/boundary/iflash_image_dispatcher.hpp"
#include "L3_services/boundary/isystem_dispatcher.hpp"
#include "L3_services/boundary/itrace_dispatcher.hpp"
#include "L3_services/boundary/idebug_dispatcher.hpp"
#include "L2_protocol/boundary/icommand_processor.hpp"

namespace Harness {

/// Downward bundle — what L3 offers L2. All mandatory.
struct L3Dispatchers {
    Services::IMotionDispatcher&         motion;
    Services::IMotorConfigDispatcher&    config;
    Services::ISafetyDispatcher&         safety;
    Services::IQueueDispatcher&          queue;
    Services::IControlModeDispatcher&    controlMode;
    Services::ISupervisorTrimDispatcher& supervisorTrim;
    Services::IEncoderDispatcher&        encoder;
    Services::IMotorDriverDispatcher&    motorDriver;
    Services::IDisplayDispatcher&        display;
    Services::IFlashImageDispatcher&     flash;
    Services::ISystemDispatcher&         system;
    Services::ITraceDispatcher&          trace;
    Services::IDebugDispatcher&          debug;
};

struct L2L3Harness {
    const L3Dispatchers&         dispatchers;   ///< Down: L2 -> L3
    Protocol::ICommandProcessor& processor;     ///< Up:   L3 -> L2
};

constexpr SignalDesc L2L3_SIGNALS[] = {
    { "Services::IMotionDispatcher",         Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "args by value" },
    { "Services::IMotorConfigDispatcher",    Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "args by value" },
    { "Services::ISafetyDispatcher",         Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "caller-owned fault string buffer" },
    { "Services::IQueueDispatcher",          Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "commands copied into the L3 CommandQueue" },
    { "Services::IControlModeDispatcher",    Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "" },
    { "Services::ISupervisorTrimDispatcher", Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "" },
    { "Services::IEncoderDispatcher",        Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "EncFilterParams by value; may restart DMA sampling" },
    { "Services::IMotorDriverDispatcher",    Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "setStepMode suspends the Motor task via Platform" },
    { "Services::IDisplayDispatcher",        Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "bitmap/stream bytes consumed before return; SPI1 mutex" },
    { "Services::IFlashImageDispatcher",     Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "chunk buffers caller-owned; SPI2 mutex" },
    { "Services::ISystemDispatcher",         Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "getTelemetry copies the snapshot under the telemetry mutex" },
    { "Services::ITraceDispatcher",          Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "entries copied out" },
    { "Services::IDebugDispatcher",          Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task), "locked until DBG:UNLOCK; output via the L2-owned IResponseSink" },
    { "Protocol::ICommandProcessor",         Provider::Upper, Flow::Up,   Timing::Sync, ctx(Ctx::Task), "L2 owns transport, line buffer, formatting; AsyncEvent by const ref" },
};
constexpr HarnessDesc L2L3_DESC = { "L2_L3", "Protocol", "Services", L2L3_SIGNALS, 14 };

/// Build once (idempotent). No taps on this harness.
const L2L3Harness& connectL2L3(const L3Dispatchers& dispatchers, Protocol::ICommandProcessor& processor);

} // namespace Harness
