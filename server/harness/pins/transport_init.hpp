/**
 * @file transport_init.hpp
 * @brief Init contract: L1 builds the transport over the L5 -> L1 byte-channel exception
 */
#pragma once
#include <stdint.h>

namespace Platform { class IClock; }
namespace Transport { class ITransport; }

namespace Transport {

enum class TransportType : uint8_t {
    VCP_UART, // UartTransport over Board::UartHardware (clock-based readByte timeout, setBaudRate)
    RTT       // RttTransport over Board::RttChannel (polling readByte, no setBaudRate)
};

struct TransportResult {
    Transport::ITransport* transport = nullptr;
};

/// L1 obtains its byte channel through harness_l5_l1_bytes.hpp (rule 3) and wraps it.
bool initTransport(TransportResult& out, TransportType type, Platform::IClock& clock);

} // namespace Transport
