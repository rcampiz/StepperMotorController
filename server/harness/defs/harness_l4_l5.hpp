/**
 * @file harness_l4_l5.hpp
 * @brief H_L4_L5 — the ONLY header through which L4 Drivers and L5 Board see each other.
 *
 * Single definitions, all owned by L5 (server/layers/L5_board/boundary):
 *   Board::ISPIBus, IGPIO, ITimerCounter, IEncoderSampler, ITearingSync, IInternalFlash,
 *   IStepPulseTimer.
 * L4 drivers take these types directly; there are no consumer-port duplicates or adapters.
 *
 * Because one device needs several pins, the harness is a set of per-device pin bundles
 * (this is what an Altium harness is for). L5 implements the init contracts that fill a
 * bundle; connectL4L5() (harness_connect.cpp) calls them once, applies trace taps, and
 * hands L4 the finished L4L5Harness. L4 init code calls connectL4L5() and picks its bundle.
 *
 * Signals (all Lower-provided, L4 calls down, synchronous):
 *   ISPIBus      : task or pre-scheduler; lock()/unlock() use a Platform mutex -> never ISR.
 *   IGPIO        : any context (register write/read).
 *   ITimerCounter: task context; 16-bit TIM4 count, wraps.
 *   IEncoderSampler: control in task context; its DMA ring is written by hardware (async).
 *   ITearingSync : task context; bounded busy-wait for the EXTI0 flag (ISR at NVIC 4, no RTOS).
 *   IInternalFlash: task or pre-scheduler; blocks for the flash operation.
 *   IStepPulseTimer: start() in task context, stop() from any context; the pulse handler
 *                  (L4's ramp) runs in the timer interrupt. For STEP/DIR drivers; only the
 *                  virtual Nucleo provides one so far (driver boundary step 5).
 *   registerExti : pre-scheduler; the callback runs in ISR context at NVIC priority 5.
 *
 * Buffer ownership: SPIManager owns the bus objects; the DMA ring is owned by L5 and only
 * read by L4; GPIO objects are L5 statics that live for the program.
 */
#pragma once
#include <stdint.h>
#include "harness/defs/harness_signal.hpp"
#include "L5_board/boundary/ispi_bus.hpp"
#include "L5_board/boundary/igpio.hpp"
#include "L5_board/boundary/itimer_counter.hpp"
#include "L5_board/boundary/iencoder_sampler.hpp"
#include "L5_board/boundary/itearing_sync.hpp"
#include "L5_board/boundary/iinternal_flash.hpp"
#include "L5_board/boundary/iboard_debug.hpp"
#include "L5_board/boundary/istep_pulse_timer.hpp"

