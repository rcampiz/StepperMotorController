/**
 * @file queue_handlers.cpp
 * @brief CommandParser queue/sync command handlers (SYNC namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Protocol {

void CommandParser::cmdQueue(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: QUEUE <cmd> [args...]");
    return;
  }

  // Parse the queued command type, validate, and dispatch
  const char *subCmd = cmd.args[0];
  auto r = ServiceStatus::InvalidParam;

  if (strcmp(subCmd, "MOVE") == 0 || strcmp(subCmd, "move") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE MOVE <steps> <dir>");
      return;
    }
    auto steps = static_cast<int32_t>(atol(cmd.args[1]));
    auto dir = static_cast<int32_t>(atol(cmd.args[2]));
    if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
      respondErr("dir must be 0 or 1");
      return;
    }
    if (steps < 0 || steps > Limits::POS_MAX) {
      respondErr("steps out of range (0-2097151)");
      return;
    }
    r = m_dispatcher.queueMove((dir == 0) ? -steps : steps);
  } else if (strcmp(subCmd, "GOTO") == 0 || strcmp(subCmd, "goto") == 0) {
    if (cmd.argCount < 2) {
      respondErr("Usage: QUEUE GOTO <position>");
      return;
    }
    auto position = static_cast<int32_t>(atol(cmd.args[1]));
    if (position < Limits::POS_MIN || position > Limits::POS_MAX) {
      respondErr("position out of range (-2097152 to 2097151)");
      return;
    }
    r = m_dispatcher.queueGoTo(position);
  } else if (strcmp(subCmd, "RUN") == 0 || strcmp(subCmd, "run") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE RUN <speed> <dir>");
      return;
    }
    auto speed = static_cast<int32_t>(atol(cmd.args[1]));
    auto dir = static_cast<int32_t>(atol(cmd.args[2]));
    if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
      respondErr("dir must be 0 or 1");
      return;
    }
    if (speed < Limits::SPEED_MIN || speed > Limits::SPEED_MAX) {
      respondErr("speed out of range (0-15625 steps/s)");
      return;
    }
    // Convert steps/s to raw register value for powerSTEP01
    auto speedRaw = static_cast<uint32_t>(
        (static_cast<uint64_t>(speed) * 1048576ULL) / 15625ULL);
    r = m_dispatcher.queueRun(speedRaw, dir);
  } else if (strcmp(subCmd, "STOP") == 0 || strcmp(subCmd, "stop") == 0) {
    bool hard = (cmd.argCount > 1 && strcmp(cmd.args[1], "hard") == 0);
    r = m_dispatcher.queueStop(hard);
  } else if (strcmp(subCmd, "HOME") == 0 || strcmp(subCmd, "home") == 0) {
    r = m_dispatcher.queueHome();
  } else if (strcmp(subCmd, "ZERO") == 0 || strcmp(subCmd, "zero") == 0) {
    r = m_dispatcher.queueZero();
  } else {
    respondErr("Unknown queue command");
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "QUEUED %u",
           static_cast<unsigned>(m_dispatcher.getQueueDepth()));
  respondStatus(r, buf);
}

void CommandParser::cmdArm() {
  auto r = m_dispatcher.queueArm();
  respondStatus(r, "ARMED");
}

void CommandParser::cmdStart() {
  auto r = m_dispatcher.queueStart();
  respondStatus(r, "RUNNING");
}

void CommandParser::cmdStartAt(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: START_AT <tick>");
    return;
  }

  uint32_t targetTick =
      static_cast<uint32_t>(strtoul(cmd.args[0], nullptr, 10));
  auto r = m_dispatcher.queueStartAt(targetTick);
  respondStatus(r, "RUNNING");
}

void CommandParser::cmdClearQueue() {
  auto r = m_dispatcher.queueClear();
  respondStatus(r, "CLEARED");
}

} // namespace Protocol
