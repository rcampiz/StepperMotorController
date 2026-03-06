/**
 * @file control_handlers.cpp
 * @brief CommandParser control command handlers (CTRL namespace — mode, follow, trim, sysid)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "harness/pins/itelemetry.hpp"

namespace Protocol {

void CommandParser::cmdGetMode() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s encoder=%s", controlModeToString(m_dispatcher.getControlMode()),
           encoderStatusToString(m_dispatcher.getEncoderStatus()));
  respondOk(buf);
}

void CommandParser::cmdSetMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: CTRL:MODE OPEN_LOOP|MONITOR|PID");
    return;
  }

  uint8_t modeId = parseControlMode(cmd.args[0]);
  if (modeId == 0xFF) {
    respondErr("Unknown mode (OPEN_LOOP, MONITOR, SPEED_TRIM)");
    return;
  }
  auto r = m_dispatcher.setControlMode(modeId);
  if (r == ServiceStatus::Ok) {
    respondOk(controlModeToString(m_dispatcher.getControlMode()));
  } else if (r == ServiceStatus::EncoderRequired) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Cannot enter %s: encoder %s",
             cmd.args[0], encoderStatusToString(m_dispatcher.getEncoderStatus()));
    respondErr(buf);
  } else {
    respondErr(statusToString(r));
  }
}

// CTRL:FOLLOW? — query following error
void CommandParser::cmdFollowingError() {
  // Expanded supervisor status including state, tier, and following error
  auto sv = m_dispatcher.getSupervisorTelemetry();
  uint8_t mode = m_dispatcher.getControlMode();

  static const char *stateNames[] = {"IDLE", "MOVING", "HOLDING", "RECOVERY",
                                     "FAULT"};
  static const char *tierNames[] = {"OBSERVE", "SOFT_CORRECT", "PAUSE_RECOVER",
                                    "FAULT_LATCH"};
  uint8_t si = sv.state;
  uint8_t ti = sv.tier;
  const char *stateName = (si < 5) ? stateNames[si] : "UNKNOWN";
  const char *tierName = (ti < 4) ? tierNames[ti] : "UNKNOWN";

  if (m_format == ResponseFormat::JSON) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"tier\":\"%s\",\"mode\":%u,"
             "\"pos_error\":%ld,\"vel_error\":%ld,"
             "\"setpoint\":%ld,\"pid_out\":%d,"
             "\"retries\":%u,\"error_ms\":%lu}",
             stateName, tierName, (unsigned)mode, static_cast<long>(sv.posError),
             static_cast<long>(sv.velError), static_cast<long>(sv.setpoint),
             (int)sv.pidOutput, (unsigned)sv.retryCount,
             static_cast<unsigned long>(sv.errorDurationMs));
    respondJsonOk("CTRL:FOLLOW?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "state=%s tier=%s err=%ld vel_err=%ld retries=%u", stateName,
             tierName, static_cast<long>(sv.posError),
             static_cast<long>(sv.velError), (unsigned)sv.retryCount);
    respondOk(buf);
  }
}

void CommandParser::cmdFollowThreshQuery() {
  auto ft = m_dispatcher.getFollowThresholds();

  if (m_format == ResponseFormat::JSON) {
    char json[160];
    snprintf(json, sizeof(json),
             "{\"move_error\":%d,\"move_time_ms\":%u,"
             "\"hold_error\":%d,\"hold_time_ms\":%u,"
             "\"hard_limit\":%d,\"max_retries\":%u}",
             (int)ft.moveErr, (unsigned)ft.moveTime, (int)ft.holdErr,
             (unsigned)ft.holdTime, (int)ft.hardLimit, (unsigned)ft.maxRetries);
    respondJsonOk("CTRL:FOLLOW:THRESH?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "E_MOVE=%d T_MOVE=%u E_HOLD=%d T_HOLD=%u E_HARD=%d N_RETRY=%u",
             (int)ft.moveErr, (unsigned)ft.moveTime, (int)ft.holdErr,
             (unsigned)ft.holdTime, (int)ft.hardLimit, (unsigned)ft.maxRetries);
    respondOk(buf);
  }
}

void CommandParser::cmdFollowThreshSet(const ParsedCommand &cmd) {
  if (cmd.argCount < 6) {
    respondErr("Usage: CTRL:FOLLOW:THRESH <E_MOVE> <T_MOVE> <E_HOLD> <T_HOLD> "
               "<E_HARD> <N_RETRY>");
    return;
  }

  m_dispatcher.setFollowThresholds({
      static_cast<int16_t>(strtol(cmd.args[0], nullptr, 10)),
      static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10)),
      static_cast<int16_t>(strtol(cmd.args[2], nullptr, 10)),
      static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10)),
      static_cast<int16_t>(strtol(cmd.args[4], nullptr, 10)),
      static_cast<uint8_t>(strtoul(cmd.args[5], nullptr, 10))
  });

  // Apply immediately to supervisor
  m_dispatcher.applySupervisorConfig();

  respondOk("Thresholds set");
}

void CommandParser::cmdFollowSave() {
  if (m_dispatcher.configSaveToFlash()) {
    respondOk("Follow config saved");
  } else {
    respondErr("Flash write failed");
  }
}

void CommandParser::cmdFollowClear() {
  if (m_dispatcher.supervisorClearFault()) {
    respondOk("Supervisor fault cleared");
  } else {
    respondErr("No fault to clear");
  }
}

// =========================================================================
// CTRL:TRIM:* — Speed-trim PI controller commands
// =========================================================================

// Helper: parse "1.50" → 150 (×100 fixed-point)
static int16_t parseGain100(const char *s) {
  int sign = 1;
  if (*s == '-') {
    sign = -1;
    s++;
  }
  int integer = 0;
  while (*s >= '0' && *s <= '9') {
    integer = integer * 10 + (*s - '0');
    s++;
  }
  int frac = 0;
  int fracDigits = 0;
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9' && fracDigits < 2) {
      frac = frac * 10 + (*s - '0');
      s++;
      fracDigits++;
    }
    while (fracDigits < 2) {
      frac *= 10;
      fracDigits++;
    }
  }
  return static_cast<int16_t>(sign * (integer * 100 + frac));
}

void CommandParser::cmdTrimQuery() {
  auto tc = m_dispatcher.getTrimConfig();
  uint8_t maxPct = (tc.kd100 > 0 && tc.kd100 <= 50) ? static_cast<uint8_t>(tc.kd100) : 8;

  TelemetrySnapshot snap = Harness::telemetry().getSnapshot();
  bool active = snap.control.tracking;
  bool frozen = snap.control.trimFrozen != 0;

  if (m_format == ResponseFormat::JSON) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"kp\":\"%d.%02d\",\"ki\":\"%d.%02d\","
             "\"out_limit\":%u,\"i_limit\":%u,\"max_pct\":%u,"
             "\"active\":%s,\"frozen\":%s,"
             "\"base_spd\":%ld,\"trim_spd\":%ld,\"final_spd\":%ld,"
             "\"vel_quality\":%u}",
             tc.kp100 / 100, (tc.kp100 < 0 ? -tc.kp100 : tc.kp100) % 100,
             tc.ki100 / 100, (tc.ki100 < 0 ? -tc.ki100 : tc.ki100) % 100,
             (unsigned)tc.outputLimit, (unsigned)tc.integralLimit,
             (unsigned)maxPct, active ? "true" : "false",
             frozen ? "true" : "false",
             static_cast<long>(snap.control.baseSpeedRaw),
             static_cast<long>(snap.control.trimSpeedRaw),
             static_cast<long>(snap.control.finalSpeedRaw),
             static_cast<unsigned>(snap.control.velQuality));
    respondJsonOk("CTRL:TRIM?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "TRIM Kp=%d.%02d Ki=%d.%02d out=%u i=%u maxPct=%u %s%s",
             tc.kp100 / 100, (tc.kp100 < 0 ? -tc.kp100 : tc.kp100) % 100,
             tc.ki100 / 100, (tc.ki100 < 0 ? -tc.ki100 : tc.ki100) % 100,
             (unsigned)tc.outputLimit, (unsigned)tc.integralLimit,
             (unsigned)maxPct, active ? "ACTIVE" : "IDLE",
             frozen ? " FROZEN" : "");
    respondOk(buf);
  }
}

void CommandParser::cmdTrimSetGains(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:TRIM:GAINS <kp> <ki>");
    return;
  }

  int16_t kp100 = parseGain100(cmd.args[0]);
  int16_t ki100 = parseGain100(cmd.args[1]);

  m_dispatcher.setTrimGains(kp100, ki100);

  // Reconfigure live trim controller if active
  m_dispatcher.applyTrimConfig();

  if (m_format == ResponseFormat::JSON) {
    char json[64];
    snprintf(json, sizeof(json), "{\"kp\":\"%d.%02d\",\"ki\":\"%d.%02d\"}",
             kp100 / 100, (kp100 < 0 ? -kp100 : kp100) % 100, ki100 / 100,
             (ki100 < 0 ? -ki100 : ki100) % 100);
    respondJsonOk("CTRL:TRIM:GAINS", json);
  } else {
    respondOk("Trim gains set");
  }
}

void CommandParser::cmdTrimSetLimits(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:TRIM:LIMITS <output_limit> <integral_limit>");
    return;
  }

  auto outLim = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto iLim = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));

  m_dispatcher.setTrimLimits(outLim, iLim);

  // Reconfigure live trim controller
  m_dispatcher.applyTrimConfig();

  if (m_format == ResponseFormat::JSON) {
    char json[64];
    snprintf(json, sizeof(json), "{\"out_limit\":%u,\"i_limit\":%u}",
             (unsigned)outLim, (unsigned)iLim);
    respondJsonOk("CTRL:TRIM:LIMITS", json);
  } else {
    respondOk("Trim limits set");
  }
}

void CommandParser::cmdTrimSetMaxPct(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: CTRL:TRIM:MAXPCT <1-50>");
    return;
  }

  int val = atoi(cmd.args[0]);
  if (val < 1 || val > 50) {
    respondErr("maxPct must be 1-50");
    return;
  }

  m_dispatcher.setTrimMaxPct(static_cast<int16_t>(val));

  // Reconfigure live trim controller
  m_dispatcher.applyTrimConfig();

  if (m_format == ResponseFormat::JSON) {
    char json[32];
    snprintf(json, sizeof(json), "{\"max_pct\":%d}", val);
    respondJsonOk("CTRL:TRIM:MAXPCT", json);
  } else {
    respondOk("Trim max percent set");
  }
}

void CommandParser::cmdTrimReset() {
  m_dispatcher.resetTrim();
  respondOk("Trim reset");
}

void CommandParser::cmdTrimSave() {
  if (m_dispatcher.configSaveToFlash()) {
    respondOk("Trim config saved");
  } else {
    respondErr("Flash write failed");
  }
}

// --- System Identification commands ---

void CommandParser::cmdSysIdStep(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:SYSID:STEP <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  auto targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs =
      (cmd.argCount >= 3) ? static_cast<uint16_t>(atol(cmd.args[2])) : 5000;
  uint16_t settleMs =
      (cmd.argCount >= 4) ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
    return;
  }

  if (m_dispatcher.sysIdStart(0, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID step started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRamp(const ParsedCommand &cmd) {
  if (cmd.argCount < 3) {
    respondErr("Usage: CTRL:SYSID:RAMP <start_sps> <end_sps> <dir> [dur_ms] "
               "[settle_ms]");
    return;
  }

  auto startSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  auto targetSpd = static_cast<uint16_t>(atol(cmd.args[1]));
  bool forward = (atol(cmd.args[2]) != 0);
  uint16_t durMs =
      (cmd.argCount >= 4) ? static_cast<uint16_t>(atol(cmd.args[3])) : 5000;
  uint16_t settleMs =
      (cmd.argCount >= 5) ? static_cast<uint16_t>(atol(cmd.args[4])) : 1000;

  if (targetSpd > 15625 || startSpd > 15625) {
    respondErr("speed must be 0-15625 sps");
    return;
  }

  if (m_dispatcher.sysIdStart(1, targetSpd, startSpd, forward, durMs, settleMs,
                              0)) {
    respondOk("SYSID ramp started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdStatus() {
  uint8_t phase = m_dispatcher.sysIdGetPhase();
  uint16_t count = m_dispatcher.sysIdGetSampleCount();

  if (m_format == ResponseFormat::JSON) {
    char data[64];
    snprintf(data, sizeof(data), "{\"phase\":%u,\"samples\":%u}",
             static_cast<unsigned>(phase), static_cast<unsigned>(count));
    respondJsonOk("CTRL:SYSID:STATUS", data);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), "phase=%u samples=%u",
             static_cast<unsigned>(phase), static_cast<unsigned>(count));
    respondOk(buf);
  }
}

void CommandParser::cmdSysIdData(const ParsedCommand &cmd) {
  if (m_dispatcher.sysIdGetPhase() != 4) { // 4 = DONE
    respondErr("No data ready (test not DONE)");
    return;
  }

  uint16_t offset =
      (cmd.argCount >= 1) ? static_cast<uint16_t>(atol(cmd.args[0])) : 0;
  uint16_t count =
      (cmd.argCount >= 2) ? static_cast<uint16_t>(atol(cmd.args[1])) : 20;
  uint16_t total;
  const auto *samples = m_dispatcher.sysIdGetSamples(total);

  if (offset >= total) {
    respondErr("offset past end");
    return;
  }
  if (offset + count > total) {
    count = total - offset;
  }

  // Header line
  char buf[64];
  snprintf(buf, sizeof(buf), "SYSID_DATA %u %u %u",
           static_cast<unsigned>(offset), static_cast<unsigned>(count),
           static_cast<unsigned>(total));
  m_transport.println(buf);

  // CSV data: time_ms,setpoint_tps,actual_tps,pos_error
  for (uint16_t i = 0; i < count; i++) {
    const auto &s = samples[offset + i];
    snprintf(buf, sizeof(buf), "%u,%d,%d,%d", static_cast<unsigned>(s.time_ms),
             static_cast<int>(s.setpoint_tps), static_cast<int>(s.actual_tps),
             static_cast<int>(s.pos_error));
    m_transport.println(buf);
  }

  m_transport.println("END");
}

void CommandParser::cmdSysIdSine(const ParsedCommand &cmd) {
  // CTRL:SYSID:SINE <baseline_sps> <amplitude_sps> <freq_mHz> <dir> [dur_ms]
  // [settle_ms]
  if (cmd.argCount < 4) {
    respondErr("Usage: CTRL:SYSID:SINE <baseline> <amplitude> <freq_mHz> <dir> "
               "[dur] [settle]");
    return;
  }

  auto baseline = static_cast<uint16_t>(atol(cmd.args[0]));
  auto amplitude = static_cast<uint16_t>(atol(cmd.args[1]));
  auto freqMHz = static_cast<uint16_t>(atol(cmd.args[2]));
  bool forward = (atol(cmd.args[3]) != 0);
  uint16_t durMs =
      (cmd.argCount >= 5) ? static_cast<uint16_t>(atol(cmd.args[4])) : 10000;
  uint16_t settleMs =
      (cmd.argCount >= 6) ? static_cast<uint16_t>(atol(cmd.args[5])) : 1000;

  if (baseline > 15625 || amplitude > 15625) {
    respondErr("speed must be 0-15625 sps");
    return;
  }
  if (freqMHz == 0 || freqMHz > 5000) {
    respondErr("freq_mHz must be 1-5000 (0.001-5.0 Hz)");
    return;
  }

  if (m_dispatcher.sysIdStart(2, amplitude, baseline, forward, durMs, settleMs,
                              freqMHz)) {
    respondOk("SYSID sine started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdTrapezoid(const ParsedCommand &cmd) {
  // CTRL:SYSID:TRAPEZOID <speed_sps> <dir> [dur_ms] [settle_ms]
  if (cmd.argCount < 2) {
    respondErr(
        "Usage: CTRL:SYSID:TRAPEZOID <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  auto targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs =
      (cmd.argCount >= 3) ? static_cast<uint16_t>(atol(cmd.args[2])) : 8000;
  uint16_t settleMs =
      (cmd.argCount >= 4) ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
    return;
  }

  if (m_dispatcher.sysIdStart(3, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID trapezoid started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRect(const ParsedCommand &cmd) {
  // CTRL:SYSID:RECT <speed_sps> <dir> [dur_ms] [settle_ms]
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:SYSID:RECT <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  auto targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs =
      (cmd.argCount >= 3) ? static_cast<uint16_t>(atol(cmd.args[2])) : 6000;
  uint16_t settleMs =
      (cmd.argCount >= 4) ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
    return;
  }

  if (m_dispatcher.sysIdStart(4, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID rect started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdAbort() {
  m_dispatcher.sysIdAbort();
  respondOk("SYSID aborted");
}

} // namespace Protocol
