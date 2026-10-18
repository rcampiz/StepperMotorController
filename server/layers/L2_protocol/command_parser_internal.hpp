/**
 * @file command_parser_internal.hpp
 * @brief Shared helpers for command_parser handler files
 *
 * Contains L2-owned string mappings, argument validation limits,
 * and utility functions used across per-domain handler .cpp files.
 */

#pragma once

#include "L2_protocol/base64_reader.hpp"
#include "L2_protocol/command_parser.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Protocol {

// newlib-nano doesn't support %lld — manual int64_t to string
inline void i64toa(int64_t val, char *buf, size_t bufSize) {
  if (bufSize == 0)
    return;
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  char tmp[21];
  int i = 0;
  bool neg = (val < 0);
  uint64_t uval =
      neg ? static_cast<uint64_t>(-val) : static_cast<uint64_t>(val);
  while (uval > 0 && i < 20) {
    tmp[i++] = '0' + static_cast<char>(uval % 10);
    uval /= 10;
  }
  size_t j = 0;
  if (neg && j < bufSize - 1)
    buf[j++] = '-';
  while (i > 0 && j < bufSize - 1)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// ============================================================================
// Argument validation limits (based on powerSTEP01 register sizes)
// ============================================================================
namespace Limits {
constexpr int32_t POS_MIN = -2097152;
constexpr int32_t POS_MAX = 2097151;
constexpr int32_t SPEED_MIN = 0;
constexpr int32_t SPEED_MAX = 15625;
constexpr int32_t ACCEL_MIN = 1;
constexpr int32_t ACCEL_MAX = 4095;
constexpr int32_t MAXSPD_MIN = 1;
constexpr int32_t MAXSPD_MAX = 1023;
constexpr int32_t DIR_MIN = 0;
constexpr int32_t DIR_MAX = 1;
} // namespace Limits

// ============================================================================
// L2 string ↔ enum mapping (protocol layer owns wire-format strings)
// ============================================================================

inline const char* controllerStateToString(uint8_t s) {
    switch (s) {
        case 0: return "IDLE";
        case 1: return "ARMED";
        case 2: return "RUNNING";
        case 3: return "FAULT";
        case 4: return "ESTOP";
        default: return "UNKNOWN";
    }
}

inline const char* controlModeToString(uint8_t m) {
    switch (m) {
        case 0: return "OPEN_LOOP";
        case 1: return "MONITOR";
        case 2: return "SPEED_TRIM";
        default: return "UNKNOWN";
    }
}

inline const char* encoderStatusToString(uint8_t s) {
    switch (s) {
        case 0: return "NOT_PRESENT";
        case 1: return "INITIALIZING";
        case 2: return "READY";
        case 3: return "FAULT";
        default: return "UNKNOWN";
    }
}

inline const char* displayModeToString(uint8_t m) {
    switch (m) {
        case 0: return "LOCAL";
        case 1: return "REMOTE";
        default: return "UNKNOWN";
    }
}

inline const char* roleIdToString(uint8_t r) {
    switch (r) {
        case 0: return "NONE";
        case 1: return "FL";
        case 2: return "FR";
        case 3: return "RL";
        case 4: return "RR";
        default: return "UNKNOWN";
    }
}

inline uint8_t parseControlMode(const char* s) {
    if (strcmp(s, "OPEN_LOOP") == 0)   return 0;
    if (strcmp(s, "MONITOR") == 0)     return 1;
    if (strcmp(s, "SPEED_TRIM") == 0)  return 2;
    if (strcmp(s, "PID") == 0)         return 2;
    return 0xFF;
}

inline uint8_t parseRole(const char* s) {
    if (strcmp(s, "FL") == 0 || strcmp(s, "fl") == 0 || strcmp(s, "FRONT_LEFT") == 0)  return 1;
    if (strcmp(s, "FR") == 0 || strcmp(s, "fr") == 0 || strcmp(s, "FRONT_RIGHT") == 0) return 2;
    if (strcmp(s, "RL") == 0 || strcmp(s, "rl") == 0 || strcmp(s, "REAR_LEFT") == 0)   return 3;
    if (strcmp(s, "RR") == 0 || strcmp(s, "rr") == 0 || strcmp(s, "REAR_RIGHT") == 0)  return 4;
    if (strcmp(s, "NONE") == 0 || strcmp(s, "none") == 0) return 0;
    return 0xFF;
}

inline uint8_t parseDisplayMode(const char* s) {
    if (strcmp(s, "LOCAL") == 0)  return 0;
    if (strcmp(s, "REMOTE") == 0) return 1;
    return 0xFF;
}

/**
 * @brief Escape a string for use inside a JSON string literal (F5).
 *
 * Escapes backslash, double quote and control characters (\n \r \t, others as \u00XX).
 * Always NUL-terminates; truncates at outSize-1. Returns the number of characters written.
 */
inline size_t jsonEscape(const char* in, char* out, size_t outSize) {
  static const char hex[] = "0123456789ABCDEF";
  size_t o = 0;
  if (outSize == 0) return 0;
  for (size_t i = 0; in != nullptr && in[i] != '\0'; ++i) {
    unsigned char c = static_cast<unsigned char>(in[i]);
    const char* esc = nullptr;
    char tmp[7];
    if (c == '"')       esc = "\\\"";
    else if (c == '\\') esc = "\\\\";
    else if (c == '\n') esc = "\\n";
    else if (c == '\r') esc = "\\r";
    else if (c == '\t') esc = "\\t";
    else if (c < 0x20) {
      tmp[0] = '\\'; tmp[1] = 'u'; tmp[2] = '0'; tmp[3] = '0';
      tmp[4] = hex[(c >> 4) & 0xF]; tmp[5] = hex[c & 0xF]; tmp[6] = '\0';
      esc = tmp;
    }
    if (esc == nullptr) {
      if (o + 1 >= outSize) break;
      out[o++] = static_cast<char>(c);
    } else {
      size_t n = 0;
      while (esc[n] != '\0') ++n;
      if (o + n >= outSize) break;
      for (size_t k = 0; k < n; ++k) out[o++] = esc[k];
    }
  }
  out[o] = '\0';
  return o;
}

} // namespace Protocol
