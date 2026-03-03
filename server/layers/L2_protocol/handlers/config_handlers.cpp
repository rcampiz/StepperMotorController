/**
 * @file config_handlers.cpp
 * @brief CommandParser motor config command handlers (DRV:CFG namespace)
 */

#include "L2_protocol/command_parser_internal.hpp"

namespace Comms {

void CommandParser::cmdAccelPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:ACCEL <steps/s^2>");
    return;
  }

  auto value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("accel must be positive");
    return;
  }

  auto r = m_dispatcher.configSetAccelPhysical(static_cast<uint32_t>(value));
  respondStatus(r, "");
}

void CommandParser::cmdDecelPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:DECEL <steps/s^2>");
    return;
  }

  auto value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("decel must be positive");
    return;
  }

  auto r = m_dispatcher.configSetDecelPhysical(static_cast<uint32_t>(value));
  respondStatus(r, "");
}

void CommandParser::cmdMaxSpdPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:MAXSPD <steps/s>");
    return;
  }

  auto value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("maxspd must be positive");
    return;
  }

  auto r = m_dispatcher.configSetMaxSpeedPhysical(static_cast<uint32_t>(value));
  respondStatus(r, "");
}

void CommandParser::cmdMotorConfigShow() {
  auto cfg = m_dispatcher.getMotorConfig();
  char buf[384];
  snprintf(buf, sizeof(buf),
           "Motor Config %s\n"
           "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
           "OCD_TH=%02X STALL_TH=%02X\n"
           "ACC=%u DEC=%u MAXSPD=%u MINSPD=%u FS_SPD=%u\n"
           "Faults: OCD=%d TH_SD=%d TH_W=%d UVLO=%d STALL_A=%d STALL_B=%d CMD=%d\n"
           "Action: %s\n"
           "StepMode: %u (1/%u)",
           cfg.valid ? "(from flash)" : "(defaults)",
           (unsigned)cfg.kvalHold, (unsigned)cfg.kvalRun,
           (unsigned)cfg.kvalAcc, (unsigned)cfg.kvalDec,
           (unsigned)cfg.ocdThreshold, (unsigned)cfg.stallThreshold,
           (unsigned)cfg.acceleration, (unsigned)cfg.deceleration,
           (unsigned)cfg.maxSpeed, (unsigned)cfg.minSpeed, (unsigned)cfg.fsSpeed,
           cfg.faultOcd, cfg.faultThermalSD,
           cfg.faultThermalWarn, cfg.faultUvlo,
           cfg.faultStallA, cfg.faultStallB, cfg.faultCmdErr,
           cfg.faultAction == 0 ? "HardStop" :
           cfg.faultAction == 1 ? "HardHiZ" : "SoftStop",
           (unsigned)(cfg.stepMode & 0x07), 1U << (cfg.stepMode & 0x07));
  m_transport.println(buf);
}

void CommandParser::cmdMotorConfigSave() {
  if (m_dispatcher.configSaveToFlash()) {
    respondOk("Config saved to flash");
  } else {
    respondErr("Flash write failed");
  }
}

void CommandParser::cmdMotorConfigLoad() {
  if (m_dispatcher.configLoadFromFlash()) {
    respondOk("Config loaded from flash");
  } else {
    respondErr("No valid config in flash");
  }
}

void CommandParser::cmdMotorConfigReset() {
  if (m_dispatcher.configFactoryReset()) {
    respondOk("Factory defaults restored and saved");
  } else {
    respondErr("Flash write failed");
  }
}

