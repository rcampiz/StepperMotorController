/**
 * @file event_codec.hpp
 * @brief Wire format encoding for async events
 *
 * Separated from EventService per CODE_PHILOSOPHY: service = policy/queue,
 * codec = wire formatting. See docs/PROTOCOL_EVENTS_V1.md.
 */

#pragma once
#include "F_platform/types/async_event_types.hpp"
#include "L2_protocol/command_parser.hpp"

namespace Comms::EventCodec {

/**
 * @brief Format an event as ASCII and write to transport
 * @param transport Transport to write to
 * @param evt Event to format
 * @param statusReg Raw STATUS register (for detail extraction)
 *
 * Output: !FAULT status=0x1234 detail=OCD\r\n
 */
void formatAscii(ITransport& transport, const AsyncEvent& evt, uint32_t seq);

/**
 * @brief Format an event as JSON and write to transport
 * @param transport Transport to write to
 * @param evt Event to format
 * @param seq Monotonic sequence number
 * @param ts_ms Tick count at send time
 *
 * Output: {"kind":"event","type":"FAULT","seq":1,"ts_ms":12345,"data":{"status":4660,"detail":"OCD"}}\r\n
 */
void formatJson(ITransport& transport, const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms);

/**
 * @brief Build the detail string for a fault event
 * @param statusReg Raw STATUS register
 * @param buf Output buffer
 * @param bufSize Buffer size
 */
void faultDetail(uint16_t statusReg, char* buf, size_t bufSize);

/**
 * @brief Build the detail string for a stall event
 * @param statusReg Raw STATUS register
 * @param buf Output buffer
 * @param bufSize Buffer size
 */
void stallDetail(uint16_t statusReg, char* buf, size_t bufSize);

} // namespace Comms::EventCodec
