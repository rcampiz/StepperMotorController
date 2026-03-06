/**
 * @file command_parser.cpp
 * @brief Core dispatch and response logic. Handlers in handlers/ *.cpp.
 */
#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/event_codec.hpp"
#include "harness/trace/interface_trace.hpp"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace Protocol {

// Count of entries in a static array (used for dispatch table sizing)
#define NCMD(a) (sizeof(a) / sizeof(*(a)))

// CommandParser reads bytes from a serial transport (USB CDC / RTT), builds up
// complete lines, tokenizes them into command + arguments, and routes them to
// the correct handler via SCPI-style two-level dispatch (e.g. "MOT:RUN 1000").
// Responses are sent back over the same transport in ASCII or JSON format.
CommandParser::CommandParser(Harness::ITransport &transport,
                             ICommandDispatcher &dispatcher)
    : m_transport(transport), m_dispatcher(dispatcher),
      m_bufIndex(0),                    // start with empty input buffer
      m_format(ResponseFormat::ASCII) { // default to ASCII (switch via FMT command)
  memset(m_buffer, 0, sizeof(m_buffer));
  memset(m_currentCmd, 0, sizeof(m_currentCmd));
}

// --- Table-driven lookup helpers ---
// Each dispatchFoo() function (e.g. dispatchMotion, dispatchSystem) defines
// static const arrays of {suffix, handler} pairs, then passes them here.
// dispatch1: handlers that take a ParsedCommand& (need to read arguments)
// dispatch0: handlers that take no arguments (simple commands like EN, DIS)
// Both scan the table linearly — tables are small (<20 entries), so O(n) is
// fine.

bool CommandParser::dispatch1(const char *suffix, const CmdEntry *table,
                              size_t count, const ParsedCommand &cmd) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(suffix, table[i].name) == 0) {
      (this->*table[i].fn)(cmd); // call matched handler via member fn ptr
      return true;
    }
  return false;
}

bool CommandParser::dispatch0(const char *suffix, const CmdEntry0 *table,
                              size_t count) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(suffix, table[i].name) == 0) {
      (this->*table[i].fn)(); // call matched handler via member fn ptr
      return true;
    }
  return false;
}

// --- Core processing ---
// Called periodically from the comms task. Reads bytes one at a time from the
// transport (USB CDC or RTT), accumulates them into m_buffer, and dispatches
// complete lines. Provides local echo so the user sees what they type.

void CommandParser::process() {
  while (m_transport.available()) {
    uint8_t byte;
    if (!m_transport.readByte(byte, 0))
      break;

    // Backspace/DEL: erase last char and overwrite on terminal
    if (byte == '\b' || byte == 127) {
      if (m_bufIndex > 0) {
        m_bufIndex--;
        m_transport.print("\b \b");
      }
      continue;
    }
    // CR or LF: end of line — parse and dispatch the buffered command
    if (byte == '\r' || byte == '\n') {
      if (m_bufIndex > 0) {
        m_transport.println("");     // echo newline
        m_buffer[m_bufIndex] = '\0'; // null-terminate
        ParsedCommand cmd = parse(m_buffer);
        if (cmd.valid) {
          m_baudRevertRate = 0; // valid cmd cancels baud auto-revert
          // Update dispatch statistics (ring buffer of recent command names)
          m_dispatchStats.totalCommands++;
          uint8_t ri = m_dispatchStats.recentHead;
          strncpy(m_dispatchStats.recentCmds[ri], cmd.cmd, 23);
          m_dispatchStats.recentCmds[ri][23] = '\0';
          m_dispatchStats.recentHead = (ri + 1) % DispatchStats::RECENT_SIZE;
          if (m_dispatchStats.recentCount < DispatchStats::RECENT_SIZE)
            m_dispatchStats.recentCount++;
          dispatch(cmd);
        }
        m_bufIndex = 0; // reset buffer for next line
      }
      continue;
    }
    // Printable ASCII: accumulate into buffer and echo back
    if (byte >= 32 && byte < 127 && m_bufIndex < CMD_BUFFER_SIZE - 1) {
      m_buffer[m_bufIndex++] = static_cast<char>(byte);
      char echo[2] = {static_cast<char>(byte), '\0'};
      m_transport.print(echo);
    }
  }
}

