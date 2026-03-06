/**
 * @file transport_init.hpp
 * @brief L1 transport init — wraps IByteChannel in ITransport
 *
 * Receives an already-constructed IByteChannel from higher layers.
 * L1 only deals with L1 types and harness interfaces.
 */

#pragma once

#include <stdint.h>

namespace Harness { class ITransport; class IClock; class IByteChannel; }

namespace Transport {

/**
 * @brief Transport type selection (determines which L1 wrapper to use)
 */
enum class TransportType : uint8_t {
    VCP_UART, // UartTransport (clock-based readByte timeout, setBaudRate)
    RTT       // RttTransport (polling readByte, no setBaudRate)
};

struct TransportResult {
    Harness::ITransport* transport = nullptr;
};

/**
 * @brief Initialize L1 transport around an existing byte channel
 *
 * @param out     Output struct with transport pointer
 * @param type    Transport type (selects UartTransport or RttTransport wrapper)
 * @param channel Byte channel (constructed by L4, passed through L3 → L2)
 * @param clock   Platform clock (for UartTransport timeout)
 * @return true on success
 */
bool initTransport(TransportResult& out, TransportType type,
                   Harness::IByteChannel& channel, Harness::IClock& clock);

} // namespace Transport
