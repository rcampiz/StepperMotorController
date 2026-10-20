/**
 * @file system_handlers.cpp
 * @brief CommandParser system command handlers (SYST, DIAG, DEV, FMT, DBG:TRACE)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "L2_protocol/status_json.hpp"
#include "L2_protocol/kv_reply.hpp"
#include "harness/pins/protocol_version.hpp"
#include "F_platform/boundary/boot_id.hpp"
#include "F_platform/boundary/itask_stats.hpp"
#include "F_platform/boundary/imemory_stats.hpp"
#include "harness/pins/async_event.hpp"
#include "harness/pins/motor_state.hpp"
#include "harness/trace/harness_meter.hpp"

namespace Protocol {

void CommandParser::cmdPing(const ParsedCommand &cmd) {
  // Capture RX timestamp (ideally would be captured at byte reception, but
  // capturing at dispatch start is a reasonable approximation)
  uint32_t rx_tick = m_dispatcher.getTickUs();

  // Parse sequence number
  uint32_t seq = 0;
  if (cmd.argCount >= 1) {
    const char *s = cmd.args[0];
    while (*s >= '0' && *s <= '9') {
      seq = seq * 10 + (*s - '0');
      s++;
    }
  }

  // Capture TX timestamp just before transmit
  uint32_t tx_tick = m_dispatcher.getTickUs();

  // Get current state
  const char *stateStr = controllerStateToString(m_dispatcher.getControllerState());

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"rx_tick\":%lu,\"tx_tick\":%lu,\"state\":\"%s\"}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick), stateStr);
    respondJsonOk("PING", buf);
  } else {
    // ASCII format: PONG <seq> <mcu_rx_tick> <mcu_tx_tick> <state>
    char buf[80];
    snprintf(buf, sizeof(buf), "PONG %lu %lu %lu %s",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick), stateStr);
    m_transport.println(buf);
  }
}

void CommandParser::cmdGetTick() {
  uint32_t tick = m_dispatcher.getTickUs();

  if (m_format == ResponseFormat::JSON) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"tick\":%lu}",
             static_cast<unsigned long>(tick));
    respondJsonOk("GET_TICK", buf);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "OK %lu", static_cast<unsigned long>(tick));
    m_transport.println(buf);
  }
}


// ---------------------------------------------------------------------------
// F4: DIAG:STAT? JSON render (pure; worst-case length is host-tested)
// ---------------------------------------------------------------------------
int renderStatusJson(char* buf, size_t bufSize,
                     const TelemetrySnapshot& snap, const StatusJsonView& v,
                     const char* head) {
  // newlib-nano: no %lld — pre-format int64_t as string
  char encCountStr[24];
  i64toa(snap.encoder.count, encCountStr, sizeof(encCountStr));
  const Protocol2TrimSpeeds spd = protocol2TrimSpeeds(snap.control);   // protocol 2 wire unit
  // Protocol 3: base and final speed in full steps/s (one decimal; trim = final - base, as
  // CTRL:TRIM? gives it; base_spd/trim_spd/final_spd are deprecated), the driver's chip-neutral
  // fault bits and its opaque status word
  char baseFs[12], finalFs[12];
  formatFixed(baseFs, sizeof(baseFs), clampSpeedFsps(snap.control.baseSpeedFsps), 1);
  formatFixed(finalFs, sizeof(finalFs), clampSpeedFsps(snap.control.finalSpeedFsps), 1);
  return snprintf(
      buf, bufSize,
        "%s"
        "{\"state\":\"%s\",\"tick\":%lu,\"queue_depth\":%u,"
        "\"motor\":{\"position\":%ld,\"speed\":%lu,\"busy\":%s,\"hi_z\":%s,"
        "\"direction\":%d,\"mot_status\":%u,\"status_reg\":\"%04X\","
        "\"cmd_err\":%s,\"ocd\":%s,\"thermal_sd\":%s,"
        "\"thermal_warn\":%s,\"uvlo\":%s,\"stall_a\":%s,\"stall_b\":%s,"
        "\"faults\":%u,\"driver_raw\":%lu},"
        "\"encoder\":{\"count\":%s,\"velocity\":%ld,\"index_seen\":%s,"
        "\"revolutions\":%ld,\"index_period_us\":%lu,\"vel_quality\":%u},"
        "\"control\":{\"following_error\":%ld,\"setpoint\":%ld,"
        "\"mode\":%u,\"tracking\":%s,"
        "\"trim_out\":%d,\"p\":%d,\"i\":%d,\"d\":%d,"
        "\"sup_state\":%u,\"tier\":%u,\"vel_error\":%ld,\"retries\":%u,"
        "\"base_spd\":%ld,\"trim_spd\":%ld,\"final_spd\":%ld,"
        "\"base_fsps\":%s,\"final_fsps\":%s,"
        "\"trim_frozen\":%u,\"vel_quality\":%u},"
        "\"heartbeat\":{\"enabled\":%s,\"timeout_ms\":%lu,"
        "\"remaining_ms\":%lu,\"timed_out\":%s},"
        "\"system\":{\"uptime_ticks\":%lu,\"free_heap\":%lu,"
      "\"rx_overflows\":%lu,\"rx_overflow_lines\":%lu},"
      "\"mode\":\"%s\",\"encoder_status\":\"%s\",\"error\":%s}}",
        head, v.stateStr, static_cast<unsigned long>(v.tick),
        static_cast<unsigned>(v.queueDepth),
        static_cast<long>(snap.motor.position),
        static_cast<unsigned long>(snap.motor.speed),
        snap.motor.busy ? "true" : "false", snap.motor.hiZ ? "true" : "false",
        v.direction, v.motStat, static_cast<unsigned>(v.statusReg),
        v.cmdErr ? "true" : "false", v.ocd ? "true" : "false",
        v.thermalSD ? "true" : "false", v.thermalWarn ? "true" : "false",
        v.uvlo ? "true" : "false", v.stallA ? "true" : "false",
        v.stallB ? "true" : "false",
        static_cast<unsigned>(snap.motor.faults & 0x3Fu), static_cast<unsigned long>(snap.motor.statusReg),
        encCountStr,
        static_cast<long>(snap.encoder.velocity),
        snap.encoder.indexSeen ? "true" : "false",
        static_cast<long>(snap.encoder.revolutions),
        static_cast<unsigned long>(snap.encoder.indexPeriodUs),
        static_cast<unsigned>(snap.encoder.velocityQuality),
        static_cast<long>(snap.control.followingError),
        static_cast<long>(snap.control.setpoint),
        static_cast<unsigned>(snap.control.mode),
        snap.control.tracking ? "true" : "false",
        static_cast<int>(snap.control.pidOutput),
        static_cast<int>(snap.control.pTerm),
        static_cast<int>(snap.control.iTerm),
        static_cast<int>(snap.control.dTerm),
        static_cast<unsigned>(snap.control.supervisorState),
        static_cast<unsigned>(snap.control.currentTier),
        static_cast<long>(snap.control.velError),
        static_cast<unsigned>(snap.control.retryCount),
        spd.base, spd.trim, spd.final_, baseFs, finalFs,
        static_cast<unsigned>(snap.control.trimFrozen),
        static_cast<unsigned>(snap.control.velQuality),
        v.hbEnabled ? "true" : "false", static_cast<unsigned long>(v.hbTimeoutMs),
        static_cast<unsigned long>(v.hbRemainingMs), v.hbTimedOut ? "true" : "false",
        static_cast<unsigned long>(v.uptimeTicks), static_cast<unsigned long>(v.freeHeap),
      static_cast<unsigned long>(v.rxOverflows), static_cast<unsigned long>(v.rxOverflowLines),
      v.modeStr, v.encStatusStr, v.anyError ? "true" : "false");
}

int renderCompactJson(char* buf, size_t bufSize,
                      const TelemetrySnapshot& snap, const StatusJsonView& v) {
  char encCountStr[24];
  i64toa(snap.encoder.count, encCountStr, sizeof(encCountStr));
  return snprintf(
      buf, bufSize,
      "%s"
      "{\"state\":\"%s\",\"tick\":%lu,"
      "\"motor\":{\"position\":%ld,\"speed\":%lu,\"busy\":%s,\"hi_z\":%s},"
      "\"encoder\":{\"count\":%s,\"velocity\":%ld},"
      "\"control\":{\"following_error\":%ld},"
      "\"heartbeat\":{\"remaining_ms\":%lu}}}",
      TELEMETRY_JSON_HEAD, v.stateStr, static_cast<unsigned long>(v.tick),
      static_cast<long>(snap.motor.position), static_cast<unsigned long>(snap.motor.speed),
      snap.motor.busy ? "true" : "false", snap.motor.hiZ ? "true" : "false",
      encCountStr, static_cast<long>(snap.encoder.velocity),
      static_cast<long>(snap.control.followingError),
      static_cast<unsigned long>(v.hbRemainingMs));
}

// Everything DIAG:STAT? reports beyond the snapshot; shared with the F9 telemetry push so
// both carry exactly the same `data` object.
void CommandParser::fillStatusView(const TelemetrySnapshot &snap, StatusJsonView &v) {
  v.tick = m_dispatcher.getTickUs();
  v.stateStr = controllerStateToString(m_dispatcher.getControllerState());
  v.queueDepth = m_dispatcher.getQueueDepth();
  v.modeStr = controlModeToString(m_dispatcher.getControlMode());
  v.encStatusStr = encoderStatusToString(m_dispatcher.getEncoderStatus());

  // Direction and fault flags, decoded by the driver (status_reg stays as its opaque word)
  const uint32_t sr = snap.motor.statusReg;
  const uint8_t fl = snap.motor.faults;
  int direction = snap.motor.forward ? 1 : 0;
  unsigned motStat = snap.motor.phase;
  bool cmdErr = (fl & Harness::DriverFault::COMMAND_ERROR) != 0u;
  bool uvlo = (fl & Harness::DriverFault::UNDERVOLTAGE) != 0u;
  bool thermalWarn = (fl & Harness::DriverFault::THERMAL_WARNING) != 0u;
  bool thermalSD = (fl & Harness::DriverFault::THERMAL_SHUTDOWN) != 0u;
  bool ocd = (fl & Harness::DriverFault::OVERCURRENT) != 0u;
  // stall_a / stall_b kept for protocol 2 clients: both report the driver's stall flag
  bool stallA = (fl & Harness::DriverFault::STALL) != 0u;
  bool stallB = stallA;

  // Filter faults against the fault-alarm config — only report faults the user
  // has enabled. STATUS register always reflects raw hardware state, but
  // disabled faults (e.g. OCD noise) should not cause GUI error state.
  auto fe = m_dispatcher.getFaultEnable();
  bool ocdEn = ocd && fe.ocd;
  bool thermalSDEn = thermalSD && fe.thermalSD;
  bool thermalWarnEn = thermalWarn && fe.thermalWarn;
  bool uvloEn = uvlo && fe.uvlo;
  bool stallAEn = stallA && fe.stallA && snap.motor.speed > 0;
  bool stallBEn = stallB && fe.stallB && snap.motor.speed > 0;
  bool cmdErrEn = cmdErr && fe.cmdErr;
  bool anyError = cmdErrEn || uvloEn || thermalSDEn || thermalWarnEn ||
                  stallAEn || stallBEn || ocdEn;

  // Get heartbeat watchdog status
  auto hb = m_dispatcher.safetyGetHeartbeatStatus();

  v.direction = direction; v.motStat = motStat; v.statusReg = sr;
  v.cmdErr = cmdErrEn; v.ocd = ocdEn; v.thermalSD = thermalSDEn; v.thermalWarn = thermalWarnEn;
  v.uvlo = uvloEn; v.stallA = stallAEn; v.stallB = stallBEn;
  v.hbEnabled = hb.enabled; v.hbTimeoutMs = hb.timeoutMs;
  v.hbRemainingMs = hb.remainingMs; v.hbTimedOut = hb.timedOut;
  v.anyError = anyError;
  v.uptimeTicks = snap.system.uptimeTicks; v.freeHeap = snap.system.freeHeap;
  v.rxOverflows = m_transport.rxOverflowCount();
  v.rxOverflowLines = m_dispatchStats.rxOverflowLines;
}

void CommandParser::cmdGetStatus() {
  TelemetrySnapshot snap;
  m_dispatcher.getTelemetry(snap);
  StatusJsonView v;
  fillStatusView(snap, v);

  if (m_format == ResponseFormat::JSON) {
    // F4: render through a pure function whose worst-case length is host-tested;
    // never send a truncated JSON line.
    char buf[STATUS_JSON_BUF];
    int n = renderStatusJson(buf, sizeof(buf), snap, v);
    if (n < 0 || n >= static_cast<int>(sizeof(buf))) {
      respondJsonErr("GET_STATUS", "ERROR", "status reply too long");
      return;
    }
    m_transport.println(buf);
  } else {
    // ASCII format: STATUS <state> <tick> <queue_depth> <mode> <encoder_status>
    // <position> <velocity>
    char buf[96];
    snprintf(buf, sizeof(buf), "STATUS %s %lu %u %s %s %ld %lu", v.stateStr,
             static_cast<unsigned long>(v.tick),
             static_cast<unsigned>(v.queueDepth), v.modeStr, v.encStatusStr,
             static_cast<long>(snap.motor.position),
             static_cast<unsigned long>(snap.motor.speed));
    m_transport.println(buf);
  }
}

// SYST:TLM:RATE <ms> / SYST:TLM:RATE? — JSON telemetry push period (F9). 0 = off.
void CommandParser::cmdTelemetryRate(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SYST:TLM:RATE <ms>");
    return;
  }
  auto requested = static_cast<uint32_t>(atol(cmd.args[0]));
  // Floor from the line rate: a push may not use more than 80 % of the link (status_json.hpp)
  const uint32_t floorMs = telemetryFloorMs();
  const uint32_t wanted = (requested > 0 && requested < floorMs) ? floorMs : requested;
  uint32_t accepted = m_dispatcher.setTelemetryPeriod(wanted);
  if (m_format == ResponseFormat::JSON) {
    char buf[112];
    snprintf(buf, sizeof(buf),
             "{\"requested\":%lu,\"accepted\":%lu,\"min_ms\":%lu,\"enabled\":%s}",
             static_cast<unsigned long>(requested),
             static_cast<unsigned long>(accepted),
             static_cast<unsigned long>(floorMs),
             (accepted > 0) ? "true" : "false");
    respondJsonOk("TLM_RATE", buf);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), "OK TLM_RATE %lu", static_cast<unsigned long>(accepted));
    m_transport.println(buf);
  }
}

uint32_t CommandParser::telemetryFloorMs() const {
  return telemetryPeriodFloorMs(m_transport.baudRate(),
                                m_telemetryCompact ? COMPACT_JSON_BUF : STATUS_JSON_BUF);
}

// SYST:TLM:FMT FULL|COMPACT - what a JSON telemetry push carries (ASCII keeps TELEM:)
void CommandParser::cmdTelemetryFormat(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SYST:TLM:FMT <FULL|COMPACT>");
    return;
  }
  const char *a = cmd.args[0];
  bool compact;
  if (strcmp(a, "COMPACT") == 0 || strcmp(a, "compact") == 0) {
    compact = true;
  } else if (strcmp(a, "FULL") == 0 || strcmp(a, "full") == 0) {
    compact = false;
  } else {
    respondErr("Telemetry format must be FULL or COMPACT");
    return;
  }
  m_telemetryCompact = compact;
  // Back to the full line: a period chosen for the compact floor would starve the encoder
  const uint32_t floorMs = telemetryFloorMs();
  uint32_t period = m_dispatcher.getTelemetryPeriod();
  if (period > 0 && period < floorMs) {
    period = m_dispatcher.setTelemetryPeriod(floorMs);
  }
  if (m_format == ResponseFormat::JSON) {
    char buf[96];
    snprintf(buf, sizeof(buf), "{\"format\":\"%s\",\"period_ms\":%lu,\"min_ms\":%lu}",
             compact ? "compact" : "full", static_cast<unsigned long>(period),
             static_cast<unsigned long>(floorMs));
    respondJsonOk("TLM_FMT", buf);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), "OK TLM_FMT %s %lu", compact ? "COMPACT" : "FULL",
             static_cast<unsigned long>(period));
    m_transport.println(buf);
  }
}

void CommandParser::cmdTelemetryFormatQuery() {
  if (m_format == ResponseFormat::JSON) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"format\":\"%s\",\"min_ms\":%lu}",
             m_telemetryCompact ? "compact" : "full",
             static_cast<unsigned long>(telemetryFloorMs()));
    respondJsonOk("TLM_FMT", buf);
  } else {
    m_transport.println(m_telemetryCompact ? "OK COMPACT" : "OK FULL");
  }
}

void CommandParser::cmdTelemetryRateQuery() {
  uint32_t period = m_dispatcher.getTelemetryPeriod();
  if (m_format == ResponseFormat::JSON) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"period_ms\":%lu,\"enabled\":%s}",
             static_cast<unsigned long>(period), (period > 0) ? "true" : "false");
    respondJsonOk("TLM_RATE", buf);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "OK %lu", static_cast<unsigned long>(period));
    m_transport.println(buf);
  }
}

// *IDN? — SCPI identification: <vendor>,<model>,<serial>,<fw>,<proto> (F17)
void CommandParser::cmdIdn() {
  char buf[128];   // the version carries the commit count and "+dirty" (docs/VERSIONING.md)
  if (m_format == ResponseFormat::JSON) {
    snprintf(buf, sizeof(buf),
             "{\"vendor\":\"%s\",\"model\":\"%s\",\"serial\":\"%s\",\"fw\":\"%s\",\"proto\":%u}",
             Harness::IDN_VENDOR, Harness::IDN_MODEL, Platform::deviceSerial(),
             Harness::fwVersion(), static_cast<unsigned>(Harness::PROTOCOL_VERSION));
    respondJsonOk("IDN", buf);
  } else {
    snprintf(buf, sizeof(buf), "%s,%s,%s,%s,%u", Harness::IDN_VENDOR, Harness::IDN_MODEL,
             Platform::deviceSerial(), Harness::fwVersion(),
             static_cast<unsigned>(Harness::PROTOCOL_VERSION));
    respondOk(buf);
  }
}

// SYST:HELP? — one line per command, generated from the dispatch tables by
// scripts/arch/gen_protocol.py (F8). The arch-check target fails when the table is stale.
namespace {
const char* const kHelpLines[] = {
#include "L2_protocol/help_table.inc"
};
} // namespace

void CommandParser::cmdHelp() {
  m_sink.begin(m_format == ResponseFormat::JSON, "HELP");
  for (size_t i = 0; i < sizeof(kHelpLines) / sizeof(kHelpLines[0]); i++)
    m_sink.writeLine(kHelpLines[i]);
  m_sink.end();
}

// DIAG:TASKS? — the LCD Task Monitor table over serial: one line per task,
// "<name> prio=<p> state=<s> stack_hwm_words=<n> cpu=<pct>" (F12 multi-line reply)
void CommandParser::cmdTasks() {
  Platform::ITaskStats* stats = Platform::taskStatsProvider();
  if (stats == nullptr) {
    respondErr("Task statistics unavailable");
    return;
  }
  Platform::TaskStatsSnapshot snap;
  stats->getSnapshot(snap);
  m_sink.begin(m_format == ResponseFormat::JSON, "TASKS");
  static const char* const kStates[] = {"Running", "Ready", "Blocked", "Suspended", "Deleted"};
  char line[96];
  for (uint8_t i = 0; i < snap.count && i < Platform::TaskStatsSnapshot::MAX_TASKS; ++i) {
    const Platform::TaskStat& t = snap.tasks[i];
    snprintf(line, sizeof(line), "%s prio=%u state=%s stack_hwm_words=%u cpu=%u",
             t.name, static_cast<unsigned>(t.priority),
             (t.state < 5) ? kStates[t.state] : "?",
             static_cast<unsigned>(t.stackHWM), static_cast<unsigned>(t.cpuPercent));
    m_sink.writeLine(line);
  }
  snprintf(line, sizeof(line), "free_heap_bytes=%lu",
           static_cast<unsigned long>(stats->getFreeHeapBytes()));
  m_sink.writeLine(line);
  m_sink.end();
}

// DIAG:MEM? — the LCD Memory page over serial, bytes unless noted:
//   flash used=<b> total=<b>          code + constants + .data image, of the FLASH region
//   ram static=<b> total=<b>          .data + .bss (the stack pool included), of the RAM region
//   stack_pool used=<b> total=<b>     the task stack pool: handed to tasks, of the whole pool
//   msp used=<b> total=<b>            main stack (boot + interrupts) peak, of its reserve
//   task <name> stack=<words> hwm=<words>   one per task: size and the least ever left free
void CommandParser::cmdMem() {
  Platform::IMemoryStats* mem = Platform::memoryStatsProvider();
  Platform::MemoryStats m = {};
  if (mem == nullptr || !mem->getMemoryStats(m)) {
    respondErr("Memory statistics unavailable");
    return;
  }
  m_sink.begin(m_format == ResponseFormat::JSON, "MEM");
  char line[64];
  snprintf(line, sizeof(line), "flash used=%lu total=%lu", static_cast<unsigned long>(m.flashUsed),
           static_cast<unsigned long>(m.flashTotal));
  m_sink.writeLine(line);
  snprintf(line, sizeof(line), "ram static=%lu total=%lu", static_cast<unsigned long>(m.ramStatic),
           static_cast<unsigned long>(m.ramTotal));
  m_sink.writeLine(line);
  snprintf(line, sizeof(line), "stack_pool used=%lu total=%lu", static_cast<unsigned long>(m.stackPoolUsed),
           static_cast<unsigned long>(m.stackPoolTotal));
  m_sink.writeLine(line);
  if (m.mainStackTotal != 0) {
    snprintf(line, sizeof(line), "msp used=%lu total=%lu", static_cast<unsigned long>(m.mainStackUsed),
             static_cast<unsigned long>(m.mainStackTotal));
    m_sink.writeLine(line);
  }
  Platform::ITaskStats* stats = Platform::taskStatsProvider();
  if (stats != nullptr) {
    Platform::TaskStatsSnapshot snap;
    stats->getSnapshot(snap);
    for (uint8_t i = 0; i < snap.count && i < Platform::TaskStatsSnapshot::MAX_TASKS; ++i) {
      const Platform::TaskStat& t = snap.tasks[i];
      snprintf(line, sizeof(line), "task %.15s stack=%u hwm=%u", t.name, static_cast<unsigned>(t.stackWords),
               static_cast<unsigned>(t.stackHWM));
      m_sink.writeLine(line);
    }
  }
  m_sink.end();
}

// DIAG:HARNESS? — the harness meter: one line per harness signal,
// "<harness>.<signal> calls=<n> bytes=<n> busy_us=<n> tasks=M<n>,E<n>,C<n>,D<n>,T<n>,?<n>"
// then "now_us=<t>". Counters only grow; take two readings and subtract for rates.
void CommandParser::cmdHarness() {
  m_sink.begin(m_format == ResponseFormat::JSON, "HARNESS");
  static const char kTask[Harness::Meter::TASK_SLOTS] = {'?', 'M', 'E', 'C', 'D', 'T'};
  char line[160];
  for (uint8_t n = 0; n < Harness::Meter::NET_COUNT; ++n) {
    Harness::Meter::Counters c;
    Harness::Meter::read(n, c);
    const Harness::Meter::NetInfo& info = Harness::Meter::info(n);
    int len = snprintf(line, sizeof(line), "%s.%s calls=%lu bytes=%lu busy_us=%lu tasks=", info.harness,
                       info.signal, static_cast<unsigned long>(c.calls), static_cast<unsigned long>(c.bytes),
                       static_cast<unsigned long>(c.busyUs));
    for (uint8_t t = 1; t <= Harness::Meter::TASK_SLOTS && len > 0 && len < static_cast<int>(sizeof(line)); ++t) {
      const uint8_t slot = static_cast<uint8_t>(t % Harness::Meter::TASK_SLOTS);   // M E C D T, then ?
      len += snprintf(line + len, sizeof(line) - static_cast<size_t>(len), "%s%c%lu", t > 1 ? "," : "",
                      kTask[slot], static_cast<unsigned long>(c.byTask[slot]));
    }
    m_sink.writeLine(line);
  }
  snprintf(line, sizeof(line), "now_us=%lu", static_cast<unsigned long>(Harness::Meter::nowUs()));
  m_sink.writeLine(line);
  m_sink.end();
}

void CommandParser::cmdVersion() {
  m_sink.begin(m_format == ResponseFormat::JSON, "VERSION");   // F12: 5 lines + terminator
  char line[80];
  snprintf(line, sizeof(line), "Stepper Motor Controller v%s", Harness::fwVersion());
  m_sink.writeLine(line);
  snprintf(line, sizeof(line), "Build: %s (" __DATE__ " " __TIME__ ")", Harness::buildId());
  m_sink.writeLine(line);
  snprintf(line, sizeof(line), "Protocol: %u (boot %08lX)",
           static_cast<unsigned>(Harness::PROTOCOL_VERSION),
           static_cast<unsigned long>(Platform::bootId()));
  m_sink.writeLine(line);

  // Show device identification
  char buf[48];
  uint16_t deviceId;
  uint8_t roleId;
  m_dispatcher.getDeviceInfo(deviceId, roleId);
  snprintf(buf, sizeof(buf), "Device: %u (%s)", static_cast<unsigned>(deviceId),
           roleIdToString(roleId));
  m_sink.writeLine(buf);

  // Show control mode
  snprintf(buf, sizeof(buf), "Mode: %s (encoder: %s)",
           controlModeToString(m_dispatcher.getControlMode()),
           encoderStatusToString(m_dispatcher.getEncoderStatus()));
  m_sink.writeLine(buf);  m_sink.end();
}

// SYST:ECHO ON|OFF / SYST:ECHO? — terminal echo of received characters (F7)
void CommandParser::cmdEcho(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SYST:ECHO <ON|OFF>");
    return;
  }
  if (strcmp(cmd.args[0], "ON") == 0 || strcmp(cmd.args[0], "on") == 0) {
    m_echo = true;
  } else if (strcmp(cmd.args[0], "OFF") == 0 || strcmp(cmd.args[0], "off") == 0) {
    m_echo = false;
  } else {
    respondErr("Usage: SYST:ECHO <ON|OFF>");
    return;
  }
  cmdEchoQuery();
}

void CommandParser::cmdEchoQuery() {
  if (m_format == ResponseFormat::JSON) {
    respondJsonOk("ECHO", m_echo ? "{\"echo\":true}" : "{\"echo\":false}");
  } else {
    respondOk(m_echo ? "ON" : "OFF");
  }
}

void CommandParser::cmdSetFormat(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: FMT <ASCII|JSON>");
    return;
  }

  const char *formatStr = cmd.args[0];

  if (strcmp(formatStr, "JSON") == 0 || strcmp(formatStr, "json") == 0) {
    m_format = ResponseFormat::JSON;
    m_echo = false;   // F7: machine mode, no terminal echo
    // Respond in the NEW format (JSON)
    respondJsonOk("SET_FORMAT", "{\"format\":\"JSON\"}");
  } else if (strcmp(formatStr, "ASCII") == 0 ||
             strcmp(formatStr, "ascii") == 0) {
    m_format = ResponseFormat::ASCII;
    m_echo = true;
    // Respond in the NEW format (ASCII)
    m_transport.println("OK ASCII");
  } else {
    respondErr("Invalid format. Use ASCII or JSON.");
  }
}

void CommandParser::cmdGetFormat() {
  if (m_format == ResponseFormat::JSON) {
    respondJsonOk("GET_FORMAT", "{\"format\":\"JSON\"}");
  } else {
    m_transport.println("OK ASCII");
  }
}

void CommandParser::cmdSetBaud(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SYST:BAUD <115200|230400|460800|921600>");
    return;
  }

  auto rate = static_cast<uint32_t>(strtoul(cmd.args[0], nullptr, 10));

  // Whitelist supported rates
  if (rate != 115200 && rate != 230400 && rate != 460800 && rate != 921600) {
    respondErr("Supported: 115200, 230400, 460800, 921600");
    return;
  }

  // Save current rate for auto-revert
  // (We don't have a getter, but 115200 is always the boot default.
  //  If already at a non-default rate, revert target is still 115200.)
  m_baudRevertRate = 115200;

  // Respond at the CURRENT baud rate so the client sees the OK
  if (m_format == ResponseFormat::JSON) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"baud\":%lu}", (unsigned long)rate);
    respondJsonOk("SET_BAUD", buf);
  } else {
    char buf[24];
    snprintf(buf, sizeof(buf), "OK %lu", (unsigned long)rate);
    m_transport.println(buf);
  }

  // Flush TX completely before switching
  m_transport.flush();

  // Switch baud rate on the UART hardware
  if (!m_transport.setBaudRate(rate)) {
    // Transport doesn't support baud change (e.g. RTT)
    m_baudRevertRate = 0;
    return;
  }

  // Set 2-second deadline for confirmation (any valid command cancels revert)
  m_baudRevertDeadline = m_dispatcher.getTickMs() + 2000;
}

void CommandParser::checkBaudRevert() {
  if (m_baudRevertRate == 0)
    return;

  uint32_t now = m_dispatcher.getTickMs();
  if ((int32_t)(now - m_baudRevertDeadline) >= 0) {
    // Timeout expired — revert to safe baud rate
    m_transport.flush();
    m_transport.setBaudRate(m_baudRevertRate);
    m_baudRevertRate = 0;
  }
}

void CommandParser::cmdGetDeviceId() {
  uint16_t deviceId;
  uint8_t roleId;
  m_dispatcher.getDeviceInfo(deviceId, roleId);

  char buf[32];
  snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(deviceId), roleIdToString(roleId));
  respondOk(buf);
}

void CommandParser::cmdSetDeviceId(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DEV:ID <id>");
    return;
  }

  auto deviceId = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));

  if (m_dispatcher.setDeviceId(deviceId)) {
    respondOk("ID saved");
  } else {
    respondErr("Flash write failed");
  }
}

// DEV:ROLE? — role only (F15; DEV:ID? keeps "<id> <role>")
void CommandParser::cmdGetRole() {
  uint16_t deviceId;
  uint8_t roleId;
  m_dispatcher.getDeviceInfo(deviceId, roleId);
  if (m_format == ResponseFormat::JSON) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"role\":\"%s\",\"role_id\":%u}", roleIdToString(roleId),
             static_cast<unsigned>(roleId));
    respondJsonOk("GET_ROLE", buf);
  } else {
    respondOk(roleIdToString(roleId));
  }
}

void CommandParser::cmdSetRole(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DEV:ROLE <FL|FR|RL|RR|NONE>");
    return;
  }

  uint8_t roleId = parseRole(cmd.args[0]);
  if (roleId == 0xFF) {
    respondErr("Unknown role (FL, FR, RL, RR, NONE)");
    return;
  }
  auto r = m_dispatcher.setRole(roleId);
  char buf[48];
  snprintf(buf, sizeof(buf), "Role=%s", cmd.args[0]);
  respondStatus(r, buf);
}

void CommandParser::cmdEventEnable(const ParsedCommand &cmd) {
  // Parse optional mask argument (default: all events)
  uint8_t mask = EVT_MASK_ALL;
  if (cmd.argCount >= 1) {
    char *endp = nullptr;
    long val = strtol(cmd.args[0], &endp, 0); // base 0: auto-detect dec/hex
    if (endp == cmd.args[0] || val < 0 || val > 0xFF) {
      respondErr("Invalid mask (0-255)");
      return;
    }
    mask = static_cast<uint8_t>(val) & EVT_MASK_ALL;
  }

  // Read current motor status for snapshot-on-enable
  Protocol::TelemetrySnapshot snap;
  m_dispatcher.getTelemetry(snap);
  m_dispatcher.enableEvents(mask, snap.motor.faults, static_cast<uint16_t>(snap.motor.statusReg & 0xFFFFu));

  if (m_format == ResponseFormat::JSON) {
    char dataBuf[32];
    snprintf(dataBuf, sizeof(dataBuf), "{\"mask\":%u}",
             static_cast<unsigned>(mask));
    respondJsonOk(m_currentCmd, dataBuf);
  } else {
    char msg[32];
    snprintf(msg, sizeof(msg), "mask=%u", static_cast<unsigned>(mask));
    respondOk(msg);
  }
}

void CommandParser::cmdEventDisable() {
  m_dispatcher.disableEvents();
  respondOk("");
}

void CommandParser::cmdEventStatus() {
  auto es = m_dispatcher.getEventStats();
  uint32_t lastSeq = m_dispatcher.getLastEventSeq();

  if (m_format == ResponseFormat::JSON) {
    char buf[192];
    snprintf(
        buf, sizeof(buf),
        "{\"mask\":%u,\"sent\":%lu,\"lost_critical\":%lu,\"lost_info\":%lu,"
        "\"last_seq\":%lu,\"depth\":%u}",
        static_cast<unsigned>(es.mask), static_cast<unsigned long>(es.sent),
        static_cast<unsigned long>(es.lostCritical),
        static_cast<unsigned long>(es.lostInfo),
        static_cast<unsigned long>(lastSeq), static_cast<unsigned>(es.queueDepth));
    respondJsonOk(m_currentCmd, buf);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "EVENT_STATUS mask=%u sent=%lu lost_critical=%lu lost_info=%lu "
             "last_seq=%lu depth=%u",
             static_cast<unsigned>(es.mask), static_cast<unsigned long>(es.sent),
             static_cast<unsigned long>(es.lostCritical),
             static_cast<unsigned long>(es.lostInfo),
             static_cast<unsigned long>(lastSeq),
             static_cast<unsigned>(es.queueDepth));
    m_transport.println(buf);
  }
}

void CommandParser::cmdSystDelay(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SYST:DELAY <ms>");
    return;
  }

  unsigned long ms = strtoul(cmd.args[0], nullptr, 10);
  if (ms > 1000) {
    respondErr("Delay must be 0-1000 ms");
    return;
  }

  m_dispatcher.setTransportDelay(static_cast<uint32_t>(ms));

  if (m_format == ResponseFormat::JSON) {
    char json[48];
    snprintf(json, sizeof(json), "{\"transport_delay_ms\":%lu}", ms);
    respondJsonOk("SYST:DELAY", json);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "DELAY=%lu", ms);
    respondOk(buf);
  }
}

void CommandParser::cmdSystDelayQuery() {
  uint32_t ms = m_dispatcher.getTransportDelay();

  if (m_format == ResponseFormat::JSON) {
    char json[48];
    snprintf(json, sizeof(json), "{\"transport_delay_ms\":%lu}",
             static_cast<unsigned long>(ms));
    respondJsonOk("SYST:DELAY?", json);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "DELAY=%lu", static_cast<unsigned long>(ms));
    respondOk(buf);
  }
}

void CommandParser::cmdTraceDump() {
  uint32_t count = m_dispatcher.traceGetCount();
  char extra[32];
  snprintf(extra, sizeof(extra), "\"entries\":%lu", static_cast<unsigned long>(count));
  m_sink.begin(m_format == ResponseFormat::JSON, "TRACE_DUMP", extra);   // F12
  char buf[80];
  if (m_format != ResponseFormat::JSON) {
    snprintf(buf, sizeof(buf), "TRACE: %u entries", static_cast<unsigned>(count));
    m_sink.writeLine(buf);
  }

  Protocol::ICommandDispatcher::TraceEntryData e;
  for (uint32_t i = 0; i < count; i++) {
    if (!m_dispatcher.traceGetEntry(i, e))
      break;
    snprintf(buf, sizeof(buf), "[%3u] T=%lu %c %s %lu",
             static_cast<unsigned>(i), static_cast<unsigned long>(e.tick),
             (e.dir == 0) ? '>' : '<', e.tag,
             static_cast<unsigned long>(e.arg0));
    m_sink.writeLine(buf);
  }
  m_sink.end();
}

void CommandParser::cmdTraceReset() {
  m_dispatcher.traceReset();
  respondOk("Trace cleared");
}

} // namespace Protocol