// Tokenize "MOT:RUN 1000 1" → cmd="MOT:RUN", args=["1000","1"], argCount=2.
// Command word is uppercased; arguments are left as-is (case-sensitive values).
// Uses index (pos) instead of pointer arithmetic for readability.
ParsedCommand CommandParser::parse(const char *line) {
  ParsedCommand cmd = {};
  cmd.valid = false;
  cmd.argCount = 0;
  size_t pos = 0;

  while (line[pos] && isspace(line[pos]))
    pos++; // skip leading whitespace
  if (!line[pos])
    return cmd; // empty line → invalid

  // Extract command word (first token), uppercased
  size_t i = 0;
  while (line[pos] && !isspace(line[pos]) && i < sizeof(cmd.cmd) - 1)
    cmd.cmd[i++] = static_cast<char>(toupper(line[pos++]));
  cmd.cmd[i] = '\0';

  // Extract space-separated arguments (up to MAX_ARGS)
  while (line[pos] && cmd.argCount < MAX_ARGS) {
    while (line[pos] && isspace(line[pos]))
      pos++; // skip inter-arg whitespace
    if (!line[pos])
      break;
    i = 0;
    while (line[pos] && !isspace(line[pos]) && i < sizeof(cmd.args[0]) - 1)
      cmd.args[cmd.argCount][i++] = line[pos++];
    cmd.args[cmd.argCount][i] = '\0';
    cmd.argCount++;
  }
  cmd.valid = true;
  return cmd;
}

// --- Top-level dispatch ---
// Routes a parsed command to the correct SCPI namespace handler.
// SCPI commands have a colon: "MOT:RUN" → prefix="MOT", suffix="RUN" →
// dispatchMotion("RUN", cmd). Non-SCPI commands (*IDN?, FMT) are handled
// inline. Anything else falls through to debug commands.

void CommandParser::dispatch(const ParsedCommand &cmd) {
  m_dispatcher.traceRecordEntry("CMD:RX");
  ITRACE(Harness::ITrace::L2_L3_DISPATCH, "[L2>L3]", "dispatch", cmd.cmd);
  strncpy(m_currentCmd, cmd.cmd,
          sizeof(m_currentCmd) - 1); // stash for JSON response echo
  m_currentCmd[sizeof(m_currentCmd) - 1] = '\0';

  // Split on first colon: "MOT:RUN" → prefix="MOT", suffix="RUN"
  const char *colon = strchr(cmd.cmd, ':');
  if (colon) {
    char prefix[8] = {};
    auto plen = static_cast<size_t>(colon - cmd.cmd);
    if (plen >= sizeof(prefix))
      plen = sizeof(prefix) - 1;
    memcpy(prefix, cmd.cmd, plen);
    const char *suffix = colon + 1; // everything after first colon

    // Prefix → namespace dispatcher table (same pattern as dispatch1/dispatch0)
    struct PfxEntry {
      const char *p;
      void (CommandParser::*fn)(const char *, const ParsedCommand &);
    };
    static const PfxEntry pfx[] = {
        {"MOT", &CommandParser::dispatchMotion},
        {"SYST", &CommandParser::dispatchSystem},
        {"SYNC", &CommandParser::dispatchSync},
        {"DIAG", &CommandParser::dispatchDiag},
        {"CTRL", &CommandParser::dispatchCtrl},
        {"DEV", &CommandParser::dispatchDevice},
        {"UI", &CommandParser::dispatchUI},
        {"DBG", &CommandParser::dispatchDebug},
        {"DRV", &CommandParser::dispatchDriver},
    };
    bool found = false;
    for (size_t i = 0; i < NCMD(pfx); i++)
      if (strcmp(prefix, pfx[i].p) == 0) {
        (this->*pfx[i].fn)(suffix, cmd);
        found = true;
        break;
      }
    if (!found) {
      m_dispatchStats.unknownCommands++;
      respondErr("Unknown namespace prefix");
    }
  } else {
    // No colon → top-level commands (*IDN?, FMT) or hardware debug commands
    if (strcmp(cmd.cmd, "*IDN?") == 0)
      cmdVersion();
    else if (strcmp(cmd.cmd, "FMT") == 0)
      cmdSetFormat(cmd);
    else if (strcmp(cmd.cmd, "FMT?") == 0)
      cmdGetFormat();
    else if (m_debugCommands) {
      // Fall through to bringup/debug handler (SPI_TEST, REG_READ, etc.)
      const char *argPtrs[MAX_ARGS];
      for (uint8_t i = 0; i < cmd.argCount; i++)
        argPtrs[i] = cmd.args[i];
      if (!m_debugCommands->dispatch(cmd.cmd, argPtrs, cmd.argCount,
                                     m_transport)) {
        m_dispatchStats.unknownCommands++;
        respondErr("Unknown command");
      }
    } else {
      m_dispatchStats.unknownCommands++;
      respondErr("Unknown command");
    }
  }
  m_dispatcher.traceRecordExit("CMD:RX");
}