void CommandParser::cmdMotorConfigKval(const ParsedCommand &cmd) {
  // Usage: MCONFIG_KVAL <hold> <run> <acc> <dec>
  if (cmd.argCount < 4) {
    respondErr("Usage: MCONFIG_KVAL <hold> <run> <acc> <dec> (hex 00-FF)");
    return;
  }

  auto hold = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 16));
  auto run = static_cast<uint8_t>(strtoul(cmd.args[1], nullptr, 16));
  auto acc = static_cast<uint8_t>(strtoul(cmd.args[2], nullptr, 16));
  auto dec = static_cast<uint8_t>(strtoul(cmd.args[3], nullptr, 16));

  m_dispatcher.configSetKval(hold, run, acc, dec);

  char buf[64];
  snprintf(buf, sizeof(buf),
           "KVAL set: H=%02X R=%02X A=%02X D=%02X (not saved)", hold, run, acc,
           dec);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigOcd(const ParsedCommand &cmd) {
  // Usage: MCONFIG_OCD <threshold>
  if (cmd.argCount < 1) {
    respondErr("Usage: MCONFIG_OCD <threshold> (0-31, ~375mA/step)");
    return;
  }

  auto thresh = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 10));
  if (thresh > 31) {
    respondErr("OCD threshold must be 0-31");
    return;
  }

  m_dispatcher.configSetOcdThreshold(thresh);

  char buf[48];
  snprintf(buf, sizeof(buf), "OCD threshold set: %u (not saved)", thresh);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigStall(const ParsedCommand &cmd) {
  // Usage: MCONFIG_STALL <threshold>
  // powerSTEP01 STALL_TH is 5-bit (0-31), NOT 7-bit like L6470
  if (cmd.argCount < 1) {
    respondErr("Usage: MCONFIG_STALL <threshold> (0-31, ~31.25mV/step BEMF)");
    return;
  }

  auto thresh = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 10));
  if (thresh > 31) {
    respondErr("Stall threshold must be 0-31");
    return;
  }

  m_dispatcher.configSetStallThreshold(thresh);

  char buf[48];
  snprintf(buf, sizeof(buf), "Stall threshold set: %u (not saved)", thresh);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigFault(const ParsedCommand &cmd) {
  // Usage: MCONFIG_FAULT <ocd> <th_sd> <th_w> <uvlo> <stall_a> <stall_b> <cmd>
  //        MCONFIG_FAULT ACTION <0|1|2>
  if (cmd.argCount < 1) {
    respondErr(
        "Usage: MCONFIG_FAULT <ocd> <th_sd> <th_w> <uvlo> <sta> <stb> <cmd>\n"
        "   or: MCONFIG_FAULT ACTION <0=HardStop|1=HardHiZ|2=SoftStop>");
    return;
  }

  // Check for ACTION subcommand
  if (strcmp(cmd.args[0], "ACTION") == 0 ||
      strcmp(cmd.args[0], "action") == 0) {
    if (cmd.argCount < 2) {
      respondErr("Usage: MCONFIG_FAULT ACTION <0|1|2>");
      return;
    }
    auto action = static_cast<uint8_t>(strtoul(cmd.args[1], nullptr, 10));
    if (action > 2) {
      respondErr("Action must be 0=HardStop, 1=HardHiZ, 2=SoftStop");
      return;
    }
    m_dispatcher.configSetFaultAction(action);
    const char *actionStr = action == 0   ? "HardStop"
                            : action == 1 ? "HardHiZ"
                                          : "SoftStop";
    char buf[48];
    snprintf(buf, sizeof(buf), "Fault action set: %s (not saved)", actionStr);
    respondOk(buf);
    return;
  }

  // Fault enable flags
  if (cmd.argCount < 7) {
    respondErr(
        "Need 7 flags: ocd th_sd th_w uvlo stall_a stall_b cmd_err (0|1)");
    return;
  }

  bool ocd = (strtoul(cmd.args[0], nullptr, 10) != 0);
  bool thermalSD = (strtoul(cmd.args[1], nullptr, 10) != 0);
  bool thermalWarn = (strtoul(cmd.args[2], nullptr, 10) != 0);
  bool uvlo = (strtoul(cmd.args[3], nullptr, 10) != 0);
  bool stallA = (strtoul(cmd.args[4], nullptr, 10) != 0);
  bool stallB = (strtoul(cmd.args[5], nullptr, 10) != 0);
  bool cmdErr = (strtoul(cmd.args[6], nullptr, 10) != 0);

  m_dispatcher.configSetFaultEnableFlags(ocd, thermalSD, thermalWarn, uvlo,
                                         stallA, stallB, cmdErr);
  respondOk("Fault enables set (not saved)");
}

void CommandParser::cmdMotorConfigMotion(const ParsedCommand &cmd) {
  // Usage: MCONFIG_MOTION <acc> <dec> <maxspd>
  if (cmd.argCount < 3) {
    respondErr("Usage: MCONFIG_MOTION <acc> <dec> <maxspd>");
    return;
  }

  auto acc = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  auto dec = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  auto maxSpd = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));

  // Validate ranges
  if (acc > 4095 || dec > 4095) {
    respondErr("ACC/DEC must be 0-4095");
    return;
  }
  if (maxSpd > 1023) {
    respondErr("MAXSPD must be 0-1023");
    return;
  }

  m_dispatcher.configSetMotionParams(acc, dec, maxSpd);

  char buf[64];
  snprintf(buf, sizeof(buf),
           "Motion params set: ACC=%u DEC=%u MAX=%u (not saved)", acc, dec,
           maxSpd);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigStepMode(const ParsedCommand &cmd) {
  // Usage: MCONFIG_STEPMODE <mode>
  // mode: 0=full, 1=half, 2=1/4, 3=1/8, 4=1/16, 5=1/32, 6=1/64, 7=1/128
  // Also accepts microstep counts: 1,2,4,8,16,32,64,128
  if (cmd.argCount < 1) {
    respondErr("Usage: MCONFIG_STEPMODE <mode> (0-7 or 1/2/4/8/16/32/64/128)");
    return;
  }

  unsigned long val = strtoul(cmd.args[0], nullptr, 10);
  uint8_t mode;

  // Values 0-7: raw register value (STEP_SEL field)
  // Values 8,16,32,64,128: microstep denominator
  if (val <= 7) {
    mode = static_cast<uint8_t>(val);
  } else if (val == 8) {
    mode = 3;
  } else if (val == 16) {
    mode = 4;
  } else if (val == 32) {
    mode = 5;
  } else if (val == 64) {
    mode = 6;
  } else if (val == 128) {
    mode = 7;
  } else {
    respondErr("Invalid mode. Use 0-7 or microstep count (8/16/32/64/128)");
    return;
  }

  m_dispatcher.configSetStepMode(mode);

  char buf[64];
  snprintf(buf, sizeof(buf), "Step mode set: %u (1/%u microstep, not saved)",
           (unsigned)mode, 1U << mode);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigApply() {
  // Apply current config to motor driver
  if (!m_dispatcher.motorApplyConfig()) {
    respondErr("Failed to apply config to motor");
    return;
  }

  // Persist to flash so config survives reboot
  if (!m_dispatcher.configSaveToFlash()) {
    respondErr("Applied but flash write failed");
    return;
  }

  // Readback chip registers to verify writes actually reached the powerSTEP01
  Comms::ICommandDispatcher::MotorDebugParams info;
  if (m_dispatcher.motorGetDebugInfo(info)) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Config applied. CHIP: OCD_TH=%02X STALL_TH=%02X ALARM_EN=%02X "
             "CONFIG=%04X FS_SPD=%03X",
             (unsigned)info.ocdTh, (unsigned)info.stallTh,
             (unsigned)info.alarmEn, (unsigned)info.config,
             (unsigned)info.fsSpd);
    respondOk(buf);
  } else {
    respondOk("Config applied and saved to flash");
  }
}

} // namespace Comms
