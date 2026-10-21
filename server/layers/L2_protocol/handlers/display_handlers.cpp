/**
 * @file display_handlers.cpp
 * @brief CommandParser display/UI/flash command handlers (UI namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "harness/pins/crc32.hpp"

namespace {
/// Upload payloads: each base64 character must arrive within this many ms (PROTOCOL.md 4)
constexpr uint32_t BYTE_TIMEOUT_MS = 100;
}

namespace Protocol {

void CommandParser::cmdUIMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    // No argument - return current mode
    cmdUIGetMode();
    return;
  }

  // Set mode
  uint8_t modeId = parseDisplayMode(cmd.args[0]);
  if (modeId == 0xFF) {
    respondErr("Unknown mode (use LOCAL or REMOTE)");
    return;
  }
  auto result = m_dispatcher.displaySetMode(modeId);
  respondStatus(result, displayModeToString(m_dispatcher.displayGetMode()));
}

void CommandParser::cmdUIGetMode() {
  respondOk(displayModeToString(m_dispatcher.displayGetMode()));
}

void CommandParser::cmdDispClear(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Parse optional color (default black = 0x0000)
  uint16_t color = 0x0000;
  if (cmd.argCount >= 1) {
    color = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 16));
  }

  m_dispatcher.displayClear(color);
  respondOk("");
}

void CommandParser::cmdDispText(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:TEXT <x> <y> <fg> <bg> <text>
  if (cmd.argCount < 5) {
    respondErr("Usage: UI:DISP:TEXT <x> <y> <fg> <bg> <text>");
    return;
  }

  auto x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto fg = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 16));
  auto bg = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 16));
  const char *text = cmd.args[4];

  m_dispatcher.displayText(x, y, text, fg, bg);
  respondOk("");
}

void CommandParser::cmdDispRect(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:RECT <x> <y> <w> <h> <color> [fill]
  if (cmd.argCount < 5) {
    respondErr("Usage: UI:DISP:RECT <x> <y> <w> <h> <color> [fill]");
    return;
  }

  auto x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  auto h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  auto color = static_cast<uint16_t>(strtoul(cmd.args[4], nullptr, 16));
  bool filled = (cmd.argCount > 5 && strcmp(cmd.args[5], "fill") == 0);

  m_dispatcher.displayRect(x, y, w, h, color, filled);
  respondOk("");
}

void CommandParser::cmdDispLine(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:LINE <x1> <y1> <x2> <y2> <color>
  if (cmd.argCount < 5) {
    respondErr("Usage: UI:DISP:LINE <x1> <y1> <x2> <y2> <color>");
    return;
  }

  auto x0 = static_cast<int16_t>(strtol(cmd.args[0], nullptr, 10));
  auto y0 = static_cast<int16_t>(strtol(cmd.args[1], nullptr, 10));
  auto x1 = static_cast<int16_t>(strtol(cmd.args[2], nullptr, 10));
  auto y1 = static_cast<int16_t>(strtol(cmd.args[3], nullptr, 10));
  auto color = static_cast<uint16_t>(strtoul(cmd.args[4], nullptr, 16));

  m_dispatcher.displayLine(x0, y0, x1, y1, color);
  respondOk("");
}

void CommandParser::cmdDispBitmap(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:BITMAP <x> <y> <w> <h> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 5 : 4;

  if (cmd.argCount < minArgs) {
    respondErr("Usage: UI:DISP:BITMAP <x> <y> <w> <h> [CRC]");
    return;
  }

  auto x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  auto h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));

  // Validate dimensions
  if (w == 0 || h == 0) {
    respondErr("Invalid dimensions");
    return;
  }
  if (x >= 240 || y >= 320) {
    respondErr("Position out of bounds");
    return;
  }

  // Calculate expected bytes (RGB565 = 2 bytes per pixel)
  auto expectedBytes = static_cast<uint32_t>(w) * h * 2;

  constexpr uint32_t MAX_BITMAP_BYTES = 240 * 320 * 2;
  if (expectedBytes > MAX_BITMAP_BYTES) {
    respondErr("Bitmap too large");
    return;
  }

  // Start LCD streaming
  if (!m_dispatcher.displayStreamStart(x, y, w, h)) {
    respondErr("LCD streaming failed");
    return;
  }

  // Send ready response with expected byte count
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(expectedBytes));
  m_transport.println(buf);
  m_transport.flush();
  Base64Reader rd(m_transport, BYTE_TIMEOUT_MS);   // base64 text follows; it skips the line's CR/LF

  // Receive binary data with timeout
  constexpr uint32_t CHUNK_SIZE = 64;
  uint8_t chunk[CHUNK_SIZE];
  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < expectedBytes) {
    uint32_t remaining = expectedBytes - bytesReceived;
    uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

    uint32_t chunkReceived = 0;
    while (chunkReceived < toRead) {
      uint8_t byte;
      if (rd.next(byte)) {
        chunk[chunkReceived++] = byte;
      } else {
        m_dispatcher.displayStreamEnd();
        char errBuf[64];
        snprintf(errBuf, sizeof(errBuf), "%s at byte %lu/%lu", rd.errorText(),
                 static_cast<unsigned long>(bytesReceived + chunkReceived),
                 static_cast<unsigned long>(expectedBytes));
        respondErr(errBuf);
        return;
      }
    }

    if (useCrc) {
      crcState = Util::crc32_update(crcState, chunk, chunkReceived);
    }

    m_dispatcher.displayStreamData(chunk, chunkReceived);
    bytesReceived += chunkReceived;
    feedWatchdogDuringUpload();
  }

  m_dispatcher.displayStreamEnd();

  if (useCrc) {
    if (!verifyCrc(crcState, rd)) {
      return;
    }
  }

  respondOk("");
}

void CommandParser::cmdDispBitmapB64(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:BITMAP:B64 <x> <y> <w> <h> <base64_data>
  if (cmd.argCount < 5) {
    respondErr("Usage: UI:DISP:BITMAP:B64 <x> <y> <w> <h> <base64_data>");
    return;
  }

  auto x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  auto h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  const char *b64 = cmd.args[4];

  // Decode base64 to binary buffer
  // Base64 decodes to ~3/4 of input length
  size_t b64Len = strlen(b64);
  size_t maxDecoded = ((b64Len * 3) / 4) + 1;

  // Limit buffer size to prevent stack overflow
  constexpr size_t MAX_BITMAP_DECODE = 512;
  if (maxDecoded > MAX_BITMAP_DECODE) {
    respondErr("Bitmap too large. Max 512 bytes decoded.");
    return;
  }

  uint8_t decoded[MAX_BITMAP_DECODE];
  size_t decodedLen = 0;

  // Simple base64 decode lookup
  auto b64Val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '+') {
      return 62;
    }
    if (c == '/') {
      return 63;
    }
    return -1;
  };

  size_t i = 0;
  while (i < b64Len && decodedLen < MAX_BITMAP_DECODE) {
    // Get 4 base64 chars
    int v[4] = {0, 0, 0, 0};
    int validChars = 0;

    for (int j = 0; j < 4 && i < b64Len; j++) {
      if (b64[i] == '=') {
        i++;
        continue;
      }
      int val = b64Val(b64[i++]);
      if (val >= 0) {
        v[j] = val;
        validChars++;
      }
    }

    if (validChars >= 2 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[0] << 2) | (v[1] >> 4));
    }
    if (validChars >= 3 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[1] << 4) | (v[2] >> 2));
    }
    if (validChars >= 4 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[2] << 6) | v[3]);
    }
  }

  // Verify size matches expected
  size_t expectedSize =
      static_cast<size_t>(w) * h * 2; // RGB565 = 2 bytes/pixel
  if (decodedLen < expectedSize) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Size mismatch: got %u, expected %u",
             static_cast<unsigned>(decodedLen),
             static_cast<unsigned>(expectedSize));
    respondErr(buf);
    return;
  }

  m_dispatcher.displayBitmap(x, y, w, h, decoded, decodedLen);
  respondOk("");
}

void CommandParser::cmdLcdReg(const ParsedCommand &cmd) {
  // Usage: UI:LCD:REG <reg> <byte> [byte ...]   (hex; e.g. UI:LCD:REG BB 19 sets VCOM)
  if (cmd.argCount < 2) {
    respondErr("Usage: UI:LCD:REG <reg> <byte> [byte ...]");
    return;
  }
  uint8_t data[14];
  uint8_t len = 0;
  auto reg = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 16));
  for (uint32_t i = 1; i < cmd.argCount && len < sizeof(data); i++) {
    data[len++] = static_cast<uint8_t>(strtoul(cmd.args[i], nullptr, 16));
  }
  if (!m_dispatcher.displayPanelRegister(reg, data, len)) {
    respondErr("Register not tunable");
    return;
  }
  respondOk("");
}

void CommandParser::cmdDispIndicator(const ParsedCommand &cmd) {
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  if (cmd.argCount < 3) {
    respondErr("Usage: UI:DISP:INDICATOR <angle> <rot_dir> <has_trans> [speed_pct] [rot_pct]");
    return;
  }

  int32_t angle = strtol(cmd.args[0], nullptr, 10);
  int32_t rotDir = strtol(cmd.args[1], nullptr, 10);
  int32_t hasTrans = strtol(cmd.args[2], nullptr, 10);

  if (angle < 0 || angle > 359) {
    respondErr("Invalid angle");
    return;
  }
  if (rotDir < -1 || rotDir > 1) {
    respondErr("Invalid rotation_dir");
    return;
  }
  if (hasTrans < 0 || hasTrans > 1) {
    respondErr("Invalid has_translation");
    return;
  }

  // Magnitudes 0-100 (optional, default full): arrow length and the arc around the ring
  int32_t speedPct = cmd.argCount > 3 ? strtol(cmd.args[3], nullptr, 10) : 100;
  int32_t rotPct = cmd.argCount > 4 ? strtol(cmd.args[4], nullptr, 10) : 100;
  if (speedPct < 0 || speedPct > 100 || rotPct < 0 || rotPct > 100) {
    respondErr("Invalid percent");
    return;
  }

  m_dispatcher.displayIndicator(
      static_cast<uint16_t>(angle), static_cast<int8_t>(rotDir), hasTrans != 0,
      static_cast<uint8_t>(speedPct), static_cast<uint8_t>(rotPct));
  respondOk("");
}

// UI:ESTOP:HOLD <pct 0-100> — the host's clear hold on the E-STOP page: a pad's START held fills
// the page's bar as the board's own CENTER hold does (the host sends SYST:FAULT:CLEAR when it is
// full). LOCAL or REMOTE; nothing shows without an E-STOP page; the bar empties by itself when the
// updates stop (the link went, the button was let go).
void CommandParser::cmdUiEstopHold(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: UI:ESTOP:HOLD <pct 0-100>");
    return;
  }
  char *end = nullptr;
  const long pct = strtol(cmd.args[0], &end, 10);
  if (end == cmd.args[0] || *end != '\0' || pct < 0 || pct > 100) {
    respondErr("INVALID_PARAM", "pct must be 0-100");
    return;
  }
  m_dispatcher.displayEstopHold(static_cast<uint8_t>(pct));
  respondOk("");
}

void CommandParser::cmdDispBitmapRle(const ParsedCommand &cmd) {
  if (!m_dispatcher.displayIsRemoteMode()) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: UI:DISP:BITMAP:RLE <x> <y> <w> <h> <compressed_bytes> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 6 : 5;

  if (cmd.argCount < minArgs) {
    respondErr(
        "Usage: UI:DISP:BITMAP:RLE <x> <y> <w> <h> <compressed_bytes> [CRC]");
    return;
  }

  auto x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  auto h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  uint32_t compressedBytes = strtoul(cmd.args[4], nullptr, 10);

  if (w == 0 || h == 0) {
    respondErr("Invalid dimensions");
    return;
  }
  if (x >= 240 || y >= 320) {
    respondErr("Position out of bounds");
    return;
  }
  if (compressedBytes == 0) {
    respondErr("Invalid compressed size");
    return;
  }

  constexpr uint32_t MAX_COMPRESSED = 240 * 320 * 3;
  if (compressedBytes > MAX_COMPRESSED) {
    respondErr("Compressed size too large");
    return;
  }

  if (!m_dispatcher.displayStreamStart(x, y, w, h)) {
    respondErr("LCD streaming failed");
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(compressedBytes));
  m_transport.println(buf);
  m_transport.flush();
  Base64Reader rd(m_transport, BYTE_TIMEOUT_MS);   // base64 text follows; it skips the line's CR/LF

  // RLE streaming decoder state machine
  enum RleState { HEADER, LITERAL, REPEAT };
  RleState rleState = HEADER;
  uint16_t runCount = 0;
  uint8_t pixelBuf[2] = {0, 0};
  uint8_t pixelIdx = 0;
  auto totalPixels = static_cast<uint32_t>(w) * h;
  uint32_t decodedPixels = 0;

  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < compressedBytes) {
    uint8_t byte;
    if (!rd.next(byte)) {
      m_dispatcher.displayStreamEnd();
      char errBuf[64];
      snprintf(errBuf, sizeof(errBuf), "%s at byte %lu/%lu", rd.errorText(),
               static_cast<unsigned long>(bytesReceived),
               static_cast<unsigned long>(compressedBytes));
      respondErr(errBuf);
      return;
    }
    if (useCrc) {
      crcState = Util::crc32_update(crcState, &byte, 1);
    }
    bytesReceived++;
    if ((bytesReceived & 63U) == 0)
      feedWatchdogDuringUpload();

    switch (rleState) {
    case HEADER:
      if (byte & 0x80) {
        runCount = static_cast<uint16_t>(byte - 125);
        rleState = REPEAT;
        pixelIdx = 0;
      } else {
        runCount = static_cast<uint16_t>(byte + 1);
        rleState = LITERAL;
        pixelIdx = 0;
      }
      break;

    case LITERAL:
      pixelBuf[pixelIdx++] = byte;
      if (pixelIdx >= 2) {
        m_dispatcher.displayStreamData(pixelBuf, 2);
        decodedPixels++;
        pixelIdx = 0;
        runCount--;
        if (runCount == 0)
          rleState = HEADER;
      }
      break;

    case REPEAT:
      pixelBuf[pixelIdx++] = byte;
      if (pixelIdx >= 2) {
        for (uint16_t i = 0; i < runCount; i++) {
          m_dispatcher.displayStreamData(pixelBuf, 2);
          decodedPixels++;
        }
        rleState = HEADER;
        pixelIdx = 0;
      }
      break;
    }
  }

  m_dispatcher.displayStreamEnd();

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState, rd)) {
      return;
    }
  }

  // Verify decoded pixel count
  if (decodedPixels != totalPixels) {
    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "RLE decode: got %lu pixels, expected %lu",
             static_cast<unsigned long>(decodedPixels),
             static_cast<unsigned long>(totalPixels));
    respondErr(errBuf);
    return;
  }

  respondOk("");
}

void CommandParser::feedWatchdogDuringUpload() {
  m_dispatcher.safetyHeartbeatReceived(m_dispatcher.safetyGetHeartbeatStatus().lastSeq);
}

bool CommandParser::hasCrcFlag(const ParsedCommand &cmd) const {
  if (cmd.argCount < 1)
    return false;
  return (strcmp(cmd.args[cmd.argCount - 1], "CRC") == 0);
}

bool CommandParser::verifyCrc(uint32_t computedCrc, Base64Reader &payload) {
  // Finalize CRC
  uint32_t expected = computedCrc ^ 0xFFFFFFFF;

  // The 4 CRC bytes (little-endian) are encoded together with the data
  uint8_t crcBytes[4];
  for (int i = 0; i < 4; i++) {
    if (!payload.next(crcBytes[i])) {
      char buf[48];
      snprintf(buf, sizeof(buf), "CRC bytes: %s", payload.errorText());
      respondErr(buf);
      return false;
    }
  }

  auto received = static_cast<uint32_t>(crcBytes[0]) |
                      (static_cast<uint32_t>(crcBytes[1]) << 8) |
                      (static_cast<uint32_t>(crcBytes[2]) << 16) |
                      (static_cast<uint32_t>(crcBytes[3]) << 24);

  if (received != expected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CRC mismatch: expected %08lX got %08lX",
             static_cast<unsigned long>(expected),
             static_cast<unsigned long>(received));
    respondErr(buf);
    return false;
  }

  return true;
}

void CommandParser::cmdFlashInfo() {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  auto info = m_dispatcher.flashGetInfo();

  if (m_format == ResponseFormat::JSON) {
    char data[128];
    snprintf(
        data, sizeof(data),
        "{\"manufacturer\":\"%02X\",\"capacity_kb\":%lu,\"max_slots\":%lu}",
        info.manufacturer,
        static_cast<unsigned long>(info.capacityBytes / 1024),
        static_cast<unsigned long>(info.maxSlots));
    respondJsonOk(m_currentCmd, data);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf), "FLASH_INFO mfr=%02X cap=%luKB slots=%lu",
             info.manufacturer,
             static_cast<unsigned long>(info.capacityBytes / 1024),
             static_cast<unsigned long>(info.maxSlots));
    respondOk(buf);
  }
}

void CommandParser::cmdFlashUpload(const ParsedCommand &cmd) {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: UI:FLASH:UPLOAD <slot> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 2 : 1;

  if (cmd.argCount < minArgs) {
    respondErr("Usage: UI:FLASH:UPLOAD <slot> [CRC]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= m_dispatcher.flashMaxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  constexpr uint32_t expectedBytes =
      Protocol::ICommandDispatcher::FLASH_IMAGE_SIZE;

  // Erase the slot first
  if (!m_dispatcher.flashEraseSlot(slot)) {
    respondErr("Flash erase failed");
    return;
  }

  // Send ready response
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(expectedBytes));
  m_transport.println(buf);
  m_transport.flush();
  Base64Reader rd(m_transport, BYTE_TIMEOUT_MS);   // base64 text follows; it skips the line's CR/LF

  // Receive binary data and program page-by-page
  constexpr uint32_t PAGE_SIZE = Protocol::ICommandDispatcher::FLASH_PAGE_SIZE;
  uint8_t page[PAGE_SIZE];
  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < expectedBytes) {
    uint32_t remaining = expectedBytes - bytesReceived;
    uint32_t toRead = (remaining < PAGE_SIZE) ? remaining : PAGE_SIZE;

    // Read one page worth of data
    uint32_t pageReceived = 0;
    while (pageReceived < toRead) {
      uint8_t byte;
      if (rd.next(byte)) {
        page[pageReceived++] = byte;
      } else {
        char errBuf[64];
        snprintf(errBuf, sizeof(errBuf), "%s at byte %lu/%lu", rd.errorText(),
                 static_cast<unsigned long>(bytesReceived + pageReceived),
                 static_cast<unsigned long>(expectedBytes));
        respondErr(errBuf);
        return;
      }
    }

    if (useCrc) {
      crcState = Util::crc32_update(crcState, page, toRead);
    }

    // Program the page to flash
    if (!m_dispatcher.flashWriteSlotData(slot, bytesReceived, page, toRead)) {
      respondErr("Flash program failed");
      return;
    }

    bytesReceived += toRead;
    feedWatchdogDuringUpload();
  }

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState, rd)) {
      return;
    }
  }

  // Read-back verification: read first 4 bytes from flash to confirm write
  uint8_t verify[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  m_dispatcher.flashReadSlotChunk(slot, 0, verify, 4);
  char okBuf[96];
  snprintf(okBuf, sizeof(okBuf),
           "Upload complete (%lu bytes, verify %02X%02X%02X%02X)",
           static_cast<unsigned long>(bytesReceived), verify[0], verify[1],
           verify[2], verify[3]);
  respondOk(okBuf);
}

void CommandParser::cmdFlashUploadRle(const ParsedCommand &cmd) {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: UI:FLASH:UPLOAD:RLE <slot> <compressed_bytes> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 3 : 2; // slot, comp_bytes [, CRC]

  if (cmd.argCount < minArgs) {
    respondErr("Usage: UI:FLASH:UPLOAD:RLE <slot> <compressed_bytes> [CRC]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= m_dispatcher.flashMaxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  uint32_t compressedBytes = strtoul(cmd.args[1], nullptr, 10);
  if (compressedBytes == 0) {
    respondErr("Invalid compressed size");
    return;
  }

  constexpr uint32_t MAX_COMPRESSED = 240 * 320 * 3;
  if (compressedBytes > MAX_COMPRESSED) {
    respondErr("Compressed size too large");
    return;
  }

  // Erase the slot first
  if (!m_dispatcher.flashEraseSlot(slot)) {
    respondErr("Flash erase failed");
    return;
  }

  // Send ready response
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(compressedBytes));
  m_transport.println(buf);
  m_transport.flush();
  Base64Reader rd(m_transport, BYTE_TIMEOUT_MS);   // base64 text follows; it skips the line's CR/LF

  // RLE streaming decoder → page buffer → flash
  enum RleState { HEADER, LITERAL, REPEAT };
  RleState rleState = HEADER;
  uint16_t runCount = 0;
  uint8_t pixelBuf[2] = {0, 0};
  uint8_t pixelIdx = 0;
  uint32_t totalPixels = 240UL * 320;
  uint32_t decodedPixels = 0;

  constexpr uint32_t PAGE_SIZE = Protocol::ICommandDispatcher::FLASH_PAGE_SIZE;
  uint8_t page[PAGE_SIZE];
  uint32_t pageOffset = 0;  // Bytes in current page buffer
  uint32_t flashOffset = 0; // Byte offset into the flash slot

  uint32_t crcState = 0xFFFFFFFF;
  uint32_t bytesReceived = 0;

  // Helper lambda: flush current page to flash
  auto flushPage = [&]() -> bool {
    if (pageOffset == 0)
      return true;
    if (!m_dispatcher.flashWriteSlotData(slot, flashOffset, page, pageOffset)) {
      return false;
    }
    flashOffset += pageOffset;
    pageOffset = 0;
    return true;
  };

  // Helper: emit one decoded pixel (2 bytes) into the page buffer
  auto emitPixel = [&](uint8_t hi, uint8_t lo) -> bool {
    page[pageOffset++] = hi;
    if (pageOffset >= PAGE_SIZE) {
      if (!flushPage())
        return false;
    }
    page[pageOffset++] = lo;
    if (pageOffset >= PAGE_SIZE) {
      if (!flushPage())
        return false;
    }
    decodedPixels++;
    return true;
  };

  while (bytesReceived < compressedBytes) {
    uint8_t byte;
    if (!rd.next(byte)) {
      char errBuf[64];
      snprintf(errBuf, sizeof(errBuf), "%s at byte %lu/%lu", rd.errorText(),
               static_cast<unsigned long>(bytesReceived),
               static_cast<unsigned long>(compressedBytes));
      respondErr(errBuf);
      return;
    }
    if (useCrc) {
      crcState = Util::crc32_update(crcState, &byte, 1);
    }
    bytesReceived++;
    if ((bytesReceived & 63U) == 0)
      feedWatchdogDuringUpload();

    switch (rleState) {
    case HEADER:
      if (byte & 0x80) {
        runCount = static_cast<uint16_t>(byte - 125);
        rleState = REPEAT;
        pixelIdx = 0;
      } else {
        runCount = static_cast<uint16_t>(byte + 1);
        rleState = LITERAL;
        pixelIdx = 0;
      }
      break;

    case LITERAL:
      pixelBuf[pixelIdx++] = byte;
      if (pixelIdx >= 2) {
        if (!emitPixel(pixelBuf[0], pixelBuf[1])) {
          respondErr("Flash program failed");
          return;
        }
        pixelIdx = 0;
        runCount--;
        if (runCount == 0)
          rleState = HEADER;
      }
      break;

    case REPEAT:
      pixelBuf[pixelIdx++] = byte;
      if (pixelIdx >= 2) {
        for (uint16_t i = 0; i < runCount; i++) {
          if (!emitPixel(pixelBuf[0], pixelBuf[1])) {
            respondErr("Flash program failed");
            return;
          }
        }
        rleState = HEADER;
        pixelIdx = 0;
      }
      break;
    }
  }

  // Flush remaining page data
  if (!flushPage()) {
    respondErr("Flash program failed (final page)");
    return;
  }

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState, rd)) {
      return; // verifyCrc already sent error response
    }
  }

  // Verify decoded pixel count
  if (decodedPixels != totalPixels) {
    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "RLE decode: got %lu pixels, expected %lu",
             static_cast<unsigned long>(decodedPixels),
             static_cast<unsigned long>(totalPixels));
    respondErr(errBuf);
    return;
  }

  // Read-back verification: read first 4 bytes from flash to confirm write
  uint8_t verify[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  m_dispatcher.flashReadSlotChunk(slot, 0, verify, 4);

  char okBuf[80];
  snprintf(okBuf, sizeof(okBuf),
           "Upload complete (%lu px, verify %02X%02X%02X%02X)",
           static_cast<unsigned long>(decodedPixels), verify[0], verify[1],
           verify[2], verify[3]);
  respondOk(okBuf);
}

void CommandParser::cmdFlashShow(const ParsedCommand &cmd) {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  if (cmd.argCount < 1) {
    respondErr("Usage: UI:FLASH:SHOW <slot>");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= m_dispatcher.flashMaxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  // Automatically switch to REMOTE mode for display control
  m_dispatcher.displaySetMode(1);

  // Start LCD streaming (full screen)
  if (!m_dispatcher.displayStreamStart(0, 0, 240, 320)) {
    respondErr("LCD streaming failed");
    return;
  }

  // Double-buffered flash→LCD pipeline: DMA1 (SPI2 flash read) overlaps
  // with DMA2 (SPI1 LCD write) since they use independent DMA controllers.
  constexpr uint32_t CHUNK_SIZE = 512;
  uint8_t buf[2][CHUNK_SIZE];
  int cur = 0;
  uint32_t offset = 0;
  uint32_t nonZeroCount = 0;
  uint32_t nonFFCount = 0;
  uint8_t first4[4] = {0};

  // Read first chunk synchronously
  if (!m_dispatcher.flashReadSlotChunk(slot, 0, buf[cur], CHUNK_SIZE)) {
    m_dispatcher.displayStreamEnd();
    respondErr("Flash read failed");
    return;
  }
  for (uint32_t i = 0; i < 4; i++)
    first4[i] = buf[cur][i];
  offset = CHUNK_SIZE;

  // Pipeline: start next flash read, then write current chunk to LCD
  while (offset < Protocol::ICommandDispatcher::FLASH_IMAGE_SIZE) {
    uint32_t remaining = Protocol::ICommandDispatcher::FLASH_IMAGE_SIZE - offset;
    uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

    // Start async flash read into other buffer (DMA1 on SPI2)
    if (!m_dispatcher.flashReadSlotChunkStart(slot, offset, buf[1 - cur],
                                              toRead)) {
      m_dispatcher.displayStreamEnd();
      respondErr("Flash read failed");
      return;
    }

    // While flash DMA runs, write current buffer to LCD (DMA2 on SPI1)
    for (uint32_t i = 0; i < CHUNK_SIZE; i++) {
      if (buf[cur][i] != 0x00)
        nonZeroCount++;
      if (buf[cur][i] != 0xFF)
        nonFFCount++;
    }
    m_dispatcher.displayStreamData(buf[cur], CHUNK_SIZE);

    // Wait for flash read to complete
    m_dispatcher.flashReadSlotChunkFinish();

    cur = 1 - cur;
    offset += toRead;
  }

  // Write final chunk to LCD
  uint32_t lastSize =
      Protocol::ICommandDispatcher::FLASH_IMAGE_SIZE - (offset - CHUNK_SIZE);
  if (lastSize > CHUNK_SIZE)
    lastSize = CHUNK_SIZE;
  for (uint32_t i = 0; i < lastSize; i++) {
    if (buf[cur][i] != 0x00)
      nonZeroCount++;
    if (buf[cur][i] != 0xFF)
      nonFFCount++;
  }
  m_dispatcher.displayStreamData(buf[cur], lastSize);

  m_dispatcher.displayStreamEnd();
  char okBuf[96];
  snprintf(okBuf, sizeof(okBuf),
           "slot=%lu first=%02X%02X%02X%02X nonZero=%lu nonFF=%lu",
           (unsigned long)slot, first4[0], first4[1], first4[2], first4[3],
           (unsigned long)nonZeroCount, (unsigned long)nonFFCount);
  respondOk(okBuf);
}

void CommandParser::cmdFlashEraseAll() {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  if (!m_dispatcher.flashEraseAll()) {
    respondErr("Flash erase failed");
    return;
  }

  respondOk("All image slots erased");
}

void CommandParser::cmdFlashDump(const ParsedCommand &cmd) {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: UI:FLASH:DUMP <slot> [offset] [len]
  if (cmd.argCount < 1) {
    respondErr("Usage: UI:FLASH:DUMP <slot> [offset] [len]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= m_dispatcher.flashMaxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  uint32_t offset = 0;
  uint32_t len = 64; // Default: dump 64 bytes
  if (cmd.argCount >= 2)
    offset = strtoul(cmd.args[1], nullptr, 10);
  if (cmd.argCount >= 3)
    len = strtoul(cmd.args[2], nullptr, 10);
  if (len > 256)
    len = 256; // Cap at 256 bytes
  if (offset + len > Protocol::ICommandDispatcher::FLASH_IMAGE_SIZE) {
    respondErr("Offset+len exceeds image size");
    return;
  }

  uint8_t buf[256];
  if (!m_dispatcher.flashReadSlotChunk(slot, offset, buf, len)) {
    respondErr("Flash read failed");
    return;
  }

  // F12: terminated reply; hex dump in 16-byte rows
  char extra[64];
  snprintf(extra, sizeof(extra), "\"slot\":%lu,\"offset\":%lu,\"len\":%lu",
           static_cast<unsigned long>(slot), static_cast<unsigned long>(offset),
           static_cast<unsigned long>(len));
  m_sink.begin(m_format == ResponseFormat::JSON, "FLASH_DUMP", extra);
  char line[80];
  for (uint32_t i = 0; i < len; i += 16) {
    int pos = snprintf(line, sizeof(line), "%06lX:",
                       static_cast<unsigned long>(
                           m_dispatcher.flashSlotAddress(slot) + offset + i));
    for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
      pos += snprintf(line + pos, sizeof(line) - pos, " %02X", buf[i + j]);
    }
    m_sink.writeLine(line);
  }
  m_sink.end();
}

void CommandParser::cmdFlashTest() {
  if (!m_dispatcher.flashIsAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // All diagnostics packed into final response (client discards println lines)
  char result[256];
  int rpos = 0;

  // Step 0: Read JEDEC ID NOW (verifies SPI2 bus is still alive)
  auto info = m_dispatcher.flashGetInfo();
  rpos += snprintf(result + rpos, sizeof(result) - rpos, "jedec=%02X/%02X/%02X",
                   info.manufacturer, info.memoryType, info.capacityCode);

  // Step 0b+0c: SPI2 peripheral + pin diagnostics (via debug handler)
  rpos += m_dispatcher.debugFormatSpi2Diag(result + rpos, sizeof(result) - rpos);

  // Step 1: Read before erase (first 4 bytes)
  uint8_t before[4];
  m_dispatcher.flashReadSlotChunk(0, 0, before, 4);
  rpos +=
      snprintf(result + rpos, sizeof(result) - rpos, " pre=%02X%02X%02X%02X",
               before[0], before[1], before[2], before[3]);

  // Step 2: Erase slot 0
  if (!m_dispatcher.flashEraseSlot(0)) {
    rpos += snprintf(result + rpos, sizeof(result) - rpos, " erase=ERR");
    respondErr(result);
    return;
  }

  // Step 3: Read after erase (should be all FF)
  uint8_t afterErase[16];
  m_dispatcher.flashReadSlotChunk(0, 0, afterErase, 16);
  bool eraseOk = true;
  for (int i = 0; i < 16; i++) {
    if (afterErase[i] != 0xFF) {
      eraseOk = false;
      break;
    }
  }
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " era=%s/%02X%02X%02X%02X", eraseOk ? "OK" : "FAIL",
                   afterErase[0], afterErase[1], afterErase[2], afterErase[3]);

  // Step 4: Write test pattern to first page
  uint8_t pattern[256];
  for (int i = 0; i < 256; i++)
    pattern[i] = (i & 1) ? 0x55 : 0xAA;
  if (!m_dispatcher.flashWriteSlotData(0, 0, pattern, 256)) {
    rpos += snprintf(result + rpos, sizeof(result) - rpos, " wr=ERR");
    respondErr(result);
    return;
  }

  // Step 5: Read back and compare
  uint8_t readback[256];
  m_dispatcher.flashReadSlotChunk(0, 0, readback, 256);
  int mismatches = 0;
  for (int i = 0; i < 256; i++) {
    if (readback[i] != pattern[i])
      mismatches++;
  }
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " wr=%s(%d) rb=%02X%02X%02X%02X",
                   mismatches == 0 ? "OK" : "FAIL", 256 - mismatches,
                   readback[0], readback[1], readback[2], readback[3]);

  if (mismatches == 0 && eraseOk) {
    respondOk(result);
  } else {
    respondErr(result);
  }
}

} // namespace Protocol