// --- SCPI namespace dispatchers ---
// Each function below handles one SCPI prefix (e.g. MOT, SYST, CTRL).
// Pattern: define static tables of {suffix, handler} pairs, then call
// dispatch1/dispatch0 to find a match. Special cases (sub-namespaces
// like SYST:FAULT:* or UI:DISP:*) are checked first with strncmp.

// MOT:* — Motor motion and configuration (move, run, stop, enable,
// accel/decel/maxspd)
void CommandParser::dispatchMotion(const char *suffix,
                                   const ParsedCommand &cmd) {
  static const CmdEntry t1[] = {
      {"MOVE", &CommandParser::cmdMove},
      {"GOTO", &CommandParser::cmdGoTo},
      {"RUN", &CommandParser::cmdRun},
      {"STOP", &CommandParser::cmdStop},
      {"HOME", &CommandParser::cmdHome},
      {"CFG:ACCEL", &CommandParser::cmdAccelPhysical},
      {"CFG:DECEL", &CommandParser::cmdDecelPhysical},
      {"CFG:MAXSPD", &CommandParser::cmdMaxSpdPhysical},
  };
  static const CmdEntry0 t0[] = {
      {"EN", &CommandParser::cmdEnable},
      {"DIS", &CommandParser::cmdDisable},
      {"ZERO", &CommandParser::cmdZero},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown MOT command");
}

// SYST:* — System commands (estop, heartbeat, baud, events, fault clear, tick)
// Sub-namespace: SYST:FAULT:CLEAR, SYST:FAULT:FORCE, SYST:FAULT:STAT?
void CommandParser::dispatchSystem(const char *suffix,
                                   const ParsedCommand &cmd) {
  // Check sub-namespace SYST:FAULT:* first (strip "FAULT:" and match remainder)
  if (strncmp(suffix, "FAULT:", 6) == 0) {
    static const CmdEntry0 ft[] = {
        {"CLEAR", &CommandParser::cmdClearFault},
        {"FORCE", &CommandParser::cmdForceClearFault},
        {"STAT?", &CommandParser::cmdGetStatus},
    };
    if (dispatch0(suffix + 6, ft, NCMD(ft)))
      return;
    respondErr("Unknown SYST:FAULT command");
    return;
  }
  static const CmdEntry t1[] = {
      {"HB", &CommandParser::cmdHeartbeat},
      {"HB:TIMEOUT", &CommandParser::cmdSetHeartbeat},
      {"BAUD", &CommandParser::cmdSetBaud},
      {"EVT:EN", &CommandParser::cmdEventEnable},
      {"DELAY", &CommandParser::cmdSystDelay},
  };
  static const CmdEntry0 t0[] = {
      {"ESTOP", &CommandParser::cmdEstop},
      {"TICK?", &CommandParser::cmdGetTick},
      {"HB:STAT?", &CommandParser::cmdGetHeartbeatStatus},
      {"VER?", &CommandParser::cmdVersion},
      {"EVT:DIS", &CommandParser::cmdEventDisable},
      {"EVT:STAT?", &CommandParser::cmdEventStatus},
      {"ZERO", &CommandParser::cmdZeroAll},
      {"DELAY?", &CommandParser::cmdSystDelayQuery},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown SYST command");
}

// SYNC:* — Multi-controller synchronization (queue, arm, start, clear)
void CommandParser::dispatchSync(const char *suffix, const ParsedCommand &cmd) {
  static const CmdEntry t1[] = {
      {"QUEUE", &CommandParser::cmdQueue},
      {"START:AT", &CommandParser::cmdStartAt},
  };
  static const CmdEntry0 t0[] = {
      {"ARM", &CommandParser::cmdArm},
      {"START", &CommandParser::cmdStart},
      {"CLEAR", &CommandParser::cmdClearQueue},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown SYNC command");
}

// DIAG:* — Diagnostics (ping for latency, status query)
void CommandParser::dispatchDiag(const char *suffix, const ParsedCommand &cmd) {
  static const CmdEntry t1[] = {{"PING", &CommandParser::cmdPing}};
  static const CmdEntry0 t0[] = {{"STAT?", &CommandParser::cmdGetStatus}};
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown DIAG command");
}

// CTRL:* — Control mode, encoder, following error supervisor, speed-trim PI,
// sysid Largest namespace. Special cases: ENC:DBG? (debug), ENC:FILT:*
// (sub-dispatch)
void CommandParser::dispatchCtrl(const char *suffix, const ParsedCommand &cmd) {
  // Special: ENC:DBG? — delegate to debug command handler (different interface)
  if (strcmp(suffix, "ENC:DBG?") == 0) {
    if (m_debugCommands) {
      const char *noArgs[] = {nullptr};
      m_debugCommands->dispatch("ENC_DEBUG", noArgs, 0, m_transport);
    }
    return;
  }
  // Special: ENC:FILT sub-dispatch (different handler signature)
  if (strncmp(suffix, "ENC:FILT:", 9) == 0) {
    cmdEncFilterSub(suffix + 9, cmd);
    return;
  }

  static const CmdEntry t1[] = {
      {"MODE", &CommandParser::cmdSetMode},
      {"ENC:FILT", &CommandParser::cmdEncFilter},
      {"ENC:FILT?", &CommandParser::cmdEncFilter},
      {"FOLLOW:THRESH", &CommandParser::cmdFollowThreshSet},
      {"TRIM:GAINS", &CommandParser::cmdTrimSetGains},
      {"TRIM:LIMITS", &CommandParser::cmdTrimSetLimits},
      {"TRIM:MAXPCT", &CommandParser::cmdTrimSetMaxPct},
      {"SYSID:STEP", &CommandParser::cmdSysIdStep},
      {"SYSID:RAMP", &CommandParser::cmdSysIdRamp},
      {"SYSID:SINE", &CommandParser::cmdSysIdSine},
      {"SYSID:TRAPEZOID", &CommandParser::cmdSysIdTrapezoid},
      {"SYSID:RECT", &CommandParser::cmdSysIdRect},
      {"SYSID:DATA?", &CommandParser::cmdSysIdData},
  };
  static const CmdEntry0 t0[] = {
      {"MODE?", &CommandParser::cmdGetMode},
      {"ENC:STAT?", &CommandParser::cmdGetEncoderStatus},
      {"ENC?", &CommandParser::cmdEncoder},
      {"ENC:ZERO", &CommandParser::cmdEncoderZero},
      {"FOLLOW?", &CommandParser::cmdFollowingError},
      {"FOLLOW:THRESH?", &CommandParser::cmdFollowThreshQuery},
      {"FOLLOW:SAVE", &CommandParser::cmdFollowSave},
      {"FOLLOW:CLEAR", &CommandParser::cmdFollowClear},
      {"TRIM?", &CommandParser::cmdTrimQuery},
      {"TRIM:RESET", &CommandParser::cmdTrimReset},
      {"TRIM:SAVE", &CommandParser::cmdTrimSave},
      {"SYSID:STATUS?", &CommandParser::cmdSysIdStatus},
      {"SYSID:ABORT", &CommandParser::cmdSysIdAbort},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown CTRL command");
}

// DEV:* — Device identification (device ID, role for multi-controller setups)
void CommandParser::dispatchDevice(const char *suffix,
                                   const ParsedCommand &cmd) {
  static const CmdEntry t1[] = {
      {"ID", &CommandParser::cmdSetDeviceId},
      {"ROLE", &CommandParser::cmdSetRole},
  };
  static const CmdEntry0 t0[] = {
      {"ID?", &CommandParser::cmdGetDeviceId},
      {"ROLE?", &CommandParser::cmdGetDeviceId},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown DEV command");
}

// UI:* — LCD display rendering and flash image storage
// Sub-namespaces: UI:DISP:* (draw primitives), UI:FLASH:* (NOR flash images)
void CommandParser::dispatchUI(const char *suffix, const ParsedCommand &cmd) {
  // UI:DISP:* — remote display drawing (text, rect, line, bitmap, etc.)
  if (strncmp(suffix, "DISP:", 5) == 0) {
    static const CmdEntry dt[] = {
        {"CLEAR", &CommandParser::cmdDispClear},
        {"TEXT", &CommandParser::cmdDispText},
        {"RECT", &CommandParser::cmdDispRect},
        {"LINE", &CommandParser::cmdDispLine},
        {"BITMAP", &CommandParser::cmdDispBitmap},
        {"BITMAP:B64", &CommandParser::cmdDispBitmapB64},
        {"BITMAP:RLE", &CommandParser::cmdDispBitmapRle},
        {"INDICATOR", &CommandParser::cmdDispIndicator},
    };
    if (dispatch1(suffix + 5, dt, NCMD(dt), cmd))
      return;
    respondErr("Unknown UI:DISP command");
    return;
  }
  // UI:FLASH:* — NOR flash image upload/show/erase
  if (strncmp(suffix, "FLASH:", 6) == 0) {
    static const CmdEntry ft1[] = {
        {"UPLOAD", &CommandParser::cmdFlashUpload},
        {"SHOW", &CommandParser::cmdFlashShow},
        {"UPLOAD:RLE", &CommandParser::cmdFlashUploadRle},
        {"DUMP", &CommandParser::cmdFlashDump},
    };
    static const CmdEntry0 ft0[] = {
        {"INFO", &CommandParser::cmdFlashInfo},
        {"ERASE_ALL", &CommandParser::cmdFlashEraseAll},
        {"TEST", &CommandParser::cmdFlashTest},
    };
    if (dispatch1(suffix + 6, ft1, NCMD(ft1), cmd))
      return;
    if (dispatch0(suffix + 6, ft0, NCMD(ft0)))
      return;
    respondErr("Unknown UI:FLASH command");
    return;
  }
  static const CmdEntry t1[] = {{"MODE", &CommandParser::cmdUIMode}};
  static const CmdEntry0 t0[] = {{"MODE?", &CommandParser::cmdUIGetMode}};
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown UI command");
}

// DBG:* — Debug/trace commands (trace dump/reset, motor debug)
void CommandParser::dispatchDebug(const char *suffix,
                                  const ParsedCommand &cmd) {
  (void)cmd; // no debug commands use parsed args
  // DBG:MOTOR — delegate to bringup debug handler
  if (strcmp(suffix, "MOTOR") == 0) {
    if (m_debugCommands) {
      const char *noArgs[] = {nullptr};
      m_debugCommands->dispatch("MOTOR_DEBUG", noArgs, 0, m_transport);
    }
    return;
  }
  static const CmdEntry0 t0[] = {
      {"TRACE:DUMP", &CommandParser::cmdTraceDump},
      {"TRACE:RESET", &CommandParser::cmdTraceReset},
  };
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown DBG command");
}

// DRV:* — Low-level driver config (KVAL, OCD, stall, step mode, encoder PPR,
// flash persist)
void CommandParser::dispatchDriver(const char *suffix,
                                   const ParsedCommand &cmd) {
  // DRV:REINIT — re-initialize powerSTEP01 and reapply saved config
  if (strcmp(suffix, "REINIT") == 0) {
    m_dispatcher.motorReinit();
    respondOk("Motor driver reinitialized and config applied");
    return;
  }
  static const CmdEntry t1[] = {
      {"CFG:KVAL", &CommandParser::cmdMotorConfigKval},
      {"CFG:OCD", &CommandParser::cmdMotorConfigOcd},
      {"CFG:STALL", &CommandParser::cmdMotorConfigStall},
      {"CFG:FAULT", &CommandParser::cmdMotorConfigFault},
      {"CFG:MOTION", &CommandParser::cmdMotorConfigMotion},
      {"CFG:STEPMODE", &CommandParser::cmdMotorConfigStepMode},
      {"STEP_MODE", &CommandParser::cmdDrvStepMode},
      {"FULL_STEPS", &CommandParser::cmdDrvFullSteps},
      {"ENC_PPR", &CommandParser::cmdDrvEncoderPPR},
  };
  static const CmdEntry0 t0[] = {
      {"CFG?", &CommandParser::cmdMotorConfigShow},
      {"CFG:SAVE", &CommandParser::cmdMotorConfigSave},
      {"CFG:LOAD", &CommandParser::cmdMotorConfigLoad},
      {"CFG:RESET", &CommandParser::cmdMotorConfigReset},
      {"CFG:APPLY", &CommandParser::cmdMotorConfigApply},
      {"STEP_MODE?", &CommandParser::cmdDrvStepModeQuery},
      {"FULL_STEPS?", &CommandParser::cmdDrvFullStepsQuery},
      {"ENC_PPR?", &CommandParser::cmdDrvEncoderPPRQuery},
  };
  if (dispatch1(suffix, t1, NCMD(t1), cmd))
    return;
  if (dispatch0(suffix, t0, NCMD(t0)))
    return;
  respondErr("Unknown DRV command");
}

// --- Response helpers ---
// All responses go through these functions. They check m_format (ASCII or JSON)
// and emit the appropriate wire format. ASCII: "OK msg\n" / "ERROR msg\n".
// JSON: {"status":"ok","command":"MOT:RUN","data":{...}}\n

// Send success response. In ASCII mode: "OK <msg>". In JSON mode: wraps msg as
// data.
void CommandParser::respondOk(const char *msg) {
  ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "respond", "ok");
  if (m_format == ResponseFormat::JSON) {
    if (msg && msg[0] != '\0') {
      char buf[192];
      snprintf(buf, sizeof(buf), "{\"message\":\"%s\"}", msg);
      respondJsonOk(m_currentCmd, buf);
    } else {
      respondJsonOk(m_currentCmd, nullptr);
    }
  } else {
    m_transport.print("OK ");
    m_transport.println(msg);
  }
}

// Send error response. In ASCII mode: "ERROR <msg>". In JSON mode: structured
// error.
void CommandParser::respondErr(const char *msg) {
  ITRACE(Harness::ITrace::L2_TELEMETRY, "[L2>L1]", "respond", "err");
  if (m_format == ResponseFormat::JSON)
    respondJsonErr(m_currentCmd, "ERROR", msg);
  else {
    m_transport.print("ERROR ");
    m_transport.println(msg);
  }
}

// Send multi-line data (used for bulk output like sysid CSV, trace dumps)
void CommandParser::respondData(const char **lines, size_t count) {
  for (size_t i = 0; i < count; i++)
    m_transport.println(lines[i]);
}

// Emit JSON success: {"status":"ok","command":"<cmd>","data":<json>}
void CommandParser::respondJsonOk(const char *command, const char *dataJson) {
  char buf[1024];
  if (dataJson && dataJson[0] != '\0')
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"command\":\"%s\",\"data\":%s}", command,
             dataJson);
  else
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"command\":\"%s\",\"data\":{}}", command);
  m_transport.println(buf);
}

// Emit JSON error:
// {"status":"error","command":"<cmd>","code":"<code>","message":"<msg>"}
void CommandParser::respondJsonErr(const char *command, const char *code,
                                   const char *message) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"status\":\"error\",\"command\":\"%s\",\"code\":\"%s\","
           "\"message\":\"%s\"}",
           command, code, message);
  m_transport.println(buf);
}

