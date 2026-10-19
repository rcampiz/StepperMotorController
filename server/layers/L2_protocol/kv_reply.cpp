/**
 * @file kv_reply.cpp
 * @brief Named-value replies and real-number formatting (see kv_reply.hpp)
 */

#include "L2_protocol/kv_reply.hpp"
#include <stdio.h>
#include <string.h>

namespace Protocol {

size_t formatFixed(char* out, size_t size, double v, unsigned decimals)
{
    if (out == nullptr || size == 0) return 0;
    if (decimals > 8) decimals = 8;
    if (!(v == v)) v = 0.0;                                  // NaN
    const bool neg = v < 0.0;
    double m = neg ? -v : v;
    if (m > 1e9) m = 1e9;
    uint32_t scale = 1;
    for (unsigned i = 0; i < decimals; ++i) scale *= 10u;
    const uint64_t q = static_cast<uint64_t>(m * static_cast<double>(scale) + 0.5);
    const unsigned long whole = static_cast<unsigned long>(q / scale);
    const unsigned long frac = static_cast<unsigned long>(q % scale);
    const bool showNeg = neg && q != 0;
    int n;
    if (decimals == 0) {
        n = snprintf(out, size, "%s%lu", showNeg ? "-" : "", whole);
    } else {
        n = snprintf(out, size, "%s%lu.%0*lu", showNeg ? "-" : "", whole, static_cast<int>(decimals), frac);
    }
    if (n < 0) { out[0] = '\0'; return 0; }
    return (static_cast<size_t>(n) < size) ? static_cast<size_t>(n) : size - 1;
}

size_t formatReal(char* out, size_t size, double v, unsigned sig)
{
    if (out == nullptr || size == 0) return 0;
    if (sig < 1) sig = 1;
    if (sig > 9) sig = 9;
    const double a = (v < 0.0) ? -v : v;
    int decimals;
    if (a != 0.0 && a < 1.0) {                               // leading zeros after the point
        double f = a;
        decimals = static_cast<int>(sig);
        while (f < 0.1 && decimals < 8) { f *= 10.0; ++decimals; }
    } else {
        int digitsBeforePoint = 1;
        double m = a;
        while (m >= 10.0 && digitsBeforePoint < 10) { m /= 10.0; ++digitsBeforePoint; }
        decimals = static_cast<int>(sig) - digitsBeforePoint;
    }
    if (decimals < 0) decimals = 0;
    if (decimals > 8) decimals = 8;
    size_t n = formatFixed(out, size, v, static_cast<unsigned>(decimals));
    if (strchr(out, '.') != nullptr) {
        while (n > 0 && out[n - 1] == '0') out[--n] = '\0';
        if (n > 0 && out[n - 1] == '.') out[--n] = '\0';
    }
    if (n == 2 && out[0] == '-' && out[1] == '0') { out[0] = '0'; out[1] = '\0'; n = 1; }
    return n;
}

KvReply::KvReply(Transport::ITransport& t, bool json, const char* command, Shape shape,
                 const char* asciiHead)
    : m_t(t), m_json(json), m_shape(shape)
{
    if (m_json) {
        m_t.print("{\"status\":\"ok\",\"command\":\"");
        m_t.print(command != nullptr ? command : "");         // command names need no escaping
        m_t.print("\",\"data\":{");
    } else if (m_shape == Shape::LINE) {
        append("OK ");
        append(asciiHead != nullptr ? asciiHead : "");
    }
}

void KvReply::num(const char* key, double v, unsigned sig)
{
    char b[24];
    formatReal(b, sizeof(b), v, sig);
    field(key, b, false);
}

void KvReply::integer(const char* key, long v)
{
    char b[16];
    snprintf(b, sizeof(b), "%ld", v);
    field(key, b, false);
}

void KvReply::flag(const char* key, bool v)
{
    field(key, m_json ? (v ? "true" : "false") : (v ? "1" : "0"), false);
}

void KvReply::text(const char* key, const char* v)
{
    field(key, v != nullptr ? v : "", true);
}

void KvReply::list(const char* key, const char* const* items, size_t count)
{
    if (m_json) {
        sep();
        m_t.print("\"");
        m_t.print(key);
        m_t.print("\":[");
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) m_t.print(",");
            m_t.print("\"");
            m_t.print(items[i]);
            m_t.print("\"");
        }
        m_t.print("]");
        return;
    }
    char b[112];
    size_t n = 0;
    b[0] = '\0';
    for (size_t i = 0; i < count && n + 2 < sizeof(b); ++i) {
        const int w = snprintf(b + n, sizeof(b) - n, i == 0 ? "%s" : ",%s", items[i]);
        if (w < 0) break;
        n += static_cast<size_t>(w);
        if (n >= sizeof(b)) { n = sizeof(b) - 1; break; }
    }
    field(key, count == 0 ? "NONE" : b, false);
}

void KvReply::end(const char* asciiNote)
{
    if (m_json) {
        m_t.println("}}");
    } else if (m_shape == Shape::LINE) {
        if (asciiNote != nullptr && asciiNote[0] != '\0') {
            append(" ");
            append(asciiNote);
        }
        m_t.println(m_line);
    } else {
        char tail[16];
        snprintf(tail, sizeof(tail), "OK %lu", static_cast<unsigned long>(m_count));
        m_t.println(tail);
    }
}

void KvReply::sep()
{
    if (m_count++ > 0) m_t.print(",");
}

void KvReply::field(const char* key, const char* value, bool quoted)
{
    if (m_json) {
        sep();
        m_t.print("\"");
        m_t.print(key);
        m_t.print(quoted ? "\":\"" : "\":");
        m_t.print(value);
        if (quoted) m_t.print("\"");
    } else if (m_shape == Shape::LINE) {
        ++m_count;
        append(" ");
        append(key);
        append("=");
        append(value);
    } else {
        ++m_count;
        m_t.print(key);
        m_t.print("=");
        m_t.println(value);
    }
}

void KvReply::append(const char* s)
{
    while (s != nullptr && *s != '\0' && m_len + 1 < sizeof(m_line)) m_line[m_len++] = *s++;
    m_line[m_len] = '\0';
}

} // namespace Protocol
