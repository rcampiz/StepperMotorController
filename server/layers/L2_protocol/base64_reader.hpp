/**
 * @file base64_reader.hpp
 * @brief Streams the base64 text that follows `OK READY <n>` and hands out decoded bytes
 *
 * Upload payloads (UI:DISP:BITMAP, :RLE, UI:FLASH:UPLOAD, :RLE) are base64 text since
 * 2026-10-18: the alphabet has no 0x18, so the out-of-band ESTOP byte stays armed for the
 * whole transfer (docs/PROTOCOL.md section 5) and the Comms task never has to pause it.
 * CR, LF, space and tab between characters are skipped, so the host may break the text
 * into lines; `=` padding ends the stream. The CRC32 (when requested) is encoded together
 * with the data, so one reader serves the payload and the trailer.
 */

#pragma once

#include "harness/defs/harness_l1_l2.hpp"   // Transport::ITransport through the L1-L2 harness
#include <stdint.h>

namespace Protocol {

class Base64Reader {
public:
  enum class Error : uint8_t { None, Timeout, BadChar, Ended };

  Base64Reader(Transport::ITransport &t, uint32_t charTimeoutMs)
      : m_t(t), m_timeout(charTimeoutMs) {}

  /// Next decoded byte; false on timeout, a character outside the alphabet, or after the
  /// padding ended the stream (see error()).
  bool next(uint8_t &out) {
    if (m_pos >= m_len && !fill())
      return false;
    out = m_buf[m_pos++];
    return true;
  }

  Error error() const { return m_err; }

  const char *errorText() const {
    switch (m_err) {
    case Error::Timeout: return "Timeout";
    case Error::BadChar: return "Bad base64";
    case Error::Ended:   return "Base64 ended early";
    default:             return "OK";
    }
  }

  /// Base64 characters consumed so far (whitespace excluded).
  uint32_t charsRead() const { return m_chars; }

private:
  static int value(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  }

  bool fill() {
    if (m_ended) {
      m_err = Error::Ended;
      return false;
    }
    uint32_t v[4] = {0, 0, 0, 0};
    int n = 0;
    int pad = 0;
    while (n < 4) {
      uint8_t c;
      if (!m_t.readByte(c, m_timeout)) {
        m_err = Error::Timeout;
        return false;
      }
      if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
        continue;
      ++m_chars;
      if (c == '=') {
        if (n < 2) {              // "=" cannot start a quad
          m_err = Error::BadChar;
          return false;
        }
        v[n++] = 0;
        ++pad;
        continue;
      }
      const int d = value(c);
      if (d < 0 || pad != 0) {    // outside the alphabet, or data after padding
        m_err = Error::BadChar;
        return false;
      }
      v[n++] = static_cast<uint32_t>(d);
    }
    m_buf[0] = static_cast<uint8_t>((v[0] << 2) | (v[1] >> 4));
    m_buf[1] = static_cast<uint8_t>(((v[1] & 0x0F) << 4) | (v[2] >> 2));
    m_buf[2] = static_cast<uint8_t>(((v[2] & 0x03) << 6) | v[3]);
    m_len = static_cast<uint8_t>(3 - pad);
    m_pos = 0;
    if (pad != 0)
      m_ended = true;
    return true;
  }

  Transport::ITransport &m_t;
  uint32_t m_timeout;
  uint32_t m_chars = 0;
  uint8_t m_buf[3] = {0, 0, 0};
  uint8_t m_len = 0;
  uint8_t m_pos = 0;
  bool m_ended = false;
  Error m_err = Error::None;
};

} // namespace Protocol
