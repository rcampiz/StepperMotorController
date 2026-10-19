/**
 * @file motion_handlers.cpp
 * @brief CommandParser motion command handlers (MOT namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Protocol {

void CommandParser::cmdMove(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
  if (cmd.argCount < 2) {
    respondErr(
        "Usage: MOT:MOVE <steps> <dir> [speed] | MOT:MOVE <rev> REV <dir> [speed]");
    return;
  }

  // Check for REV unit: MOVE <rev> REV <dir> [speed]
  bool isRev = (cmd.argCount >= 2 && strcmp(cmd.args[1], "REV") == 0);
  int32_t steps;
  int dirArgIdx, speedArgIdx;

  if (isRev) {
    if (cmd.argCount < 3) {
      respondErr("Usage: MOT:MOVE <rev> REV <dir> [speed]");
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
  if (steps < 0 || steps > lim.posMax) {
    respondRange("steps out of range (0-%ld)", lim.posMax);
    return;
  }

  int32_t signedSteps = (dir == 0) ? -steps : steps;

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    auto spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > lim.speedMax) {
      respondRange("speed out of range (1-%ld steps/s)", lim.speedMax);
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionMove(signedSteps, speedOverride);
  respondStatus(r, "");
}

void CommandParser::cmdGoTo(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:GOTO <pos> [speed] | MOT:GOTO <rev> REV [speed]");
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
  if (position < lim.posMin || position > lim.posMax) {
    respondRange("position out of range (%ld to %ld)", lim.posMin, lim.posMax);
    return;
  }

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    auto spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > lim.speedMax) {
      respondRange("speed out of range (1-%ld steps/s)", lim.speedMax);
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionGoTo(position, speedOverride);
  respondStatus(r, "");
}

void CommandParser::cmdRun(const ParsedCommand &cmd) {
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
  if (cmd.argCount < 2) {
    respondErr("Usage: MOT:RUN <speed> <dir> | MOT:RUN <rpm> RPM <dir>");
    return;
  }

  // Check for RPM unit: RUN <rpm> RPM <dir>
  bool isRpm = (cmd.argCount >= 2 && strcmp(cmd.args[1], "RPM") == 0);
  int32_t speed;
  int dirArgIdx;

  if (isRpm) {
    if (cmd.argCount < 3) {
      respondErr("Usage: MOT:RUN <rpm> RPM <dir>");
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
  if (speed < Limits::SPEED_MIN || speed > lim.speedMax) {
    respondRange("speed out of range (0-%ld steps/s)", lim.speedMax);
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
  // Self-timed: from the arrival of the line's last byte (USART ISR stamp) to the return
  // of the HardHiZ SPI write. This is the on-chip part of the ESTOP latency; the host
  // adds its own USB round trip on top (Phase 4 Step 0 baseline, no analyzer needed).
  const uint32_t rxUs = m_transport.lastRxTickUs();
  m_dispatcher.safetyEstop();
  const uint32_t hizUs = m_dispatcher.getTickUs();
  const unsigned long latency = (rxUs != 0) ? (hizUs - rxUs) : 0;
  // Out-of-band stop (0x18 ahead of this line): arrival -> STBY low, arrival -> SPI HardHiZ
  uint32_t oobPinUs = 0, oobHizUs = 0;
  const bool oob = m_dispatcher.safetyEstopTiming(oobPinUs, oobHizUs);
  char buf[112];
  if (m_format == ResponseFormat::JSON) {
    if (oob) {
      snprintf(buf, sizeof(buf),
               "{\"message\":\"ESTOP\",\"latency_us\":%lu,\"oob_pin_us\":%lu,\"oob_hiz_us\":%lu}",
               latency, static_cast<unsigned long>(oobPinUs), static_cast<unsigned long>(oobHizUs));
    } else {
      snprintf(buf, sizeof(buf), "{\"message\":\"ESTOP\",\"latency_us\":%lu}", latency);
    }
    respondJsonOk("SYST:ESTOP", buf);
  } else {
    if (oob) {
      snprintf(buf, sizeof(buf), "ESTOP latency_us=%lu oob_pin_us=%lu oob_hiz_us=%lu", latency,
               static_cast<unsigned long>(oobPinUs), static_cast<unsigned long>(oobHizUs));
    } else {
      snprintf(buf, sizeof(buf), "ESTOP latency_us=%lu", latency);
    }
    respondOk(buf);
  }
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
  const MotionLimits lim = motionLimits(m_dispatcher.driverCaps());
  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this
  // move
  uint32_t speedOverride = 0;
  if (cmd.argCount >= 1) {
    auto spd = static_cast<int32_t>(atol(cmd.args[0]));
    if (spd < 1 || spd > lim.speedMax) {
      respondRange("speed out of range (1-%ld steps/s)", lim.speedMax);
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
