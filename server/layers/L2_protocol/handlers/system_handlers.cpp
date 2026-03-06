/**
 * @file system_handlers.cpp
 * @brief CommandParser system command handlers (SYST, DIAG, DEV, FMT, DBG:TRACE)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "harness/pins/async_event.hpp"
#include "harness/pins/itelemetry.hpp"

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

void CommandParser::cmdGetStatus() {
  uint32_t tick = m_dispatcher.getTickUs();
  const char *stateStr = controllerStateToString(m_dispatcher.getControllerState());
  uint32_t queueDepth = m_dispatcher.getQueueDepth();
  const char *modeStr = controlModeToString(m_dispatcher.getControlMode());
  const char *encStatusStr = encoderStatusToString(m_dispatcher.getEncoderStatus());

  // Get actual position and velocity from motor telemetry
  TelemetrySnapshot snap = Harness::telemetry().getSnapshot();

  if (m_format == ResponseFormat::JSON) {
    // Parse status register for direction and error flags
    uint16_t sr = snap.motor.statusReg;
    int direction = (sr & (1 << 4)) ? 1 : 0; // bit 4: 1=FWD
    unsigned motStat = (sr >> 5) & 0x3;      // bits 5-6
    bool cmdErr = (sr & (1 << 7)) != 0;      // bit 7
    bool uvlo = !(sr & (1 << 9));            // bit 9 (active low)
    // Bits 11-12: TH_STATUS 2-bit field
    // 00=Normal, 01=Warning, 10=Bridge shutdown, 11=Device shutdown
    uint8_t thStatus = (sr >> 11) & 0x3;
    bool thermalWarn = (thStatus >= 1);
    bool thermalSD = (thStatus >= 2);
    bool ocd = !(sr & (1 << 13));    // bit 13 (active low)
    bool stallA = !(sr & (1 << 14)); // bit 14 (active low)
    bool stallB = !(sr & (1 << 15)); // bit 15 (active low)

    // Filter faults against ALARM_EN config — only report faults the user
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

    // Bypass respondJsonOk (256-byte buffer too small) — format full envelope
    // newlib-nano: no %lld — pre-format int64_t as string
    char encCountStr[24];
    i64toa(snap.encoder.count, encCountStr, sizeof(encCountStr));

    char buf[1024];
    snprintf(
        buf, sizeof(buf),
        "{\"status\":\"ok\",\"command\":\"GET_STATUS\",\"data\":"
        "{\"state\":\"%s\",\"tick\":%lu,\"queue_depth\":%u,"
        "\"motor\":{\"position\":%ld,\"speed\":%lu,\"busy\":%s,\"hi_z\":%s,"
        "\"direction\":%d,\"mot_status\":%u,\"status_reg\":\"%04X\","
        "\"cmd_err\":%s,\"ocd\":%s,\"thermal_sd\":%s,"
        "\"thermal_warn\":%s,\"uvlo\":%s,\"stall_a\":%s,\"stall_b\":%s},"
        "\"encoder\":{\"count\":%s,\"velocity\":%ld,\"index_seen\":%s,"
        "\"revolutions\":%ld,\"index_period_us\":%lu,\"vel_quality\":%u},"
        "\"control\":{\"following_error\":%ld,\"setpoint\":%ld,"
        "\"mode\":%u,\"tracking\":%s,"
        "\"trim_out\":%d,\"p\":%d,\"i\":%d,\"d\":%d,"
        "\"sup_state\":%u,\"tier\":%u,\"vel_error\":%ld,\"retries\":%u,"
        "\"base_spd\":%ld,\"trim_spd\":%ld,\"final_spd\":%ld,"
        "\"trim_frozen\":%u,\"vel_quality\":%u},"
        "\"heartbeat\":{\"enabled\":%s,\"timeout_ms\":%lu,"
        "\"remaining_ms\":%lu,\"timed_out\":%s},"
        "\"mode\":\"%s\",\"encoder_status\":\"%s\",\"error\":%s}}",
        stateStr, static_cast<unsigned long>(tick),
        static_cast<unsigned>(queueDepth),
        static_cast<long>(snap.motor.position),
        static_cast<unsigned long>(snap.motor.speed),
        snap.motor.busy ? "true" : "false", snap.motor.hiZ ? "true" : "false",
        direction, motStat, static_cast<unsigned>(sr),
        cmdErrEn ? "true" : "false", ocdEn ? "true" : "false",
        thermalSDEn ? "true" : "false", thermalWarnEn ? "true" : "false",
        uvloEn ? "true" : "false", stallAEn ? "true" : "false",
        stallBEn ? "true" : "false", encCountStr,
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
        static_cast<long>(snap.control.baseSpeedRaw),
        static_cast<long>(snap.control.trimSpeedRaw),
        static_cast<long>(snap.control.finalSpeedRaw),
        static_cast<unsigned>(snap.control.trimFrozen),
        static_cast<unsigned>(snap.control.velQuality),
        hb.enabled ? "true" : "false", static_cast<unsigned long>(hb.timeoutMs),
        static_cast<unsigned long>(hb.remainingMs), hb.timedOut ? "true" : "false",
        modeStr, encStatusStr, anyError ? "true" : "false");
    m_transport.println(buf);
  } else {
    // ASCII format: STATUS <state> <tick> <queue_depth> <mode> <encoder_status>
    // <position> <velocity>
    char buf[96];
    snprintf(buf, sizeof(buf), "STATUS %s %lu %u %s %s %ld %lu", stateStr,
             static_cast<unsigned long>(tick),
             static_cast<unsigned>(queueDepth), modeStr, encStatusStr,
             static_cast<long>(snap.motor.position),
             static_cast<unsigned long>(snap.motor.speed));
    m_transport.println(buf);
  }
}

void CommandParser::cmdVersion() {
  m_transport.println("Stepper Motor Controller v0.3.0");
  m_transport.println("Build: " __DATE__ " " __TIME__);
  m_transport.println("Protocol: ARM/START sync v1");

  // Show device identification
  char buf[48];
  uint16_t deviceId;
  uint8_t roleId;
  m_dispatcher.getDeviceInfo(deviceId, roleId);
  snprintf(buf, sizeof(buf), "Device: %u (%s)", static_cast<unsigned>(deviceId),
           roleIdToString(roleId));
  m_transport.println(buf);

  // Show control mode
  snprintf(buf, sizeof(buf), "Mode: %s (encoder: %s)",
           controlModeToString(m_dispatcher.getControlMode()),
           encoderStatusToString(m_dispatcher.getEncoderStatus()));
  m_transport.println(buf);
}

void CommandParser::cmdSetFormat(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_FORMAT <ASCII|JSON>");
    return;
  }

  const char *formatStr = cmd.args[0];

  if (strcmp(formatStr, "JSON") == 0 || strcmp(formatStr, "json") == 0) {
    m_format = ResponseFormat::JSON;
    // Respond in the NEW format (JSON)
    respondJsonOk("SET_FORMAT", "{\"format\":\"JSON\"}");
  } else if (strcmp(formatStr, "ASCII") == 0 ||
             strcmp(formatStr, "ascii") == 0) {
    m_format = ResponseFormat::ASCII;
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
    respondErr("Usage: SET_BAUD <115200|230400|460800|921600>");
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
    respondErr("Usage: SET_DEVICE_ID <id>");
    return;
  }

  auto deviceId = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));

  if (m_dispatcher.setDeviceId(deviceId)) {
    respondOk("ID saved");
  } else {
    respondErr("Flash write failed");
  }
}

void CommandParser::cmdSetRole(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_ROLE <FL|FR|RL|RR|NONE>");
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
  Protocol::TelemetrySnapshot snap = Harness::telemetry().getSnapshot();
  uint16_t currentStatus = snap.motor.statusReg;

  m_dispatcher.enableEvents(mask, currentStatus);

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
  char buf[80];
  snprintf(buf, sizeof(buf), "TRACE: %u entries", static_cast<unsigned>(count));
  m_transport.println(buf);

  Harness::ICommandDispatcher::TraceEntryData e;
  for (uint32_t i = 0; i < count; i++) {
    if (!m_dispatcher.traceGetEntry(i, e))
      break;
    snprintf(buf, sizeof(buf), "[%3u] T=%lu %c %s %lu",
             static_cast<unsigned>(i), static_cast<unsigned long>(e.tick),
             (e.dir == 0) ? '>' : '<', e.tag,
             static_cast<unsigned long>(e.arg0));
    m_transport.println(buf);
  }
}

void CommandParser::cmdTraceReset() {
  m_dispatcher.traceReset();
  respondOk("Trace cleared");
}

} // namespace Protocol
