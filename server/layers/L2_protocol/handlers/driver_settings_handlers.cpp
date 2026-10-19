/**
 * @file driver_settings_handlers.cpp
 * @brief Protocol 3: the motor driver's capabilities (DRV:CAPS?) and its settings in physical
 *        units (DRV:CFG:CURRENT/DUTY/USTEP/ACCEL/DECEL/MAXSPD/OVERCURRENT/STALL_TH/ALARMS,
 *        DRV:CFG:PHYS?, DRV:USTEP). Driver boundary step 4 (docs/design/DRIVER_BOUNDARY.md).
 *
 * Units: amps, duty 0..1, microsteps per full step, full steps/s^2, full steps/s, a stall
 * threshold on the driver's 0..1 scale. Ranges come from the driver's capabilities
 * (Harness::DriverCaps, Harness::*WithinCaps); a value the driver cannot represent exactly
 * is rounded onto its grid (IMotorDriver::quantize) and every reply shows the quantised value,
 * i.e. what DRV:CFG:APPLY puts in force. The setters change the stored settings only (RAM, like
 * every DRV:CFG command): DRV:CFG:APPLY writes them to the driver and saves; DRV:USTEP is the
 * exception, it changes the resolution at once (the driver releases its outputs if it must).
 *
 * Each setter's "<name>?" twin (same handler) is its query: the same reply, no change.
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "L2_protocol/kv_reply.hpp"

namespace Protocol {

namespace {

/// Chip-neutral fault names, bit i = Harness::DriverFault (1 << i): DRV:CAPS? fault lists,
/// DRV:CFG:ALARMS and the telemetry `faults` mask
constexpr const char* FAULT_NAMES[6] = {
    "overcurrent", "undervoltage", "thermal_shutdown", "thermal_warning", "stall", "command_error"};

size_t faultNames(uint8_t mask, const char* out[6])
{
    size_t n = 0;
    for (unsigned i = 0; i < 6; ++i) {
        if (mask & (1u << i)) out[n++] = FAULT_NAMES[i];
    }
    return n;
}

bool sameText(const char* a, const char* b)          // ASCII, case-insensitive
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 32);
        if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 32);
        if (x != y) return false;
    }
    return *a == *b;
}

/// A whole argument as a finite real number
bool parseReal(const char* s, float& out)
{
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0' || !(v == v) || v > 1e30 || v < -1e30) return false;
    out = static_cast<float>(v);
    return true;
}

bool isQuery(const ParsedCommand& cmd)
{
    const size_t n = strlen(cmd.cmd);
    return n > 0 && cmd.cmd[n - 1] == '?';
}

const char* driveModeName(Harness::DriveMode m)
{
    return m == Harness::DriveMode::CURRENT ? "current" : "voltage";
}

constexpr const char* NOT_SAVED = "(not saved; DRV:CFG:APPLY puts it in force)";

} // namespace

void CommandParser::respondOutOfRange(const char* what, double lo, double hi, const char* unit) {
  char a[20], b[20], msg[96];
  formatReal(a, sizeof(a), lo);
  formatReal(b, sizeof(b), hi);
  snprintf(msg, sizeof(msg), "%s must be %s-%s%s%s", what, a, b, (unit && *unit) ? " " : "",
           unit ? unit : "");
  respondErr("OUT_OF_RANGE", msg);
}

// The reply of a physical setter (changed = true: with the "not saved" note) or its query,
// for one group of settings, in the values the driver puts in force
void CommandParser::replyDriverSettings(const char* what, bool changed) {
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  const Harness::DriverSettings asked = m_dispatcher.driverSettings();
  const Harness::DriverSettings q = m_dispatcher.driverSettingsInForce();
  KvReply r(m_transport, m_format == ResponseFormat::JSON, m_currentCmd, KvReply::Shape::LINE, what);
  if (strcmp(what, "CURRENT") == 0) {
    r.num("hold_a", q.currentA.hold);
    r.num("run_a", q.currentA.run);
    r.num("acc_a", q.currentA.acc);
    r.num("dec_a", q.currentA.dec);
    r.text("drive_mode", driveModeName(q.mode));
  } else if (strcmp(what, "DUTY") == 0) {
    r.num("hold_duty", q.duty.hold);
    r.num("run_duty", q.duty.run);
    r.num("acc_duty", q.duty.acc);
    r.num("dec_duty", q.duty.dec);
    r.text("drive_mode", driveModeName(q.mode));
  } else if (strcmp(what, "USTEP") == 0) {
    r.integer("microsteps_asked", asked.microsteps);
    r.integer("microsteps", q.microsteps);
    r.text("drive_mode", driveModeName(q.mode));
  } else if (strcmp(what, "ACCEL") == 0) {
    r.num("accel_fsps2", q.accelFsps2);
  } else if (strcmp(what, "DECEL") == 0) {
    r.num("decel_fsps2", q.decelFsps2);
  } else if (strcmp(what, "MAXSPD") == 0) {
    r.num("max_speed_fsps", q.maxSpeedFsps);
  } else if (strcmp(what, "OVERCURRENT") == 0) {
    r.num("overcurrent_a", q.overcurrentA);
    if (caps.overcurrentOhms > 0.0f) {        // the chip senses a voltage: say which
      r.num("overcurrent_v", static_cast<double>(q.overcurrentA) * caps.overcurrentOhms);
    }
  } else if (strcmp(what, "STALL_TH") == 0) {
    r.num("stall_threshold", q.stallThreshold01);
  } else if (strcmp(what, "ALARMS") == 0) {
    const char* names[6];
    r.list("alarms", names, faultNames(q.faultAlarms, names));
  }
  r.end(changed ? NOT_SAVED : nullptr);
}

// DRV:CAPS? — what the motor driver can do, units in the key names
void CommandParser::cmdDrvCaps() {
  const Harness::DriverCaps c = m_dispatcher.driverCaps();
  KvReply r(m_transport, m_format == ResponseFormat::JSON, "DRV:CAPS?", KvReply::Shape::LINES);
  r.flag("voltage_mode", c.voltageControl);
  r.flag("current_mode", c.currentControl);
  r.flag("current_settable", c.currentSettable);
  r.integer("max_microsteps_voltage", c.maxMicrostepsVoltage);
  r.integer("max_microsteps_current", c.maxMicrostepsCurrent);
  r.flag("stall_detect", c.stallDetect);
  r.flag("standby_pin", c.standbyPin);
  r.flag("hardware_positioning", c.hardwarePositioning);
  r.integer("sync_pulses_per_full_step", c.syncPulsesPerFullStep);
  r.list("fault_bits", FAULT_NAMES, 6);
  const char* names[6];
  r.list("faults_reported", names, faultNames(c.faultsReported, names));
  r.list("faults_alarmable", names, faultNames(c.faultsAlarmable, names));
  r.num("max_current_a", c.maxCurrentA);
  r.num("current_step_a", c.currentStepA);
  r.num("duty_step", c.dutyStep);
  r.text("overcurrent_sense", c.overcurrentOhms > 0.0f ? "voltage" : "current");
  r.num("overcurrent_ohms", c.overcurrentOhms);
  r.num("overcurrent_min_a", c.overcurrentMinA);
  r.num("overcurrent_max_a", c.overcurrentMaxA);
  r.num("overcurrent_step_a", c.overcurrentStepA);
  r.num("stall_step", c.stallStep01);
  r.num("max_speed_fsps", c.maxSpeedFsps);
  r.num("max_speed_limit_fsps", c.maxSpeedLimitFsps);
  r.num("speed_limit_step_fsps", c.speedLimitStepFsps);
  r.num("accel_min_fsps2", c.accelMinFsps2);
  r.num("accel_max_fsps2", c.accelMaxFsps2);
  r.num("accel_step_fsps2", c.accelStepFsps2);
  r.integer("min_position_usteps", c.minPositionUsteps);
  r.integer("max_position_usteps", c.maxPositionUsteps);
  r.end();
}

// DRV:CFG:PHYS? — every driver setting, quantised by the driver (what APPLY puts in force),
// plus the stored request where the driver caps it, and the non-driver motor settings
void CommandParser::cmdDrvPhysShow() {
  const Harness::DriverCaps c = m_dispatcher.driverCaps();
  const Harness::DriverSettings asked = m_dispatcher.driverSettings();
  const Harness::DriverSettings q = m_dispatcher.driverSettingsInForce();
  const auto cfg = m_dispatcher.getMotorConfig();
  KvReply r(m_transport, m_format == ResponseFormat::JSON, "DRV:CFG:PHYS?", KvReply::Shape::LINES);
  r.text("source", cfg.valid ? "flash" : "defaults");
  r.text("drive_mode", driveModeName(q.mode));
  r.integer("microsteps_asked", asked.microsteps);
  r.integer("microsteps", q.microsteps);
  if (c.currentControl && c.currentSettable) {
    r.num("hold_a", q.currentA.hold);
    r.num("run_a", q.currentA.run);
    r.num("acc_a", q.currentA.acc);
    r.num("dec_a", q.currentA.dec);
  }
  if (c.voltageControl) {
    r.num("hold_duty", q.duty.hold);
    r.num("run_duty", q.duty.run);
    r.num("acc_duty", q.duty.acc);
    r.num("dec_duty", q.duty.dec);
  }
  r.num("accel_fsps2", q.accelFsps2);
  r.num("decel_fsps2", q.decelFsps2);
  r.num("max_speed_fsps", q.maxSpeedFsps);
  r.num("overcurrent_a", q.overcurrentA);
  if (c.overcurrentOhms > 0.0f) {
    r.num("overcurrent_v", static_cast<double>(q.overcurrentA) * c.overcurrentOhms);
  }
  if (c.stallDetect) {
    r.num("stall_threshold", q.stallThreshold01);
  }
  const char* names[6];
  r.list("alarms", names, faultNames(q.faultAlarms, names));
  r.text("fault_action", cfg.faultAction == 0 ? "HardStop" : cfg.faultAction == 1 ? "HardHiZ" : "SoftStop");
  r.integer("idle_release_ms", cfg.idleReleaseMs);
  r.integer("full_steps_per_rev", m_dispatcher.getFullStepsPerRev());
  r.end();
}

// DRV:CFG:CURRENT <hold_A> <run_A> <acc_A> <dec_A> — current-mode references, amps
void CommandParser::cmdDrvCurrent(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("CURRENT", false); return; }
  if (cmd.argCount < 4) {
    respondErr("Usage: DRV:CFG:CURRENT <hold_A> <run_A> <acc_A> <dec_A>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!caps.currentControl) {
    respondErr("INVALID_STATE", "the driver has no current mode");
    return;
  }
  if (!caps.currentSettable) {
    respondErr("INVALID_STATE", "the driver's current is set in hardware");
    return;
  }
  float v[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (!parseReal(cmd.args[i], v[i])) {
      respondErr("Usage: DRV:CFG:CURRENT <hold_A> <run_A> <acc_A> <dec_A>");
      return;
    }
    if (!Harness::currentWithinCaps(caps, v[i])) {
      respondOutOfRange("current", 0.0, caps.maxCurrentA, "A");
      return;
    }
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.currentA = Harness::DriveLevels{v[0], v[1], v[2], v[3]};
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("CURRENT", true);
}

// DRV:CFG:DUTY <hold> <run> <acc> <dec> — voltage-mode drive duty, 0..1 of the supply
void CommandParser::cmdDrvDuty(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("DUTY", false); return; }
  if (cmd.argCount < 4) {
    respondErr("Usage: DRV:CFG:DUTY <hold> <run> <acc> <dec> (0..1)");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!caps.voltageControl) {
    respondErr("INVALID_STATE", "the driver has no voltage mode");
    return;
  }
  float v[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (!parseReal(cmd.args[i], v[i])) {
      respondErr("Usage: DRV:CFG:DUTY <hold> <run> <acc> <dec> (0..1)");
      return;
    }
    if (!Harness::dutyWithinCaps(caps, v[i])) {
      respondOutOfRange("duty", 0.0, 1.0, "");
      return;
    }
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.duty = Harness::DriveLevels{v[0], v[1], v[2], v[3]};
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("DUTY", true);
}

// DRV:CFG:USTEP <n> — microsteps per full step asked for (a power of two); the driver caps
// it for the drive mode (the reply says what is in force); APPLY writes it
void CommandParser::cmdDrvUstepConfig(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("USTEP", false); return; }
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:CFG:USTEP <microsteps 1|2|4|...>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  char* end = nullptr;
  const unsigned long n = strtoul(cmd.args[0], &end, 10);
  if (end == cmd.args[0] || *end != '\0' || !Harness::microstepsWithinCaps(caps, n)) {
    respondOutOfRange("microsteps (a power of two)", 1.0, caps.maxMicrostepsAny(), "");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.microsteps = static_cast<uint16_t>(n);
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("USTEP", true);
}

// DRV:CFG:ACCEL <full_steps/s^2> — stored profile acceleration (MOT:CFG:ACCEL is the live one)
void CommandParser::cmdDrvAccel(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("ACCEL", false); return; }
  float v = 0.0f;
  if (cmd.argCount < 1 || !parseReal(cmd.args[0], v)) {
    respondErr("Usage: DRV:CFG:ACCEL <full_steps/s^2>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!Harness::accelWithinCaps(caps, v)) {
    respondOutOfRange("accel", caps.accelMinFsps2, caps.accelMaxFsps2, "full steps/s^2");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.accelFsps2 = v;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("ACCEL", true);
}

// DRV:CFG:DECEL <full_steps/s^2> — stored profile deceleration (MOT:CFG:DECEL is the live one)
void CommandParser::cmdDrvDecel(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("DECEL", false); return; }
  float v = 0.0f;
  if (cmd.argCount < 1 || !parseReal(cmd.args[0], v)) {
    respondErr("Usage: DRV:CFG:DECEL <full_steps/s^2>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!Harness::accelWithinCaps(caps, v)) {
    respondOutOfRange("decel", caps.accelMinFsps2, caps.accelMaxFsps2, "full steps/s^2");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.decelFsps2 = v;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("DECEL", true);
}

// DRV:CFG:MAXSPD <full_steps/s> — stored profile speed ceiling (MOT:CFG:MAXSPD is the live one)
void CommandParser::cmdDrvMaxSpeed(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("MAXSPD", false); return; }
  float v = 0.0f;
  if (cmd.argCount < 1 || !parseReal(cmd.args[0], v)) {
    respondErr("Usage: DRV:CFG:MAXSPD <full_steps/s>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!Harness::speedLimitWithinCaps(caps, v)) {
    respondOutOfRange("max speed", caps.speedLimitStepFsps, caps.maxSpeedLimitFsps, "full steps/s");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.maxSpeedFsps = v;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("MAXSPD", true);
}

// DRV:CFG:OVERCURRENT <A> — overcurrent detection threshold, amps. When the driver senses a
// voltage (DRV:CAPS? overcurrent_sense=voltage) the amps are an estimate and the reply also
// gives the volts the chip compares (overcurrent_v)
void CommandParser::cmdDrvOvercurrent(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("OVERCURRENT", false); return; }
  float v = 0.0f;
  if (cmd.argCount < 1 || !parseReal(cmd.args[0], v)) {
    respondErr("Usage: DRV:CFG:OVERCURRENT <amps>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!Harness::overcurrentWithinCaps(caps, v)) {
    respondOutOfRange("overcurrent", caps.overcurrentMinA, caps.overcurrentMaxA, "A");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.overcurrentA = v;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("OVERCURRENT", true);
}

// DRV:CFG:STALL_TH <0..1> — stall threshold on the driver's scale (0 most sensitive)
void CommandParser::cmdDrvStallThreshold(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("STALL_TH", false); return; }
  float v = 0.0f;
  if (cmd.argCount < 1 || !parseReal(cmd.args[0], v)) {
    respondErr("Usage: DRV:CFG:STALL_TH <0..1>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  if (!caps.stallDetect) {
    respondErr("INVALID_STATE", "the driver has no stall detection");
    return;
  }
  if (!Harness::stallWithinCaps(caps, v)) {
    respondOutOfRange("stall threshold", 0.0, 1.0, "");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.stallThreshold01 = v;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("STALL_TH", true);
}

// DRV:CFG:ALARMS <name> [name...] | NONE | ALL — which faults raise the driver's alarm
// (names as in DRV:CAPS? faults_alarmable, case-insensitive)
void CommandParser::cmdDrvAlarms(const ParsedCommand &cmd) {
  if (isQuery(cmd)) { replyDriverSettings("ALARMS", false); return; }
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:CFG:ALARMS <fault> [fault...] | NONE | ALL");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  uint8_t mask = 0;
  for (uint8_t a = 0; a < cmd.argCount; ++a) {
    if (sameText(cmd.args[a], "NONE")) continue;
    if (sameText(cmd.args[a], "ALL")) { mask |= caps.faultsAlarmable; continue; }
    uint8_t bit = 0;
    for (unsigned i = 0; i < 6; ++i) {
      if (sameText(cmd.args[a], FAULT_NAMES[i])) bit = static_cast<uint8_t>(1u << i);
    }
    if (bit == 0 || (bit & caps.faultsAlarmable) == 0) {
      respondErr("INVALID_PARAM", "unknown or not alarmable fault (DRV:CAPS? faults_alarmable)");
      return;
    }
    mask |= bit;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();
  s.faultAlarms = mask;
  const auto st = m_dispatcher.configSetDriverSettings(s);
  if (st != ServiceStatus::Ok) { respondErr(statusToCode(st), statusToString(st)); return; }
  replyDriverSettings("ALARMS", true);
}

// DRV:USTEP <n> — change the resolution now (motor idle; the driver releases its outputs for
// the write if it must) and keep it as the request; the reply is what the driver reads back
void CommandParser::cmdDrvUstep(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:USTEP <microsteps 1|2|4|...>");
    return;
  }
  const Harness::DriverCaps caps = m_dispatcher.driverCaps();
  char* end = nullptr;
  const unsigned long n = strtoul(cmd.args[0], &end, 10);
  if (end == cmd.args[0] || *end != '\0' || !Harness::microstepsWithinCaps(caps, n)) {
    respondOutOfRange("microsteps (a power of two)", 1.0, caps.maxMicrostepsAny(), "");
    return;
  }
  Protocol::TelemetrySnapshot snap;
  m_dispatcher.getTelemetry(snap);
  if (snap.motor.busy) {
    respondErr("MOTOR_BUSY", "Motor must be idle to change the resolution");
    return;
  }
  uint8_t readback = 0;
  if (!m_dispatcher.motorSetStepModeSafe(Harness::microstepsLog2(static_cast<uint16_t>(n)), readback)) {
    respondErr("VERIFY_FAILED", "the driver did not take the resolution");
    return;
  }
  Harness::DriverSettings s = m_dispatcher.driverSettings();     // RAM: SAVE keeps it
  s.microsteps = static_cast<uint16_t>(n);
  (void)m_dispatcher.configSetDriverSettings(s);
  KvReply r(m_transport, m_format == ResponseFormat::JSON, "DRV:USTEP", KvReply::Shape::LINE, "USTEP");
  r.integer("microsteps_asked", static_cast<long>(n));
  r.integer("microsteps", static_cast<long>(1UL << (readback > 15 ? 15 : readback)));
  r.integer("microsteps_per_rev", static_cast<long>(m_dispatcher.getMicrostepsPerRev()));
  r.end();
}

// DRV:USTEP? — the resolution in force, read back from the driver
void CommandParser::cmdDrvUstepQuery() {
  Harness::MotorDebugParams info = {};
  m_dispatcher.motorGetDebugInfo(info, Harness::DriverDiagView::NONE);
  const uint16_t us = info.microsteps != 0 ? info.microsteps : 1;
  KvReply r(m_transport, m_format == ResponseFormat::JSON, "DRV:USTEP?", KvReply::Shape::LINE, "USTEP");
  r.integer("microsteps", us);
  r.integer("microsteps_per_rev", static_cast<long>(m_dispatcher.getFullStepsPerRev()) * us);
  r.text("drive_mode", info.driveMode ? "current" : "voltage");
  r.end();
}

} // namespace Protocol
