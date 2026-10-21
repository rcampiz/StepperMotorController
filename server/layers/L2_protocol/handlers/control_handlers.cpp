/**
 * @file control_handlers.cpp
 * @brief CommandParser control command handlers (CTRL namespace — mode, follow, trim, sysid)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "L2_protocol/status_json.hpp"
#include "L2_protocol/kv_reply.hpp"

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
  respondStatus(m_dispatcher.configSaveToFlash(), "Follow config saved");
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

  TelemetrySnapshot snap;
  m_dispatcher.getTelemetry(snap);
  bool active = snap.control.tracking;
  bool frozen = snap.control.trimFrozen != 0;
  const Protocol2TrimSpeeds spd = protocol2TrimSpeeds(snap.control);   // protocol 2 wire unit

  if (m_format == ResponseFormat::JSON) {
    char baseFs[16], trimFs[16], finalFs[16];                         // protocol 3: full steps/s
    formatFixed(baseFs, sizeof(baseFs), clampSpeedFsps(snap.control.baseSpeedFsps), 1);
    formatFixed(trimFs, sizeof(trimFs), clampSpeedFsps(snap.control.trimSpeedFsps), 1);
    formatFixed(finalFs, sizeof(finalFs), clampSpeedFsps(snap.control.finalSpeedFsps), 1);
    char json[352];
    snprintf(json, sizeof(json),
             "{\"kp\":\"%d.%02d\",\"ki\":\"%d.%02d\","
             "\"out_limit\":%u,\"i_limit\":%u,\"max_pct\":%u,"
             "\"active\":%s,\"frozen\":%s,"
             "\"base_spd\":%ld,\"trim_spd\":%ld,\"final_spd\":%ld,"
             "\"base_fsps\":%s,\"trim_fsps\":%s,\"final_fsps\":%s,"
             "\"vel_quality\":%u}",
             tc.kp100 / 100, (tc.kp100 < 0 ? -tc.kp100 : tc.kp100) % 100,
             tc.ki100 / 100, (tc.ki100 < 0 ? -tc.ki100 : tc.ki100) % 100,
             (unsigned)tc.outputLimit, (unsigned)tc.integralLimit,
             (unsigned)maxPct, active ? "true" : "false",
             frozen ? "true" : "false",
             spd.base, spd.trim, spd.final_, baseFs, trimFs, finalFs,
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
  respondStatus(m_dispatcher.configSaveToFlash(), "Trim config saved");
}

// =========================================================================
// CTRL:RES:* — Resonance avoider (the run current moves the resonance off the speed)
// =========================================================================

namespace {

bool parseResReal(const char *s, float &out) {
  if (s == nullptr || *s == '\0') return false;
  char *end = nullptr;
  const double v = strtod(s, &end);
  if (end == s || *end != '\0' || !(v == v)) return false;
  out = static_cast<float>(v);
  return true;
}

const char *resStateName(uint8_t s) {
  switch (static_cast<Harness::ResonanceState>(s)) {
    case Harness::ResonanceState::NOMINAL: return "nominal";
    case Harness::ResonanceState::BOOST: return "boost";
    case Harness::ResonanceState::CUT: return "cut";
    case Harness::ResonanceState::UNAVOIDABLE: return "unavoidable";
    default: return "off";
  }
}

} // namespace

// CTRL:RES? — the stored model and limits (defaults filled in), the live state and the
// resonant speeds (harmonics 1, 2, 4) at the configured run current
void CommandParser::cmdResQuery() {
  const auto s = m_dispatcher.getResonance();
  char f1a[16], ex[16], runIn[16], runCfg[16], r1[16], r2[16], r4[16];
  formatFixed(f1a, sizeof(f1a), s.config.f1AHz, 1);
  formatFixed(ex, sizeof(ex), s.config.exponent, 2);
  formatFixed(runIn, sizeof(runIn), s.runInForceA, 3);
  formatFixed(runCfg, sizeof(runCfg), s.runConfiguredA, 3);
  formatFixed(r1, sizeof(r1), s.resonantRpm[0], 1);
  formatFixed(r2, sizeof(r2), s.resonantRpm[1], 1);
  formatFixed(r4, sizeof(r4), s.resonantRpm[2], 1);
  const unsigned band = s.config.bandPct ? s.config.bandPct : Harness::RES_DEFAULT_BAND_PCT;
  const unsigned boost = s.config.boostPct ? s.config.boostPct : Harness::RES_DEFAULT_BOOST_PCT;
  const unsigned cut = s.config.cutPct ? s.config.cutPct : Harness::RES_DEFAULT_CUT_PCT;
  const unsigned mask = s.config.harmonicMask ? s.config.harmonicMask : Harness::RES_DEFAULT_HARMONICS;
  char buf[384];
  if (m_format == ResponseFormat::JSON) {
    snprintf(buf, sizeof(buf),
             "{\"enabled\":%s,\"state\":\"%s\",\"run_a\":%s,\"run_cfg_a\":%s,"
             "\"f1a_hz\":%s,\"exponent\":%s,\"band_pct\":%u,\"boost_pct\":%u,\"cut_pct\":%u,"
             "\"harmonics\":%u,\"rpm_k1\":%s,\"rpm_k2\":%s,\"rpm_k4\":%s,"
             "\"changes\":%lu,\"guard_trips\":%lu}",
             s.config.enabled ? "true" : "false", resStateName(s.state), runIn, runCfg, f1a, ex,
             band, boost, cut, mask, r1, r2, r4, static_cast<unsigned long>(s.changes),
             static_cast<unsigned long>(s.guardTrips));
    respondJsonOk("CTRL:RES?", buf);
  } else {
    snprintf(buf, sizeof(buf),
             "enabled=%u state=%s run=%sA cfg=%sA f1a=%sHz exp=%s band=%u%% boost=%u%% cut=%u%%"
             " mask=%u rpm_k1=%s rpm_k2=%s rpm_k4=%s changes=%lu guard_trips=%lu",
             s.config.enabled ? 1u : 0u, resStateName(s.state), runIn, runCfg, f1a, ex, band,
             boost, cut, mask, r1, r2, r4, static_cast<unsigned long>(s.changes),
             static_cast<unsigned long>(s.guardTrips));
    respondOk(buf);
  }
}

// CTRL:RES:CFG <enable 0|1> [f1a_hz] [exponent] [band_pct] [boost_pct] [cut_pct] [harmonic_mask]
// In force on the Motor task's next poll; CTRL:RES:SAVE keeps it. Omitted values stay.
void CommandParser::cmdResConfig(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: CTRL:RES:CFG <0|1> [f1a_hz] [exponent] [band_pct] [boost_pct] [cut_pct] [mask 1-7]");
    return;
  }
  Harness::ResonanceConfig c = m_dispatcher.getResonance().config;
  c.enabled = strtol(cmd.args[0], nullptr, 10) != 0;
  float v = 0.0f;
  if (cmd.argCount >= 2) {
    if (!parseResReal(cmd.args[1], v) || v < 1.0f || v > 2000.0f) { respondErr("f1a_hz is 1-2000"); return; }
    c.f1AHz = v;
  }
  if (cmd.argCount >= 3) {
    if (!parseResReal(cmd.args[2], v) || v < 0.05f || v > 2.0f) { respondErr("exponent is 0.05-2"); return; }
    c.exponent = v;
  }
  const long lim[4][2] = {{1, 30}, {0, 100}, {0, 90}, {1, 7}};
  uint8_t *dst[4] = {&c.bandPct, &c.boostPct, &c.cutPct, &c.harmonicMask};
  for (unsigned i = 0; i < 4 && 3u + i < cmd.argCount; ++i) {
    const long n = strtol(cmd.args[3 + i], nullptr, 10);
    if (n < lim[i][0] || n > lim[i][1]) { respondErr("band 1-30, boost 0-100, cut 0-90, mask 1-7"); return; }
    *dst[i] = static_cast<uint8_t>(n);
  }
  if (c.enabled && !(c.f1AHz > 0.0f)) { respondErr("No model: give f1a_hz and exponent"); return; }
  m_dispatcher.setResonanceConfig(c);
  respondOk(c.enabled ? "Resonance avoider on (not saved; CTRL:RES:SAVE keeps it)"
                      : "Resonance avoider off (not saved; CTRL:RES:SAVE keeps it)");
}

void CommandParser::cmdResSave() {
  respondStatus(m_dispatcher.configSaveToFlash(), "Resonance config saved");
}

// --- System Identification commands ---

void CommandParser::cmdSysIdStep(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
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

  if (targetSpd == 0 || targetSpd > lim.speedMax) {
    respondRange("speed_sps must be 1-%ld", lim.speedMax);
    return;
  }

  if (m_dispatcher.sysIdStart(0, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID step started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRamp(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
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

  if (targetSpd > lim.speedMax || startSpd > lim.speedMax) {
    respondRange("speed must be 0-%ld sps", lim.speedMax);
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

  // F12: terminated reply. JSON carries offset/samples/total as fields; ASCII keeps the
  // header line and ends with OK <n> instead of END.
  char extra[64];
  snprintf(extra, sizeof(extra), "\"offset\":%u,\"samples\":%u,\"total\":%u",
           static_cast<unsigned>(offset), static_cast<unsigned>(count),
           static_cast<unsigned>(total));
  m_sink.begin(m_format == ResponseFormat::JSON, "SYSID_DATA", extra);
  char buf[64];
  if (m_format != ResponseFormat::JSON) {
    snprintf(buf, sizeof(buf), "SYSID_DATA %u %u %u",
             static_cast<unsigned>(offset), static_cast<unsigned>(count),
             static_cast<unsigned>(total));
    m_sink.writeLine(buf);
  }

  // CSV data: time_ms,setpoint_tps,actual_tps,pos_error
  for (uint16_t i = 0; i < count; i++) {
    const auto &s = samples[offset + i];
    snprintf(buf, sizeof(buf), "%u,%d,%d,%d", static_cast<unsigned>(s.time_ms),
             static_cast<int>(s.setpoint_tps), static_cast<int>(s.actual_tps),
             static_cast<int>(s.pos_error));
    m_sink.writeLine(buf);
  }
  m_sink.end();
}

void CommandParser::cmdSysIdSine(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
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

  if (baseline > lim.speedMax || amplitude > lim.speedMax) {
    respondRange("speed must be 0-%ld sps", lim.speedMax);
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
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
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

  if (targetSpd == 0 || targetSpd > lim.speedMax) {
    respondRange("speed_sps must be 1-%ld", lim.speedMax);
    return;
  }

  if (m_dispatcher.sysIdStart(3, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID trapezoid started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRect(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
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

  if (targetSpd == 0 || targetSpd > lim.speedMax) {
    respondRange("speed_sps must be 1-%ld", lim.speedMax);
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

// ---------------------------------------------------------------------------
// Position capture: the DMA rings (encoder count + commanded-step SYNC count, sampled at
// the same instants at the encoder sample rate, CTRL:ENC:FILT:RATE) frozen and paged out.
// ---------------------------------------------------------------------------

// CTRL:CAP:FREEZE - stop the rings; reply with their size and rate
void CommandParser::cmdCaptureFreeze() {
  uint32_t samples = 0;
  uint16_t rateHz = 0;
  bool sync = false;
  if (!m_dispatcher.captureFreeze(samples, rateHz, sync)) {
    respondErr("INVALID_STATE", "No encoder sampler to capture");
    return;
  }
  // Pulses the driver puts on its step-sync output per full step (a capability)
  const unsigned pulsesPerFullStep = m_dispatcher.driverCaps().syncPulsesPerFullStep;
  if (m_format == ResponseFormat::JSON) {
    char buf[112];
    snprintf(buf, sizeof(buf),
             "{\"samples\":%lu,\"rate_hz\":%u,\"sync\":%s,\"pulses_per_full_step\":%u}",
             static_cast<unsigned long>(samples), static_cast<unsigned>(rateHz),
             sync ? "true" : "false", pulsesPerFullStep);
    respondJsonOk("CAP_FREEZE", buf);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "OK CAP_FREEZE %lu %u %u %u", static_cast<unsigned long>(samples),
             static_cast<unsigned>(rateHz), sync ? 1U : 0U,
             pulsesPerFullStep);
    m_transport.println(buf);
  }
}

// CTRL:CAP:DATA? <offset> <count> - lines "enc,sync" (16-bit wrapped), oldest first
void CommandParser::cmdCaptureData(const ParsedCommand &cmd) {
  uint32_t offset = (cmd.argCount >= 1) ? static_cast<uint32_t>(atol(cmd.args[0])) : 0;
  uint32_t count = (cmd.argCount >= 2) ? static_cast<uint32_t>(atol(cmd.args[1])) : 200;
  if (count > 256) count = 256;
  uint16_t enc = 0, sync = 0;
  if (!m_dispatcher.captureRead(offset, enc, sync)) {
    respondErr("INVALID_STATE", "No frozen capture at that offset (CTRL:CAP:FREEZE first)");
    return;
  }
  char extra[48];
  snprintf(extra, sizeof(extra), "\"offset\":%lu", static_cast<unsigned long>(offset));
  m_sink.begin(m_format == ResponseFormat::JSON, "CAP_DATA", extra);
  char buf[32];
  for (uint32_t i = 0; i < count; i++) {
    if (!m_dispatcher.captureRead(offset + i, enc, sync)) {
      break;
    }
    snprintf(buf, sizeof(buf), "%u,%u", static_cast<unsigned>(enc), static_cast<unsigned>(sync));
    m_sink.writeLine(buf);
  }
  m_sink.end();
}

// CTRL:CAP:RESUME - restart sampling at the current rate
void CommandParser::cmdCaptureResume() {
  m_dispatcher.captureResume();
  respondOk("Sampling resumed");
}

} // namespace Protocol