// Convenience: map a ServiceStatus (from L3 services) to OK/ERROR response
void CommandParser::respondStatus(ServiceStatus r, const char *okMsg) {
  if (r == ServiceStatus::Ok)
    respondOk(okMsg);
  else
    respondErr(statusToString(r));
}

// --- ICommandProcessor interface ---

void CommandParser::formatEvent(Harness::ITransport &transport,
                                const AsyncEvent &evt, uint32_t seq,
                                uint32_t ts_ms) {
  if (m_format == ResponseFormat::JSON) {
    EventCodec::formatJson(transport, evt, seq, ts_ms);
  } else {
    EventCodec::formatAscii(transport, evt, seq);
  }
}

void CommandParser::getDispatchStats(Harness::DispatchStats &out) const {
  out.totalCommands = m_dispatchStats.totalCommands;
  out.unknownCommands = m_dispatchStats.unknownCommands;
  out.parseErrors = m_dispatchStats.parseErrors;
  out.recentCount = m_dispatchStats.recentCount;
  // Copy recent commands (most recent first)
  for (uint8_t i = 0; i < m_dispatchStats.recentCount && i < 8; i++) {
    uint8_t idx = (m_dispatchStats.recentHead + DispatchStats::RECENT_SIZE - 1 -
                   i) %
                  DispatchStats::RECENT_SIZE;
    strncpy(out.recentCmds[i], m_dispatchStats.recentCmds[idx], 23);
    out.recentCmds[i][23] = '\0';
  }
}

} // namespace Protocol
