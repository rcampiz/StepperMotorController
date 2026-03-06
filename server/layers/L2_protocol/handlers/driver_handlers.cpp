/**
 * @file driver_handlers.cpp
 * @brief DRV: driver configuration command handlers (powerSTEP01 unit model)
 */

#include "L2_protocol/command_parser_internal.hpp"
#include "harness/pins/itelemetry.hpp"

namespace Protocol {

// DRV:STEP_MODE <0-7> — Hi-Z safe microstep mode change
void CommandParser::cmdDrvStepMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:STEP_MODE <0-7>");
    return;
  }

  unsigned long mode = strtoul(cmd.args[0], nullptr, 10);
  if (mode > 7) {
    respondErr("Step mode must be 0-7");
    return;
  }

  // Check motor is not busy
  Protocol::TelemetrySnapshot snap = Harness::telemetry().getSnapshot();
  if (snap.motor.busy) {
    if (m_format == ResponseFormat::JSON) {
      respondJsonErr("DRV:STEP_MODE", "MOTOR_BUSY",
                     "Motor must be idle to change microstep mode");
    } else {
      respondErr("MOTOR_BUSY: Motor must be idle to change microstep mode");
    }
    return;
  }

  // Suspend motor task, perform Hi-Z safe write, resume
  uint8_t readback = 0;
  bool ok =
      m_dispatcher.motorSetStepModeSafe(static_cast<uint8_t>(mode), readback);

  if (!ok) {
    if (m_format == ResponseFormat::JSON) {
      respondJsonErr("DRV:STEP_MODE", "VERIFY_FAILED",
                     "STEP_MODE readback mismatch");
    } else {
      respondErr("VERIFY_FAILED: STEP_MODE readback mismatch");
    }
    return;
  }

  // Update persistent config (RAM only — user must DRV:CFG:SAVE to persist)
  m_dispatcher.configSetStepMode(readback);
  uint32_t ustepsPerRev = m_dispatcher.getMicrostepsPerRev();

  if (m_format == ResponseFormat::JSON) {
    char json[128];
    snprintf(
        json, sizeof(json),
        "{\"step_mode\":%u,\"microstep_ratio\":%u,\"microsteps_per_rev\":%lu}",
        (unsigned)readback, 1U << readback,
        static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:STEP_MODE", json);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf),
             "STEP_MODE=%u MICROSTEP_RATIO=1/%u MICROSTEPS_PER_REV=%lu",
             (unsigned)readback, 1U << readback,
             static_cast<unsigned long>(ustepsPerRev));
    respondOk(buf);
  }
}

// DRV:STEP_MODE? — read step mode from hardware
void CommandParser::cmdDrvStepModeQuery() {
  Harness::MotorDebugParams info = {};
  m_dispatcher.motorGetDebugInfo(info);
  uint8_t mode = info.stepMode;
  uint32_t ustepsPerRev =
      static_cast<uint32_t>(m_dispatcher.getFullStepsPerRev()) * (1U << mode);

  if (m_format == ResponseFormat::JSON) {
    char json[128];
    snprintf(
        json, sizeof(json),
        "{\"step_mode\":%u,\"microstep_ratio\":%u,\"microsteps_per_rev\":%lu}",
        (unsigned)mode, 1U << mode, static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:STEP_MODE?", json);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf),
             "STEP_MODE=%u MICROSTEP_RATIO=1/%u MICROSTEPS_PER_REV=%lu",
             (unsigned)mode, 1U << mode,
             static_cast<unsigned long>(ustepsPerRev));
    respondOk(buf);
  }
}

// DRV:FULL_STEPS <n> — set motor full steps per revolution
void CommandParser::cmdDrvFullSteps(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:FULL_STEPS <steps_per_rev>");
    return;
  }

  unsigned long steps = strtoul(cmd.args[0], nullptr, 10);
  if (steps == 0 || steps > 10000) {
    respondErr("Full steps/rev must be 1-10000");
    return;
  }

  m_dispatcher.configSetFullStepsPerRev(static_cast<uint16_t>(steps));
  uint32_t ustepsPerRev = m_dispatcher.getMicrostepsPerRev();

  if (m_format == ResponseFormat::JSON) {
    char json[96];
    snprintf(json, sizeof(json),
             "{\"full_steps_per_rev\":%lu,\"microsteps_per_rev\":%lu}", steps,
             static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:FULL_STEPS", json);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "FULL_STEPS=%lu MICROSTEPS_PER_REV=%lu", steps,
             static_cast<unsigned long>(ustepsPerRev));
    respondOk(buf);
  }
}

// DRV:FULL_STEPS? — query motor full steps per revolution
void CommandParser::cmdDrvFullStepsQuery() {
  uint16_t steps = m_dispatcher.getFullStepsPerRev();
  uint32_t ustepsPerRev = m_dispatcher.getMicrostepsPerRev();

  if (m_format == ResponseFormat::JSON) {
    char json[96];
    snprintf(json, sizeof(json),
             "{\"full_steps_per_rev\":%u,\"microsteps_per_rev\":%lu}",
             (unsigned)steps, static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:FULL_STEPS?", json);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "FULL_STEPS=%u MICROSTEPS_PER_REV=%lu",
             (unsigned)steps, static_cast<unsigned long>(ustepsPerRev));
    respondOk(buf);
  }
}

// DRV:ENC_PPR <n> — set encoder pulses per revolution
void CommandParser::cmdDrvEncoderPPR(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DRV:ENC_PPR <pulses_per_rev>");
    return;
  }

  unsigned long ppr = strtoul(cmd.args[0], nullptr, 10);
  if (ppr == 0 || ppr > 65535) {
    respondErr("Encoder PPR must be 1-65535");
    return;
  }

  m_dispatcher.configSetEncoderPPR(static_cast<uint16_t>(ppr));

  if (m_format == ResponseFormat::JSON) {
    char json[48];
    snprintf(json, sizeof(json), "{\"encoder_ppr\":%lu}", ppr);
    respondJsonOk("DRV:ENC_PPR", json);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "ENC_PPR=%lu", ppr);
    respondOk(buf);
  }
}

// DRV:ENC_PPR? — query encoder pulses per revolution
void CommandParser::cmdDrvEncoderPPRQuery() {
  uint16_t ppr = m_dispatcher.configGetEncoderPPR();

  if (m_format == ResponseFormat::JSON) {
    char json[48];
    snprintf(json, sizeof(json), "{\"encoder_ppr\":%u}", (unsigned)ppr);
    respondJsonOk("DRV:ENC_PPR?", json);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "ENC_PPR=%u", (unsigned)ppr);
    respondOk(buf);
  }
}

} // namespace Protocol