namespace Harness {

// ---------------------------------------------------------------------------
// Pin bundles (one per device). Filled by the L5 init contracts below.
// ---------------------------------------------------------------------------
struct MotorPins {
    Board::IGPIO*   cs      = nullptr;
    Board::IGPIO*   stbyRst = nullptr;
    Board::IGPIO*   busy    = nullptr;
    Board::IGPIO*   flag    = nullptr;
    Board::ISPIBus* spi     = nullptr;   ///< SPI1, shared with the LCD
};

struct LCDPins {
    Board::IGPIO*       cs     = nullptr;
    Board::IGPIO*       dc     = nullptr;
    Board::IGPIO*       nreset = nullptr;
    Board::IGPIO*       te     = nullptr;   ///< TE level (input)
    Board::ISPIBus*     spi    = nullptr;   ///< SPI1, shared with the motor
    Board::ITearingSync* teSync = nullptr;  ///< TE edge wait (EXTI0)
};

struct JoystickPins {
    Board::IGPIO* left   = nullptr;
    Board::IGPIO* right  = nullptr;
    Board::IGPIO* up     = nullptr;
    Board::IGPIO* down   = nullptr;
    Board::IGPIO* center = nullptr;
};

struct FlashPins {
    Board::IGPIO*   cs  = nullptr;
    Board::ISPIBus* spi = nullptr;      ///< SPI2
};

struct EncoderPins {
    Board::ITimerCounter*  timer   = nullptr;   ///< TIM4 quadrature counter
    Board::IEncoderSampler* sampler = nullptr;  ///< TIM3 + DMA1 sampler of TIM4->CNT
    uint8_t                indexExtiLine = 0xFF; ///< EXTI line of the index pulse (PC9 -> 9)
};

using ExtiCallback = void (*)();

// ---------------------------------------------------------------------------
// Init contracts: declared here, implemented by L5 (L5_board/init/*.cpp).
// Return false when the board cannot be brought up (bundle left untouched).
// ---------------------------------------------------------------------------
bool initMotorBoard(MotorPins& pins);
bool initLCDBoard(LCDPins& pins);
bool initJoystickBoard(JoystickPins& pins);
bool initFlashBoard(FlashPins& pins);
bool initEncoderBoard(EncoderPins& pins);
Board::IInternalFlash& internalFlash();
Board::IBoardDebug& boardDebug();

// ---------------------------------------------------------------------------
// The harness: what L4 receives. Optional boards are nullptr when absent.
// ---------------------------------------------------------------------------
struct L4L5Harness {
    const MotorPins*       motor;
    const LCDPins*         lcd;
    const JoystickPins*    joystick;
    const FlashPins*       flash;
    const EncoderPins*     encoder;
    Board::IInternalFlash* internalFlash;
    Board::IBoardDebug*    boardDebug;
    void (*registerExti)(uint8_t line, ExtiCallback cb);   ///< pre-scheduler only
};

constexpr SignalDesc L4L5_SIGNALS[] = {
    { "Board::ISPIBus (SPI1 motor+lcd, SPI2 flash)", Provider::Lower, Flow::Down, Timing::Sync,      ctx(Ctx::Task | Ctx::PreScheduler),            "bus owned by SPIManager; per-bus Platform mutex; caller buffers" },
    { "Board::IGPIO (per pin)",                      Provider::Lower, Flow::Down, Timing::Sync,      ctx(Ctx::Task | Ctx::ISR | Ctx::PreScheduler), "L5 statics" },
    { "Board::ITimerCounter (TIM4)",                 Provider::Lower, Flow::Down, Timing::Sync,      ctx(Ctx::Task),                                "16-bit, wraps" },
    { "Board::IEncoderSampler (TIM3+DMA1)",          Provider::Lower, Flow::Down, Timing::AsyncFlag, ctx(Ctx::Task),                                "256-entry DMA ring owned by L5; reader keeps its own index" },
    { "Board::ITearingSync (EXTI0)",                 Provider::Lower, Flow::Down, Timing::AsyncFlag, ctx(Ctx::Task),                                "flag set at NVIC 4 (no RTOS calls); bounded 18 ms wait" },
    { "Board::IInternalFlash",                       Provider::Lower, Flow::Down, Timing::Sync,      ctx(Ctx::Task | Ctx::PreScheduler),            "blocks for erase/program" },
    { "Board::IBoardDebug",                          Provider::Lower, Flow::Down, Timing::Sync,      ctx(Ctx::Task),                           "register-level bring-up commands; text via IResponseSink" },
    { "registerExti (index pulse)",                  Provider::Lower, Flow::Up,   Timing::AsyncFlag, ctx(Ctx::ISR),                                 "callback table in harness; runs at NVIC 5 (RTOS-safe)" },
    { "Board::IStepPulseTimer (STEP/DIR drivers)",   Provider::Lower, Flow::Down, Timing::AsyncFlag, ctx(Ctx::Task | Ctx::ISR),                     "L4's pulse handler runs in the timer ISR (integer only); no board timer yet, virtual Nucleo only" },
};
constexpr HarnessDesc L4L5_DESC = { "L4_L5", "Drivers", "Board", L4L5_SIGNALS, 9 };

/// Build once (idempotent): calls the L5 init contracts, applies the SPI trace taps.
/// Requires the SPI manager and clocks to be initialised (System::initPlatform).
const L4L5Harness& connectL4L5();

} // namespace Harness
