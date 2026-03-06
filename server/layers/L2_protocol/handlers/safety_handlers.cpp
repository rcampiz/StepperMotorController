/**
 * @file safety_handlers.cpp
 * @brief CommandParser safety command handlers (SYST:FAULT, SYST:HB namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Protocol {

void CommandParser::cmdClearFault() {
  char faultBuf[80] = {};
  auto r = m_dispatcher.safetyClearFault(faultBuf, sizeof(faultBuf));
  if (r == ServiceStatus::Ok) {
    respondOk("IDLE");
  } else if (r == ServiceStatus::FaultActive) {
    respondErr(faultBuf);
  } else {
    respondErr(statusToString(r));
  }
}

void CommandParser::cmdForceClearFault() {
  auto r = m_dispatcher.safetyForceClearFault();
  respondStatus(r, "IDLE");
}

void CommandParser::cmdHeartbeat(const ParsedCommand &cmd) {
  uint32_t seq = 0;
  if (cmd.argCount >= 1) {
    seq = static_cast<uint32_t>(atol(cmd.args[0]));
  }

  m_dispatcher.safetyHeartbeatReceived(seq);

  auto hb = m_dispatcher.safetyGetHeartbeatStatus();

  uint32_t mcu_tick = m_dispatcher.getTickUs();

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(
        buf, sizeof(buf), "{\"seq\":%lu,\"mcu_tick\":%lu,\"remaining_ms\":%lu}",
        static_cast<unsigned long>(seq), static_cast<unsigned long>(mcu_tick),
        static_cast<unsigned long>(hb.remainingMs));
    respondJsonOk("HEARTBEAT", buf);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "HEARTBEAT_ACK %lu %lu %lu",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(mcu_tick),
             static_cast<unsigned long>(hb.remainingMs));
    m_transport.println(buf);
  }
}

void CommandParser::cmdSetHeartbeat(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_HEARTBEAT <timeout_ms>");
    return;
  }

  auto requested = static_cast<uint32_t>(atol(cmd.args[0]));
  uint32_t accepted = m_dispatcher.safetySetHeartbeatTimeout(requested);

  if (m_format == ResponseFormat::JSON) {
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"requested\":%lu,\"accepted\":%lu,\"enabled\":%s}",
             static_cast<unsigned long>(requested),
             static_cast<unsigned long>(accepted),
             (accepted > 0) ? "true" : "false");
    respondJsonOk("SET_HEARTBEAT", buf);
  } else {
    char buf[48];
    snprintf(buf, sizeof(buf), "OK HEARTBEAT %lu",
             static_cast<unsigned long>(accepted));
    m_transport.println(buf);
  }
}

void CommandParser::cmdGetHeartbeatStatus() {
  auto hb = m_dispatcher.safetyGetHeartbeatStatus();

  if (m_format == ResponseFormat::JSON) {
    char buf[160];
    snprintf(
        buf, sizeof(buf),
        "{\"enabled\":%s,\"timeout_ms\":%lu,\"last_seq\":%lu,"
        "\"remaining_ms\":%lu,\"timed_out\":%s}",
        hb.enabled ? "true" : "false", static_cast<unsigned long>(hb.timeoutMs),
        static_cast<unsigned long>(hb.lastSeq),
        static_cast<unsigned long>(hb.remainingMs), hb.timedOut ? "true" : "false");
    respondJsonOk("GET_HEARTBEAT_STATUS", buf);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), "HEARTBEAT_STATUS %s %lu %lu %lu %s",
             hb.enabled ? "ENABLED" : "DISABLED",
             static_cast<unsigned long>(hb.timeoutMs),
             static_cast<unsigned long>(hb.lastSeq),
             static_cast<unsigned long>(hb.remainingMs),
             hb.timedOut ? "TIMED_OUT" : "OK");
    m_transport.println(buf);
  }
}

} // namespace Protocol
