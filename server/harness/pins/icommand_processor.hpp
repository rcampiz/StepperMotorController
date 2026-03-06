/**
 * @file icommand_processor.hpp
 * @brief Command processing interface — boundary contract
 *
 * Abstracts L2 command parser for Scheduler tasks.
 * Tasks call process()/checkBaudRevert()/formatEvent() without
 * seeing L2 protocol headers.
 */

#pragma once

#include <stdint.h>
#include "harness/pins/idispatch_stats.hpp"

// Forward declarations
struct AsyncEvent;
namespace Harness { class ITransport; }

namespace Harness {

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
    virtual void formatEvent(ITransport& transport, const AsyncEvent& evt,
                             uint32_t seq, uint32_t ts_ms) = 0;

    /** @brief Get command dispatch statistics */
    virtual void getDispatchStats(DispatchStats& out) const = 0;
};

} // namespace Harness
