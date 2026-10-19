/**
 * @file harness_l3_l4.hpp
 * @brief H_L3_L4 — the ONLY header through which L3 Services and L4 Drivers see each other.
 *
 * Single definitions, all owned by L4 (server/layers/L4_drivers/boundary):
 *   Drivers::IMotorDriver, ICanvas, IJoystick, IFlash, IEncoderHardware, IConfigStore.
 * L3 uses these types directly. The former Services:: duplicate ports and their
 * generated adapters are gone; the aliases at the bottom let L3 keep its unqualified
 * names (ICanvas, DriverSettings...) — they are the sheet entries of this harness.
 *
 * Every signal is Lower-provided and called downward, synchronously, in task context
 * (or pre-scheduler during init). SPI-backed devices block on the bus mutex, so none
 * may be used from ISR context. The one ISR entry on this boundary,
 * IEncoderHardware::indexISR, is reached from the L5 EXTI callback, never from L3.
 *
 * Observability: TracedMotorDriver is applied ONCE here by connectL3L4() and is a pure
 * observer; MotorController::periodicUpdate owns the telemetry writes.
 *
 * Buffer ownership: drivers own no caller memory. ICanvas stream buffers are caller-owned
 * until streamBitmapEnd(); IFlash read buffers are caller-owned; IConfigStore::read copies.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L4_drivers/boundary/icanvas.hpp"
#include "L4_drivers/boundary/ijoystick.hpp"
#include "L4_drivers/boundary/iflash.hpp"
#include "L4_drivers/boundary/iencoder_hardware.hpp"
#include "L4_drivers/boundary/iconfig_store.hpp"
#include "L4_drivers/boundary/idebug_access.hpp"
#include "F_platform/boundary/iclock.hpp"

namespace Harness {

struct L3L4Harness {
    Drivers::IMotorDriver*     motor;        ///< nullptr if the motor board failed init (fatal)
    Drivers::ICanvas*          canvas;       ///< nullptr if the LCD failed init (fatal for display)
    Drivers::IJoystick*        joystick;     ///< nullptr if absent
    Drivers::IFlash*           flash;        ///< nullptr if the NOR flash is absent/unresponsive
    Drivers::IEncoderHardware* encoderHw;    ///< nullptr if the encoder is absent (OPEN_LOOP)
    Drivers::IConfigStore*     configStore;  ///< persistent config blob (internal flash)
    Drivers::IDebugAccess*     debug;        ///< raw bring-up commands (nullptr if unavailable)
};

constexpr SignalDesc L3L4_SIGNALS[] = {
    { "Drivers::IMotorDriver",     Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::PreScheduler), "blocks on SPI1 mutex; traced tap is a pure observer" },
    { "Drivers::ICanvas",          Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::PreScheduler), "blocks on SPI1 mutex; stream buffers caller-owned until streamBitmapEnd; waitForVSync busy-waits <= 18 ms" },
    { "Drivers::IJoystick",        Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task),                     "optional" },
    { "Drivers::IFlash",           Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task),                     "blocks on SPI2 mutex; optional" },
    { "Drivers::IEncoderHardware", Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::ISR),          "indexISR only from the L5 EXTI callback; index fields published as a seqlock snapshot (readIndex)" },
    { "Drivers::IDebugAccess",     Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task),                     "raw bring-up commands pass-through to L5; gated by L3 DebugService" },
    { "Drivers::IConfigStore",     Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::PreScheduler), "erase/write block for the flash operation; read copies into caller buffer" },
};
constexpr HarnessDesc L3L4_DESC = { "L3_L4", "Services", "Drivers", L3L4_SIGNALS, 7 };

/// Build once (idempotent): runs the L4 init contracts (which pull L5 through H_L4_L5)
/// and applies the motor trace tap. Requires System::initPlatform to have run.
const L3L4Harness& connectL3L4(Platform::IClock& clock);

} // namespace Harness

// ---------------------------------------------------------------------------------------
// Sheet entries: the names L3 code uses for the L4 types it consumes. One definition (L4),
// visible to L3 under its own namespace through this harness only.
// ---------------------------------------------------------------------------------------
namespace Services {
using Drivers::IMotorDriver;
using Drivers::MotorStatus;
using Drivers::DriverCaps;
using Drivers::DriverSettings;
using Drivers::DriveLevels;
using Drivers::DriveMode;
using Drivers::ApplyResult;
using Drivers::LegacyDriverUnits;
using Drivers::ICanvas;
namespace Canvas = Drivers::Canvas;
using Drivers::IJoystick;
using Drivers::IFlash;
using Drivers::FlashJedecId;
using Drivers::IEncoderHardware;
using Drivers::IConfigStore;
using Drivers::IDebugAccess;
} // namespace Services
