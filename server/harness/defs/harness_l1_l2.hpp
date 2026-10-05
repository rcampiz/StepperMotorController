/**
 * @file harness_l1_l2.hpp
 * @brief H_L1_L2 — the ONLY header through which L2 Protocol and L1 Transport see each other.
 *
 * Single definition: Transport::ITransport, owned by L1 (L1_transport/boundary/itransport.hpp).
 * The former Protocol::ITransport consumer-port duplicate and TransportAdapter are gone;
 * the parser holds a Transport::ITransport& directly.
 *
 * Signal
 *   transport : L1 provides, L2 calls down. Sync. Task context only (Comms task).
 *               Caller owns every buffer passed in; L1 copies out of the L5 RX ring.
 *               readByte() may block up to timeoutMs by polling the Platform clock.
 *
 * Observability: TracedTransport is applied here by connectL1L2(), never inside L1.
 * Rule 3 note: the bytes themselves enter L1 from L5 through Transport::IByteChannel
 * (the one documented layer skip); that connection is a separate init contract.
 */
#pragma once
#include "harness/defs/harness_signal.hpp"
#include "L1_transport/boundary/itransport.hpp"

namespace Harness {

struct L1L2Harness {
    Transport::ITransport& transport;
};

constexpr SignalDesc L1L2_SIGNALS[] = {
    { "Transport::ITransport", Provider::Lower, Flow::Down, Timing::Sync, ctx(Ctx::Task | Ctx::PreScheduler),
      "caller-owned buffers; L1 owns the RX ring (via L5) and TX polling" },
};
constexpr HarnessDesc L1L2_DESC = { "L1_L2", "Protocol", "Transport", L1L2_SIGNALS, 1 };

/// Build once (idempotent): wraps the raw L1 transport in its trace tap.
const L1L2Harness& connectL1L2(Transport::ITransport& rawTransport);

} // namespace Harness
