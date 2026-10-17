/**
 * @file harness_l5_l1_bytes.hpp
 * @brief X_L5_L1 — the ONE documented layer skip (rule 3): host command bytes, L5 -> L1
 *
 * Transport::IByteChannel is owned by L1 (L1_transport/boundary/ibyte_channel.hpp) and
 * implemented by two L5 objects: Board::UartHardware (USART2, ISR-fed ring buffer) and
 * Board::RttChannel (SEGGER RTT). L1 reads bytes from it; no other layer sees it.
 *
 * Optional RX wake: the USART2 ISR may wake the Comms task through the Platform task
 * notifier (power net); the composition root registers the handle via setCommsRxWake().
 *
 * Signals
 *   IByteChannel : L5 provides, L1 calls down. Sync read of an ISR-fed SPSC ring; caller-owned
 *                  read buffer; ring owned by the L5 singleton. The producer runs in the
 *                  USART2 ISR (NVIC 6, RTOS-safe) and may call ITaskNotifier::notifyFromISR.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "L1_transport/boundary/ibyte_channel.hpp"
#include "F_platform/boundary/itask_notifier.hpp"
#include "F_platform/boundary/iclock.hpp"
#include "harness/pins/comms_board.hpp"      // CommsChannel + the L5 init contract

namespace Harness {

struct L5L1ByteHarness {
    Transport::IByteChannel& channel;
};

/// L5 implements: route USART2 RX interrupts to a task notification (nullptr disables).
void setCommsRxWake(Platform::ITaskNotifier* notifier, Platform::TaskHandle task);
/// L5 implements: let a task writer sleep (1 ms steps) while the USART2 TX ring is full.
void setCommsTxClock(Platform::IClock* clock);

constexpr SignalDesc L5L1_SIGNALS[] = {
    { "Transport::IByteChannel", Provider::Lower, Flow::Up, Timing::AsyncFlag, ctx(Ctx::Task | Ctx::ISR),
      "ring owned by L5; ISR producer (NVIC 6); caller-owned read buffer; may notify the Comms task" },
};
constexpr HarnessDesc L5L1_DESC = { "L5_L1", "Transport", "Board", L5L1_SIGNALS, 1 };

/// Build once (idempotent): runs the L5 comms-board init contract for `type`. No taps.
/// Returns nullptr if the board could not be brought up.
const L5L1ByteHarness* connectL5L1Bytes(CommsChannel type);

} // namespace Harness
