/**
 * @file motion_handlers.cpp
 * @brief CommandParser motion command handlers (MOT namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Protocol {

void CommandParser::cmdMove(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr(
        "Usage: MOVE <steps> <dir> [speed] | MOVE <rev> REV <dir> [speed]");
    return;
  }

  // Check for REV unit: MOVE <rev> REV <dir> [speed]
  bool isRev = (cmd.argCount >= 2 && strcmp(cmd.args[1], "REV") == 0);
  int32_t steps;
  int dirArgIdx, speedArgIdx;

  if (isRev) {
    if (cmd.argCount < 3) {
      respondErr("Usage: MOVE <rev> REV <dir> [speed]");
      return;
    }
    double rev = atof(cmd.args[0]);
    uint32_t ustepsPerRev = m_dispatcher.getMicrostepsPerRev();
    steps = static_cast<int32_t>(rev * static_cast<double>(ustepsPerRev) + 0.5);
    dirArgIdx = 2;
    speedArgIdx = 3;
  } else {
    steps = static_cast<int32_t>(atol(cmd.args[0]));
    dirArgIdx = 1;
    speedArgIdx = 2;
  }

  auto dir = static_cast<int32_t>(atol(cmd.args[dirArgIdx]));

  // Validate direction (must be 0 or 1)
  if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
    respondErr("dir must be 0 or 1");
    return;
  }

  // Validate steps (within position range)
  if (steps < 0 || steps > Limits::POS_MAX) {
    respondErr("steps out of range (0-2097151)");
    return;
  }

  int32_t signedSteps = (dir == 0) ? -steps : steps;

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    auto spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionMove(signedSteps, speedOverride);
  respondStatus(r, "");
}

void CommandParser::cmdGoTo(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: GOTO <pos> [speed] | GOTO <rev> REV [speed]");
    return;
  }

  // Check for REV unit: GOTO <rev> REV [speed]
  bool isRev = (cmd.argCount >= 2 && strcmp(cmd.args[1], "REV") == 0);
  int32_t position;
  int speedArgIdx;

  if (isRev) {
    double rev = atof(cmd.args[0]);
    uint32_t ustepsPerRev = m_dispatcher.getMicrostepsPerRev();
    double usteps = rev * static_cast<double>(ustepsPerRev);
    position = (usteps >= 0) ? static_cast<int32_t>(usteps + 0.5)
                             : static_cast<int32_t>(usteps - 0.5);
    speedArgIdx = 2;
  } else {
    position = static_cast<int32_t>(atol(cmd.args[0]));
    speedArgIdx = 1;
  }

  // Validate position (22-bit signed range)
  if (position < Limits::POS_MIN || position > Limits::POS_MAX) {
    respondErr("position out of range (-2097152 to 2097151)");
    return;
  }

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    auto spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionGoTo(position, speedOverride);
  respondStatus(r, "");
}

void CommandParser::cmdRun(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: RUN <speed> <dir> | RUN <rpm> RPM <dir>");
    return;
  }

  // Check for RPM unit: RUN <rpm> RPM <dir>
  bool isRpm = (cmd.argCount >= 2 && strcmp(cmd.args[1], "RPM") == 0);
  int32_t speed;
  int dirArgIdx;

  if (isRpm) {
    if (cmd.argCount < 3) {
      respondErr("Usage: RUN <rpm> RPM <dir>");
      return;
    }
    double rpm = atof(cmd.args[0]);
    uint16_t fullSteps = m_dispatcher.getFullStepsPerRev();
    speed =
        static_cast<int32_t>(rpm * static_cast<double>(fullSteps) / 60.0 + 0.5);
    dirArgIdx = 2;
  } else {
    speed = static_cast<int32_t>(atol(cmd.args[0]));
    dirArgIdx = 1;
  }

  auto dir = static_cast<int32_t>(atol(cmd.args[dirArgIdx]));

  // Validate direction (must be 0 or 1)
  if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
    respondErr("dir must be 0 or 1");
    return;
  }

  // Validate speed (in steps/s)
  if (speed < Limits::SPEED_MIN || speed > Limits::SPEED_MAX) {
    respondErr("speed out of range (0-15625 steps/s)");
    return;
  }

  auto r = m_dispatcher.motionRun(static_cast<uint32_t>(speed), dir == 1);
  respondStatus(r, "");
}

void CommandParser::cmdStop(const ParsedCommand &cmd) {
  // Optional "hard" argument
  bool hard = (cmd.argCount > 0 && strcmp(cmd.args[0], "hard") == 0);

  auto r = m_dispatcher.motionStop(hard);
  respondStatus(r, "");
}

void CommandParser::cmdEstop() {
  m_dispatcher.safetyEstop();
  respondOk("ESTOP");
}

void CommandParser::cmdEnable() {
  auto r = m_dispatcher.motionEnable();
  respondStatus(r, "ENABLED");
}

void CommandParser::cmdDisable() {
  auto r = m_dispatcher.motionDisable();
  respondStatus(r, "HI-Z");
}

void CommandParser::cmdHome(const ParsedCommand &cmd) {
  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount >= 1) {
    auto spd = static_cast<int32_t>(atol(cmd.args[0]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionHome(speedOverride);
  respondStatus(r, "");
}

void CommandParser::cmdZero() {
  auto r = m_dispatcher.motionZero();
  respondStatus(r, "");
}

void CommandParser::cmdEncoderZero() {
  m_dispatcher.encoderResetCount();
  respondOk("ENCODER_ZEROED");
}

void CommandParser::cmdZeroAll() {
  auto r = m_dispatcher.motionZero();
  m_dispatcher.encoderResetCount();
  respondStatus(r, "ALL_ZEROED");
}

} // namespace Protocol
