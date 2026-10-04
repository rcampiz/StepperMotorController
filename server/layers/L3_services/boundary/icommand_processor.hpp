/**
 * @file boundary/icommandprocessor.hpp
 * @brief Consumer port — Services side of the Transport::ICommandProcessor harness connection
 *
 * Structurally identical to Transport::ICommandProcessor.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

// Forward declarations
struct AsyncEvent;
namespace Transport { class ITransport; }

namespace Services {

struct DispatchStats;

class ICommandProcessor {
public:
    virtual ~ICommandProcessor() = default;

    /** @brief Process incoming bytes from transport */
    virtual void process() = 0;

    /** @brief Check and revert baud rate if needed */
    virtual void checkBaudRevert() = 0;

    /**
     * @brief Format and send an async event over transport
     * @param transport Output transport
     * @param evt Event to send
     * @param seq Monotonic sequence number
     * @param ts_ms Timestamp in milliseconds
     */
    virtual void formatEvent(Transport::ITransport& transport, const AsyncEvent& evt,
                             uint32_t seq, uint32_t ts_ms) = 0;

    /** @brief Get command dispatch statistics */
    virtual void getDispatchStats(Services::DispatchStats& out) const = 0;
};

} // namespace Services
