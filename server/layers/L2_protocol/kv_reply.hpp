/**
 * @file kv_reply.hpp
 * @brief Replies made of named values (protocol 3: DRV:CAPS?, DRV:CFG:PHYS?, the physical
 *        DRV:CFG setters), the same keys in both formats, and real numbers without printf.
 *
 * newlib-nano's printf has no %f, so real numbers are formatted here: formatReal() gives up
 * to 6 significant digits with trailing zeros trimmed ("0.312", "5005.89", "572.917"),
 * formatFixed() a fixed number of decimals (telemetry speeds).
 *
 * A KvReply streams straight to the transport (no reply-sized buffer on the Comms stack):
 *   JSON          {"status":"ok","command":"<cmd>","data":{"key":value,...}}
 *   ASCII LINE    OK <head> key=value key=value ... [<note>]       (one line, up to 190 chars)
 *   ASCII LINES   key=value  (one per line), then OK <n>           (a terminated multi-line reply)
 * Keys carry their unit (`hold_a`, `accel_fsps2`); lists are JSON arrays of strings and
 * comma-separated in ASCII ("NONE" when empty). Validate before constructing: the JSON head
 * is on the wire once the object exists. Out of line (kv_reply.cpp): the replies call it a
 * lot, inlined it would cost flash for nothing.
 */
#pragma once

#include "harness/defs/harness_l1_l2.hpp"
#include <stddef.h>
#include <stdint.h>

namespace Protocol {

/// `v` with exactly `decimals` decimals (0..8), rounded half away from zero; magnitudes are
/// clamped to 1e9. Returns the length written.
size_t formatFixed(char* out, size_t size, double v, unsigned decimals);

/// `v` with `sig` significant digits (1..9, at most 8 decimals), no exponent, trailing
/// zeros and a trailing point trimmed: 0.078 -> "0.078", 9.906 -> "9.906",
/// 15609.741 -> "15609.7", 0.00390625 -> "0.00390625", 0 -> "0"
size_t formatReal(char* out, size_t size, double v, unsigned sig = 6);

class KvReply {
public:
    enum class Shape : uint8_t { LINE, LINES };

    /// `asciiHead` starts the ASCII LINE reply ("CURRENT"); ignored otherwise
    KvReply(Transport::ITransport& t, bool json, const char* command, Shape shape,
            const char* asciiHead = nullptr);

    /// A real number (formatReal, `sig` significant digits)
    void num(const char* key, double v, unsigned sig = 6);
    void integer(const char* key, long v);
    void flag(const char* key, bool v);
    /// A text value (JSON string): caller passes plain ASCII without quotes or backslashes
    void text(const char* key, const char* v);
    /// A list of names: JSON array of strings, ASCII comma-separated ("NONE" when empty)
    void list(const char* key, const char* const* items, size_t count);
    /// Close the reply. `asciiNote` ends an ASCII LINE reply ("(not saved; ...)")
    void end(const char* asciiNote = nullptr);

private:
    void sep();
    void field(const char* key, const char* value, bool quoted);
    void append(const char* s);

    Transport::ITransport& m_t;
    bool m_json;
    Shape m_shape;
    uint32_t m_count = 0;
    size_t m_len = 0;
    char m_line[192] = {};
};

} // namespace Protocol
