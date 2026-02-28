/**
 * @file command_parser.cpp
 * @brief ASCII command protocol parser implementation
 *
 * Supports synchronized multi-controller operation.
 * See docs/HOST_INTERFACE_AND_SYNC.md for protocol specification.
 */

#include "L2_protocol/command_parser.hpp"
#include "L2_protocol/telemetry.hpp"
#include "L2_protocol/async_event.hpp"
// L3 includes eliminated via ICommandDispatcher:
//   command_queue, config_service, control_mode, device_config,
//   motion_service, safety_service, tick_timer, timing_service,
//   event_service, motor_config, following_supervisor, sysid,
//   speed_trim_controller, unit_conversion
// Remaining L3 (debug commands only):
#include "L3_services/infra/trace.hpp"
#include "L3_services/infra/flash_image_service.hpp"
#include "F_util/crc32.hpp"
#include "L5_board/board_pins.hpp"
#include "F_platform/tasks/comms_task.hpp"
#include "F_platform/tasks/display_task.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "F_platform/tasks/motor_task.hpp"
#include "ui/ui_mode.hpp"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// newlib-nano doesn't support %lld — manual int64_t to string
static void i64toa(int64_t val, char* buf, size_t bufSize) {
    if (bufSize == 0) return;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[21];
    int i = 0;
    bool neg = (val < 0);
    uint64_t uval = neg ? static_cast<uint64_t>(-val) : static_cast<uint64_t>(val);
    while (uval > 0 && i < 20) {
        tmp[i++] = '0' + static_cast<char>(uval % 10);
        uval /= 10;
    }
    size_t j = 0;
    if (neg && j < bufSize - 1) buf[j++] = '-';
    while (i > 0 && j < bufSize - 1) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

namespace Comms {

// ============================================================================
// Argument validation limits (based on powerSTEP01 register sizes)
// ============================================================================
namespace Limits {
    // Position: 22-bit signed (-2^21 to 2^21-1)
    constexpr int32_t POS_MIN = -2097152;
    constexpr int32_t POS_MAX = 2097151;

    // Speed: in steps/s (converted to 20-bit raw register value internally)
    // Max 15625 steps/s per powerSTEP01 datasheet (raw 0xFFFFF * 2^-28 / 250ns)
    constexpr int32_t SPEED_MIN = 0;
    constexpr int32_t SPEED_MAX = 15625;  // steps per second

    // Acceleration/Deceleration: 12-bit register (0-4095)
    constexpr int32_t ACCEL_MIN = 1;
    constexpr int32_t ACCEL_MAX = 4095;

    // Max Speed: 10-bit register (0-1023)
    constexpr int32_t MAXSPD_MIN = 1;
    constexpr int32_t MAXSPD_MAX = 1023;

    // Direction: 0 or 1
    constexpr int32_t DIR_MIN = 0;
    constexpr int32_t DIR_MAX = 1;
}

CommandParser::CommandParser(ITransport &transport, ICommandDispatcher &dispatcher)
    : m_transport(transport), m_dispatcher(dispatcher),
      m_bufIndex(0), m_format(ResponseFormat::ASCII) {
  memset(m_buffer, 0, sizeof(m_buffer));
  memset(m_currentCmd, 0, sizeof(m_currentCmd));
}

void CommandParser::process() {
  // Read available bytes
  while (m_transport.available()) {
    uint8_t byte;
    if (!m_transport.readByte(byte, 0)) {
      break;
    }

    // Handle backspace
    if (byte == '\b' || byte == 127) {
      if (m_bufIndex > 0) {
        m_bufIndex--;
        m_transport.print("\b \b");
      }
      continue;
    }

    // Handle newline (command complete)
    if (byte == '\r' || byte == '\n') {
      if (m_bufIndex > 0) {
        m_transport.println("");
        m_buffer[m_bufIndex] = '\0';

        ParsedCommand cmd = parse(m_buffer);
        if (cmd.valid) {
          // Any valid command confirms the baud rate is working
          m_baudRevertRate = 0;
          dispatch(cmd);
        }

        m_bufIndex = 0;
      }
      continue;
    }

    // Handle printable characters
    if (byte >= 32 && byte < 127) {
      if (m_bufIndex < CMD_BUFFER_SIZE - 1) {
        m_buffer[m_bufIndex++] = static_cast<char>(byte);
        // Echo character
        char echo[2] = {static_cast<char>(byte), '\0'};
        m_transport.print(echo);
      }
    }
  }
}

ParsedCommand CommandParser::parse(const char *line) {
  ParsedCommand cmd = {};
  cmd.valid = false;
  cmd.argCount = 0;

  // Skip leading whitespace
  while (*line && isspace(*line)) {
    line++;
  }

  if (!*line) {
    return cmd;
  }

  // Extract command word
  size_t i = 0;
  while (*line && !isspace(*line) && i < sizeof(cmd.cmd) - 1) {
    cmd.cmd[i++] = static_cast<char>(toupper(*line++));
  }
  cmd.cmd[i] = '\0';

  // Extract arguments
  while (*line && cmd.argCount < MAX_ARGS) {
    // Skip whitespace
    while (*line && isspace(*line)) {
      line++;
    }
    if (!*line)
      break;

    // Extract argument
    i = 0;
    while (*line && !isspace(*line) && i < sizeof(cmd.args[0]) - 1) {
      cmd.args[cmd.argCount][i++] = *line++;
    }
    cmd.args[cmd.argCount][i] = '\0';
    cmd.argCount++;
  }

  cmd.valid = true;
  return cmd;
}

void CommandParser::dispatch(const ParsedCommand &cmd) {
  TRACE_ENTRY("CMD:RX");

  // Store current command for JSON echo
  strncpy(m_currentCmd, cmd.cmd, sizeof(m_currentCmd) - 1);
  m_currentCmd[sizeof(m_currentCmd) - 1] = '\0';

  // --- Two-level SCPI namespace dispatch ---
  // Find first ':' to split prefix from suffix
  const char *colon = strchr(cmd.cmd, ':');

  if (colon != nullptr) {
    // SCPI-style command with namespace prefix
    char prefix[8] = {};
    size_t prefixLen = static_cast<size_t>(colon - cmd.cmd);
    if (prefixLen >= sizeof(prefix)) prefixLen = sizeof(prefix) - 1;
    memcpy(prefix, cmd.cmd, prefixLen);
    prefix[prefixLen] = '\0';

    const char *suffix = colon + 1;  // everything after first ':'

    if (strcmp(prefix, "MOT") == 0) {
      dispatchMotion(suffix, cmd);
    } else if (strcmp(prefix, "SYST") == 0) {
      dispatchSystem(suffix, cmd);
    } else if (strcmp(prefix, "SYNC") == 0) {
      dispatchSync(suffix, cmd);
    } else if (strcmp(prefix, "DIAG") == 0) {
      dispatchDiag(suffix, cmd);
    } else if (strcmp(prefix, "CTRL") == 0) {
      dispatchCtrl(suffix, cmd);
    } else if (strcmp(prefix, "DEV") == 0) {
      dispatchDevice(suffix, cmd);
    } else if (strcmp(prefix, "FMT") == 0) {
      // FMT is a leaf — no suffix expected, but handle FMT:xxx just in case
      respondErr("Unknown FMT command");
    } else if (strcmp(prefix, "UI") == 0) {
      dispatchUI(suffix, cmd);
    } else if (strcmp(prefix, "DBG") == 0) {
      dispatchDebug(suffix, cmd);
    } else if (strcmp(prefix, "DRV") == 0) {
      dispatchDriver(suffix, cmd);
    } else {
      respondErr("Unknown namespace prefix");
    }
  } else {
    // No colon — could be *IDN?, FMT, FMT?, or a legacy flat command
    if (strcmp(cmd.cmd, "*IDN?") == 0) {
      cmdVersion();
    } else if (strcmp(cmd.cmd, "FMT") == 0) {
      cmdSetFormat(cmd);
    } else if (strcmp(cmd.cmd, "FMT?") == 0) {
      cmdGetFormat();
    } else {
      dispatchLegacy(cmd);
    }
  }

  TRACE_EXIT("CMD:RX");
}

// ============================================================================
// SCPI Namespace Dispatch Handlers
// ============================================================================

// --- MOT: Motion commands ---
void CommandParser::dispatchMotion(const char *suffix, const ParsedCommand &cmd) {
  // MOT:MOVE, MOT:GOTO, MOT:RUN, MOT:STOP, MOT:EN, MOT:DIS, MOT:HOME, MOT:ZERO
  if (strcmp(suffix, "MOVE") == 0) {
    cmdMove(cmd);
  } else if (strcmp(suffix, "GOTO") == 0) {
    cmdGoTo(cmd);
  } else if (strcmp(suffix, "RUN") == 0) {
    cmdRun(cmd);
  } else if (strcmp(suffix, "STOP") == 0) {
    cmdStop(cmd);
  } else if (strcmp(suffix, "EN") == 0) {
    cmdEnable();
  } else if (strcmp(suffix, "DIS") == 0) {
    cmdDisable();
  } else if (strcmp(suffix, "HOME") == 0) {
    cmdHome(cmd);
  } else if (strcmp(suffix, "ZERO") == 0) {
    cmdZero();
  }
  // MOT:CFG:ACCEL, MOT:CFG:DECEL, MOT:CFG:MAXSPD (physical units — steps/s^2, steps/s)
  else if (strcmp(suffix, "CFG:ACCEL") == 0) {
    cmdAccelPhysical(cmd);
  } else if (strcmp(suffix, "CFG:DECEL") == 0) {
    cmdDecelPhysical(cmd);
  } else if (strcmp(suffix, "CFG:MAXSPD") == 0) {
    cmdMaxSpdPhysical(cmd);
  } else {
    respondErr("Unknown MOT command");
  }
}

// --- SYST: System commands ---
void CommandParser::dispatchSystem(const char *suffix, const ParsedCommand &cmd) {
  // SYST:ESTOP
  if (strcmp(suffix, "ESTOP") == 0) {
    cmdEstop();
  }
  // SYST:FAULT:CLEAR, SYST:FAULT:STAT?
  else if (strncmp(suffix, "FAULT:", 6) == 0) {
    const char *faultSuffix = suffix + 6;
    if (strcmp(faultSuffix, "CLEAR") == 0) {
      cmdClearFault();
    } else if (strcmp(faultSuffix, "FORCE") == 0) {
      cmdForceClearFault();
    } else if (strcmp(faultSuffix, "STAT?") == 0) {
      // TODO: dedicated fault status query (currently use GET_STATUS)
      cmdGetStatus();
    } else {
      respondErr("Unknown SYST:FAULT command");
    }
  }
  // SYST:TICK?
  else if (strcmp(suffix, "TICK?") == 0) {
    cmdGetTick();
  }
  // SYST:HB <seq> (heartbeat)
  else if (strcmp(suffix, "HB") == 0) {
    cmdHeartbeat(cmd);
  }
  // SYST:HB:TIMEOUT <ms>
  else if (strcmp(suffix, "HB:TIMEOUT") == 0) {
    cmdSetHeartbeat(cmd);
  }
  // SYST:HB:STAT?
  else if (strcmp(suffix, "HB:STAT?") == 0) {
    cmdGetHeartbeatStatus();
  }
  // SYST:VER?
  else if (strcmp(suffix, "VER?") == 0) {
    cmdVersion();
  }
  // SYST:BAUD <rate>
  else if (strcmp(suffix, "BAUD") == 0) {
    cmdSetBaud(cmd);
  }
  // SYST:EVT:EN [mask]
  else if (strcmp(suffix, "EVT:EN") == 0) {
    cmdEventEnable(cmd);
  }
  // SYST:EVT:DIS
  else if (strcmp(suffix, "EVT:DIS") == 0) {
    cmdEventDisable();
  }
  // SYST:EVT:STAT?
  else if (strcmp(suffix, "EVT:STAT?") == 0) {
    cmdEventStatus();
  }
  // SYST:ZERO (combined motor + encoder zero)
  else if (strcmp(suffix, "ZERO") == 0) {
    cmdZeroAll();
  }
  // SYST:DELAY <ms> / SYST:DELAY?
  else if (strcmp(suffix, "DELAY") == 0) {
    cmdSystDelay(cmd);
  } else if (strcmp(suffix, "DELAY?") == 0) {
    cmdSystDelayQuery();
  } else {
    respondErr("Unknown SYST command");
  }
}

// --- SYNC: Synchronization commands ---
void CommandParser::dispatchSync(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "QUEUE") == 0) {
    cmdQueue(cmd);
  } else if (strcmp(suffix, "ARM") == 0) {
    cmdArm();
  } else if (strcmp(suffix, "START") == 0) {
    cmdStart();
  } else if (strcmp(suffix, "START:AT") == 0) {
    cmdStartAt(cmd);
  } else if (strcmp(suffix, "CLEAR") == 0) {
    cmdClearQueue();
  } else {
    respondErr("Unknown SYNC command");
  }
}

// --- DIAG: Diagnostics commands ---
void CommandParser::dispatchDiag(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "PING") == 0) {
    cmdPing(cmd);
  } else if (strcmp(suffix, "STAT?") == 0) {
    cmdGetStatus();
  } else {
    respondErr("Unknown DIAG command");
  }
}

// --- CTRL: Control mode commands ---
void CommandParser::dispatchCtrl(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "MODE?") == 0) {
    cmdGetMode();
  } else if (strcmp(suffix, "MODE") == 0) {
    cmdSetMode(cmd);
  } else if (strcmp(suffix, "ENC:STAT?") == 0) {
    cmdGetEncoderStatus();
  } else if (strcmp(suffix, "ENC?") == 0) {
    cmdEncoder();
  } else if (strcmp(suffix, "ENC:ZERO") == 0) {
    cmdEncoderZero();
  } else if (strcmp(suffix, "ENC:DBG?") == 0) {
    cmdEncDebug();
  } else if (strcmp(suffix, "ENC:FILT") == 0 || strcmp(suffix, "ENC:FILT?") == 0) {
    cmdEncFilter(cmd);
  } else if (strncmp(suffix, "ENC:FILT:", 9) == 0) {
    cmdEncFilterSub(suffix + 9, cmd);
  } else if (strcmp(suffix, "FOLLOW?") == 0) {
    cmdFollowingError();
  } else if (strcmp(suffix, "FOLLOW:THRESH?") == 0) {
    cmdFollowThreshQuery();
  } else if (strcmp(suffix, "FOLLOW:THRESH") == 0) {
    cmdFollowThreshSet(cmd);
  } else if (strcmp(suffix, "FOLLOW:SAVE") == 0) {
    cmdFollowSave();
  } else if (strcmp(suffix, "FOLLOW:CLEAR") == 0) {
    cmdFollowClear();
  } else if (strcmp(suffix, "TRIM?") == 0) {
    cmdTrimQuery();
  } else if (strcmp(suffix, "TRIM:GAINS") == 0) {
    cmdTrimSetGains(cmd);
  } else if (strcmp(suffix, "TRIM:LIMITS") == 0) {
    cmdTrimSetLimits(cmd);
  } else if (strcmp(suffix, "TRIM:MAXPCT") == 0) {
    cmdTrimSetMaxPct(cmd);
  } else if (strcmp(suffix, "TRIM:RESET") == 0) {
    cmdTrimReset();
  } else if (strcmp(suffix, "TRIM:SAVE") == 0) {
    cmdTrimSave();
  // Legacy PID aliases → route to trim
  } else if (strcmp(suffix, "PID?") == 0) {
    cmdPidQuery();
  } else if (strcmp(suffix, "PID:GAINS") == 0) {
    cmdPidSetGains(cmd);
  } else if (strcmp(suffix, "PID:LIMITS") == 0) {
    cmdPidSetLimits(cmd);
  } else if (strcmp(suffix, "PID:RESET") == 0) {
    cmdPidReset();
  } else if (strcmp(suffix, "PID:SAVE") == 0) {
    cmdPidSave();
  } else if (strcmp(suffix, "SYSID:STEP") == 0) {
    cmdSysIdStep(cmd);
  } else if (strcmp(suffix, "SYSID:RAMP") == 0) {
    cmdSysIdRamp(cmd);
  } else if (strcmp(suffix, "SYSID:SINE") == 0) {
    cmdSysIdSine(cmd);
  } else if (strcmp(suffix, "SYSID:TRAPEZOID") == 0) {
    cmdSysIdTrapezoid(cmd);
  } else if (strcmp(suffix, "SYSID:RECT") == 0) {
    cmdSysIdRect(cmd);
  } else if (strcmp(suffix, "SYSID:STATUS?") == 0) {
    cmdSysIdStatus();
  } else if (strcmp(suffix, "SYSID:DATA?") == 0) {
    cmdSysIdData(cmd);
  } else if (strcmp(suffix, "SYSID:ABORT") == 0) {
    cmdSysIdAbort();
  } else {
    respondErr("Unknown CTRL command");
  }
}

// --- DEV: Device identity commands ---
void CommandParser::dispatchDevice(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "ID?") == 0) {
    cmdGetDeviceId();
  } else if (strcmp(suffix, "ID") == 0) {
    cmdSetDeviceId(cmd);
  } else if (strcmp(suffix, "ROLE") == 0) {
    cmdSetRole(cmd);
  } else if (strcmp(suffix, "ROLE?") == 0) {
    cmdGetDeviceId();  // returns ID + role
  } else {
    respondErr("Unknown DEV command");
  }
}

// --- UI: Display / UI commands ---
void CommandParser::dispatchUI(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "MODE") == 0) {
    cmdUIMode(cmd);
  } else if (strcmp(suffix, "MODE?") == 0) {
    cmdUIGetMode();
  }
  // UI:DISP:* display rendering commands
  else if (strncmp(suffix, "DISP:", 5) == 0) {
    const char *dispSuffix = suffix + 5;
    if (strcmp(dispSuffix, "CLEAR") == 0) {
      cmdDispClear(cmd);
    } else if (strcmp(dispSuffix, "TEXT") == 0) {
      cmdDispText(cmd);
    } else if (strcmp(dispSuffix, "RECT") == 0) {
      cmdDispRect(cmd);
    } else if (strcmp(dispSuffix, "LINE") == 0) {
      cmdDispLine(cmd);
    } else if (strcmp(dispSuffix, "BITMAP") == 0) {
      cmdDispBitmap(cmd);
    } else if (strcmp(dispSuffix, "BITMAP:B64") == 0) {
      cmdDispBitmapB64(cmd);
    } else {
      respondErr("Unknown UI:DISP command");
    }
  }
  // UI:FLASH:* flash image storage commands
  else if (strncmp(suffix, "FLASH:", 6) == 0) {
    const char *flashSuffix = suffix + 6;
    if (strcmp(flashSuffix, "INFO") == 0) {
      cmdFlashInfo();
    } else if (strcmp(flashSuffix, "UPLOAD") == 0) {
      cmdFlashUpload(cmd);
    } else if (strcmp(flashSuffix, "SHOW") == 0) {
      cmdFlashShow(cmd);
    } else if (strcmp(flashSuffix, "ERASE_ALL") == 0) {
      cmdFlashEraseAll();
    } else {
      respondErr("Unknown UI:FLASH command");
    }
  } else {
    respondErr("Unknown UI command");
  }
}

// --- DBG: Debug commands (bringup diagnostics — not stable contract) ---
void CommandParser::dispatchDebug(const char *suffix, const ParsedCommand &cmd) {
  (void)cmd;
  if (strcmp(suffix, "MOTOR") == 0) {
    cmdMotorDebug();
  } else if (strcmp(suffix, "TRACE:DUMP") == 0) {
    cmdTraceDump();
  } else if (strcmp(suffix, "TRACE:RESET") == 0) {
    cmdTraceReset();
  } else {
    respondErr("Unknown DBG command");
  }
}

// --- DRV: Driver configuration commands (powerSTEP01 registers) ---
void CommandParser::dispatchDriver(const char *suffix, const ParsedCommand &cmd) {
  if (strcmp(suffix, "CFG?") == 0) {
    cmdMotorConfigShow();
  } else if (strcmp(suffix, "CFG:SAVE") == 0) {
    cmdMotorConfigSave();
  } else if (strcmp(suffix, "CFG:LOAD") == 0) {
    cmdMotorConfigLoad();
  } else if (strcmp(suffix, "CFG:RESET") == 0) {
    cmdMotorConfigReset();
  } else if (strcmp(suffix, "CFG:KVAL") == 0) {
    cmdMotorConfigKval(cmd);
  } else if (strcmp(suffix, "CFG:OCD") == 0) {
    cmdMotorConfigOcd(cmd);
  } else if (strcmp(suffix, "CFG:STALL") == 0) {
    cmdMotorConfigStall(cmd);
  } else if (strcmp(suffix, "CFG:FAULT") == 0) {
    cmdMotorConfigFault(cmd);
  } else if (strcmp(suffix, "CFG:MOTION") == 0) {
    cmdMotorConfigMotion(cmd);
  } else if (strcmp(suffix, "CFG:STEPMODE") == 0) {
    cmdMotorConfigStepMode(cmd);
  } else if (strcmp(suffix, "CFG:APPLY") == 0) {
    cmdMotorConfigApply();
  }
  // DRV:STEP_MODE <0-7> / DRV:STEP_MODE? (Hi-Z safe)
  else if (strcmp(suffix, "STEP_MODE") == 0) {
    cmdDrvStepMode(cmd);
  } else if (strcmp(suffix, "STEP_MODE?") == 0) {
    cmdDrvStepModeQuery();
  }
  // DRV:FULL_STEPS <n> / DRV:FULL_STEPS?
  else if (strcmp(suffix, "FULL_STEPS") == 0) {
    cmdDrvFullSteps(cmd);
  } else if (strcmp(suffix, "FULL_STEPS?") == 0) {
    cmdDrvFullStepsQuery();
  }
  // DRV:ENC_PPR <n> / DRV:ENC_PPR?
  else if (strcmp(suffix, "ENC_PPR") == 0) {
    cmdDrvEncoderPPR(cmd);
  } else if (strcmp(suffix, "ENC_PPR?") == 0) {
    cmdDrvEncoderPPRQuery();
  } else {
    respondErr("Unknown DRV command");
  }
}

// ============================================================================
// Legacy flat-command dispatch (backward compatibility)
// ============================================================================
void CommandParser::dispatchLegacy(const ParsedCommand &cmd) {
  // Response format commands
  if (strcmp(cmd.cmd, "SET_FORMAT") == 0) {
    cmdSetFormat(cmd);
  } else if (strcmp(cmd.cmd, "GET_FORMAT") == 0) {
    cmdGetFormat();
  } else if (strcmp(cmd.cmd, "SET_BAUD") == 0) {
    cmdSetBaud(cmd);
  }
  // Motion commands
  else if (strcmp(cmd.cmd, "MOVE") == 0) {
    cmdMove(cmd);
  } else if (strcmp(cmd.cmd, "GOTO") == 0) {
    cmdGoTo(cmd);
  } else if (strcmp(cmd.cmd, "RUN") == 0) {
    cmdRun(cmd);
  } else if (strcmp(cmd.cmd, "STOP") == 0) {
    cmdStop(cmd);
  } else if (strcmp(cmd.cmd, "ESTOP") == 0) {
    cmdEstop();
  }
  // Configuration commands
  else if (strcmp(cmd.cmd, "ENABLE") == 0) {
    cmdEnable();
  } else if (strcmp(cmd.cmd, "DISABLE") == 0) {
    cmdDisable();
  } else if (strcmp(cmd.cmd, "ACCEL") == 0) {
    cmdAccel(cmd);
  } else if (strcmp(cmd.cmd, "DECEL") == 0) {
    cmdDecel(cmd);
  } else if (strcmp(cmd.cmd, "MAXSPD") == 0) {
    cmdMaxSpd(cmd);
  }
  // Synchronization commands
  else if (strcmp(cmd.cmd, "QUEUE") == 0) {
    cmdQueue(cmd);
  } else if (strcmp(cmd.cmd, "ARM") == 0) {
    cmdArm();
  } else if (strcmp(cmd.cmd, "START") == 0) {
    cmdStart();
  } else if (strcmp(cmd.cmd, "START_AT") == 0) {
    cmdStartAt(cmd);
  } else if (strcmp(cmd.cmd, "CLEAR_QUEUE") == 0) {
    cmdClearQueue();
  }
  // Motor driver reinit (power-cycle recovery)
  else if (strcmp(cmd.cmd, "MOTOR_REINIT") == 0) {
    Tasks::MotorTask_Reinit();
    respondOk("Motor driver reinitialized and config applied");
  }
  // Timing/diagnostics commands
  else if (strcmp(cmd.cmd, "PING") == 0) {
    cmdPing(cmd);
  } else if (strcmp(cmd.cmd, "GET_TICK") == 0) {
    cmdGetTick();
  } else if (strcmp(cmd.cmd, "GET_STATUS") == 0) {
    cmdGetStatus();
  } else if (strcmp(cmd.cmd, "CLEAR_FAULT") == 0) {
    cmdClearFault();
  } else if (strcmp(cmd.cmd, "FORCE_CLEAR_FAULT") == 0) {
    cmdForceClearFault();
  }
  // Heartbeat commands
  else if (strcmp(cmd.cmd, "HEARTBEAT") == 0) {
    cmdHeartbeat(cmd);
  } else if (strcmp(cmd.cmd, "SET_HEARTBEAT") == 0) {
    cmdSetHeartbeat(cmd);
  } else if (strcmp(cmd.cmd, "GET_HEARTBEAT_STATUS") == 0) {
    cmdGetHeartbeatStatus();
  }
  // Event commands (legacy aliases)
  else if (strcmp(cmd.cmd, "EVENT_ENABLE") == 0) {
    cmdEventEnable(cmd);
  } else if (strcmp(cmd.cmd, "EVENT_DISABLE") == 0) {
    cmdEventDisable();
  } else if (strcmp(cmd.cmd, "EVENT_STATUS") == 0) {
    cmdEventStatus();
  }
  // Utility commands
  else if (strcmp(cmd.cmd, "HELP") == 0 || strcmp(cmd.cmd, "?") == 0) {
    cmdHelp();
  } else if (strcmp(cmd.cmd, "VER") == 0 || strcmp(cmd.cmd, "VERSION") == 0) {
    cmdVersion();
  } else if (strcmp(cmd.cmd, "HOME") == 0) {
    cmdHome(cmd);
  } else if (strcmp(cmd.cmd, "ZERO") == 0) {
    cmdZero();
  } else if (strcmp(cmd.cmd, "ENC_ZERO") == 0) {
    cmdEncoderZero();
  } else if (strcmp(cmd.cmd, "ZERO_ALL") == 0) {
    cmdZeroAll();
  } else if (strcmp(cmd.cmd, "ENC_FILTER") == 0) {
    cmdEncFilter(cmd);
  } else if (strcmp(cmd.cmd, "ENCODER") == 0 || strcmp(cmd.cmd, "ENC") == 0) {
    cmdEncoder();
  } else if (strcmp(cmd.cmd, "ENC_DEBUG") == 0) {
    cmdEncDebug();
  }
  // Device identification commands
  else if (strcmp(cmd.cmd, "GET_DEVICE_ID") == 0) {
    cmdGetDeviceId();
  } else if (strcmp(cmd.cmd, "SET_DEVICE_ID") == 0) {
    cmdSetDeviceId(cmd);
  } else if (strcmp(cmd.cmd, "SET_ROLE") == 0) {
    cmdSetRole(cmd);
  }
  // Control mode commands
  else if (strcmp(cmd.cmd, "GET_MODE") == 0) {
    cmdGetMode();
  } else if (strcmp(cmd.cmd, "SET_MODE") == 0) {
    cmdSetMode(cmd);
  } else if (strcmp(cmd.cmd, "GET_ENCODER_STATUS") == 0) {
    cmdGetEncoderStatus();
  }
  // UI mode commands
  else if (strcmp(cmd.cmd, "UI_MODE") == 0) {
    cmdUIMode(cmd);
  } else if (strcmp(cmd.cmd, "UI_GET_MODE") == 0) {
    cmdUIGetMode();
  }
  // Display commands (remote rendering)
  else if (strcmp(cmd.cmd, "DISP_CLEAR") == 0) {
    cmdDispClear(cmd);
  } else if (strcmp(cmd.cmd, "DISP_TEXT") == 0) {
    cmdDispText(cmd);
  } else if (strcmp(cmd.cmd, "DISP_RECT") == 0) {
    cmdDispRect(cmd);
  } else if (strcmp(cmd.cmd, "DISP_LINE") == 0) {
    cmdDispLine(cmd);
  } else if (strcmp(cmd.cmd, "DISP_BITMAP") == 0) {
    cmdDispBitmap(cmd);
  } else if (strcmp(cmd.cmd, "DISP_BITMAP_B64") == 0) {
    cmdDispBitmapB64(cmd);
  } else if (strcmp(cmd.cmd, "DISP_INDICATOR") == 0) {
    cmdDispIndicator(cmd);
  } else if (strcmp(cmd.cmd, "DISP_BITMAP_RLE") == 0) {
    cmdDispBitmapRle(cmd);
  } else if (strcmp(cmd.cmd, "FLASH_INFO") == 0) {
    cmdFlashInfo();
  } else if (strcmp(cmd.cmd, "FLASH_UPLOAD") == 0) {
    cmdFlashUpload(cmd);
  } else if (strcmp(cmd.cmd, "FLASH_UPLOAD_RLE") == 0) {
    cmdFlashUploadRle(cmd);
  } else if (strcmp(cmd.cmd, "FLASH_SHOW") == 0) {
    cmdFlashShow(cmd);
  } else if (strcmp(cmd.cmd, "FLASH_ERASE_ALL") == 0) {
    cmdFlashEraseAll();
  } else if (strcmp(cmd.cmd, "FLASH_DUMP") == 0) {
    cmdFlashDump(cmd);
  } else if (strcmp(cmd.cmd, "FLASH_TEST") == 0) {
    cmdFlashTest();
  } else if (strcmp(cmd.cmd, "SPI_DEBUG") == 0) {
    // Debug: show SPI1 and GPIO states
    char buf[128];
    snprintf(buf, sizeof(buf),
             "SPI1_CR1=%08X SR=%04X MODER=%08X AFR0=%08X PC_ODR=%04X",
             (unsigned)SPI1->CR1, (unsigned)SPI1->SR,
             (unsigned)GPIOA->MODER, (unsigned)GPIOA->AFR[0],
             (unsigned)GPIOC->ODR);
    m_transport.println(buf);
  } else if (strcmp(cmd.cmd, "MOTOR_DEBUG") == 0) {
    cmdMotorDebug();

  // ===== Motor Configuration Commands =====
  } else if (strcmp(cmd.cmd, "MCONFIG") == 0) {
    cmdMotorConfigShow();
  } else if (strcmp(cmd.cmd, "MCONFIG_SAVE") == 0) {
    cmdMotorConfigSave();
  } else if (strcmp(cmd.cmd, "MCONFIG_LOAD") == 0) {
    cmdMotorConfigLoad();
  } else if (strcmp(cmd.cmd, "MCONFIG_RESET") == 0) {
    cmdMotorConfigReset();
  } else if (strcmp(cmd.cmd, "MCONFIG_KVAL") == 0) {
    cmdMotorConfigKval(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_OCD") == 0) {
    cmdMotorConfigOcd(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_STALL") == 0) {
    cmdMotorConfigStall(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_FAULT") == 0) {
    cmdMotorConfigFault(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_MOTION") == 0) {
    cmdMotorConfigMotion(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_STEPMODE") == 0) {
    cmdMotorConfigStepMode(cmd);
  } else if (strcmp(cmd.cmd, "MCONFIG_APPLY") == 0) {
    cmdMotorConfigApply();

  } else if (strcmp(cmd.cmd, "SPI_MODE_TEST") == 0) {
    // Test all 4 SPI modes to find which one works
    char buf[256];
    m_transport.println("Testing SPI modes on motor CS (PB6/D10)...");

    for (int mode = 0; mode < 4; mode++) {
      // Disable SPI
      SPI1->CR1 &= ~SPI_CR1_SPE;

      // Clear and set CPOL/CPHA
      SPI1->CR1 &= ~(SPI_CR1_CPOL | SPI_CR1_CPHA);
      if (mode & 0x02) SPI1->CR1 |= SPI_CR1_CPOL;  // Mode 2,3
      if (mode & 0x01) SPI1->CR1 |= SPI_CR1_CPHA;  // Mode 1,3

      // Re-enable SPI
      SPI1->CR1 |= SPI_CR1_SPE;

      // Pull CS low (PB6)
      GPIOB->BSRR = (1UL << (6 + 16));  // PB6 low

      // Small delay
      for (volatile int i = 0; i < 100; i++);

      // Send GetStatus command (0xD0) and read 2 bytes
      while (!(SPI1->SR & SPI_SR_TXE));
      *(volatile uint8_t*)&SPI1->DR = 0xD0;
      while (!(SPI1->SR & SPI_SR_RXNE));
      uint8_t dummy = *(volatile uint8_t*)&SPI1->DR;

      while (!(SPI1->SR & SPI_SR_TXE));
      *(volatile uint8_t*)&SPI1->DR = 0x00;
      while (!(SPI1->SR & SPI_SR_RXNE));
      uint8_t hi = *(volatile uint8_t*)&SPI1->DR;

      while (!(SPI1->SR & SPI_SR_TXE));
      *(volatile uint8_t*)&SPI1->DR = 0x00;
      while (!(SPI1->SR & SPI_SR_RXNE));
      uint8_t lo = *(volatile uint8_t*)&SPI1->DR;

      // Wait for SPI idle
      while (SPI1->SR & SPI_SR_BSY);

      // Pull CS high (PB6)
      GPIOB->BSRR = (1UL << 6);  // PB6 high

      uint16_t status = (hi << 8) | lo;
      snprintf(buf, sizeof(buf), "Mode%d: STATUS=%04X (hi=%02X lo=%02X)",
               mode, status, hi, lo);
      m_transport.println(buf);
    }

    // Restore Mode 3
    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= (SPI_CR1_CPOL | SPI_CR1_CPHA);
    SPI1->CR1 |= SPI_CR1_SPE;

    m_transport.println("Done. Mode 3 restored.");
  } else if (strcmp(cmd.cmd, "CS_LOW") == 0) {
    // Force CS (PB6) low for measurement
    GPIOB->BSRR = (1UL << (6 + 16));  // PB6 low
    m_transport.println("PB6 (CS/D10) forced LOW. Measure voltage now.");
    m_transport.println("Send CS_HIGH to release.");
  } else if (strcmp(cmd.cmd, "CS_HIGH") == 0) {
    // Release CS (PB6) high
    GPIOB->BSRR = (1UL << 6);  // PB6 high
    m_transport.println("PB6 (CS/D10) released HIGH.");
  } else if (strcmp(cmd.cmd, "MISO_PULLDOWN") == 0) {
    // Enable pull-down on PA6 (MISO)
    GPIOA->PUPDR &= ~(0x3UL << 12);  // Clear PUPDR6
    GPIOA->PUPDR |= (0x2UL << 12);   // Pull-down on PA6
    m_transport.println("PA6 (MISO) pull-down ENABLED.");
  } else if (strcmp(cmd.cmd, "MISO_NOPULL") == 0) {
    // Disable pull on PA6 (MISO)
    GPIOA->PUPDR &= ~(0x3UL << 12);  // Clear PUPDR6 (no pull)
    m_transport.println("PA6 (MISO) pull DISABLED.");
  } else if (strcmp(cmd.cmd, "STBY_RELEASE") == 0) {
    // Release STBY/RST (PA9) - set HIGH for normal operation
    GPIOA->MODER &= ~(0x3UL << 18);  // Clear MODER9
    GPIOA->MODER |= (0x1UL << 18);   // Output mode
    GPIOA->BSRR = (1UL << 9);        // PA9 high
    m_transport.println("PA9 (STBY_RST) set HIGH - powerSTEP01 should be active.");
  } else if (strcmp(cmd.cmd, "STBY_HOLD") == 0) {
    // Hold STBY/RST (PA9) - set LOW for standby/reset
    GPIOA->MODER &= ~(0x3UL << 18);  // Clear MODER9
    GPIOA->MODER |= (0x1UL << 18);   // Output mode
    GPIOA->BSRR = (1UL << (9 + 16)); // PA9 low
    m_transport.println("PA9 (STBY_RST) set LOW - powerSTEP01 in standby/reset.");
  } else if (strcmp(cmd.cmd, "GPIO_STATE") == 0) {
    // Show GPIO states for motor pins
    char buf[256];
    uint32_t pa_idr = GPIOA->IDR;
    uint32_t pb_idr = GPIOB->IDR;
    snprintf(buf, sizeof(buf),
             "PA: IDR=%04X (MISO/PA6=%d, STBY/PA9=%d, FLAG/PA10=%d)",
             (unsigned)(pa_idr & 0xFFFF),
             (int)((pa_idr >> 6) & 1),
             (int)((pa_idr >> 9) & 1),
             (int)((pa_idr >> 10) & 1));
    m_transport.println(buf);
    snprintf(buf, sizeof(buf),
             "PB: IDR=%04X (BUSY/PB4=%d, CS/PB6=%d)",
             (unsigned)(pb_idr & 0xFFFF),
             (int)((pb_idr >> 4) & 1),
             (int)((pb_idr >> 6) & 1));
    m_transport.println(buf);
  } else if (strcmp(cmd.cmd, "PA6_HIZ") == 0) {
    // Force PA6 to high-impedance input to test resistance
    // NOTE: Don't disable SPI1 - motor task uses it and will hang
    char buf[128];

    // Set PA6 as input with no pull (SPI1 stays enabled but won't drive PA6)
    GPIOA->MODER &= ~(0x3UL << 12);   // Input mode (00)
    GPIOA->PUPDR &= ~(0x3UL << 12);   // No pull (00)

    snprintf(buf, sizeof(buf), "PA6 set to Hi-Z input. MODER=%08lX PUPDR=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOA->PUPDR);
    m_transport.println(buf);
    m_transport.println("Measure PA6 to GND now - should be MΩ if no external load.");
    m_transport.println("Send PA6_RESTORE to restore SPI config.");
  } else if (strcmp(cmd.cmd, "PA6_RESTORE") == 0) {
    // Restore PA6 to SPI MISO
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->MODER |= (0x2UL << 12);    // AF mode (10)
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);    // Pull-down (10)
    m_transport.println("PA6 restored to SPI1 MISO with pull-down.");
  } else if (strcmp(cmd.cmd, "SPI_STOP") == 0) {
    // Completely stop SPI and set all SPI pins to hi-Z for measurement
    char buf[80];

    // FIRST: Suspend motor task to prevent SPI access during disable
    Tasks::MotorTask_Suspend();
    m_transport.println("Motor task suspended.");

    // Disable SPI1 peripheral
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Set PA5 (SCK), PA6 (MISO), PA7 (MOSI) as inputs with no pull
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));  // Input mode
    GPIOA->PUPDR &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));  // No pull

    // Set PB6 (CS) as input with no pull
    GPIOB->MODER &= ~(0x3UL << 12);  // Input mode for PB6
    GPIOB->PUPDR &= ~(0x3UL << 12);  // No pull

    m_transport.println("SPI1 DISABLED. All pins Hi-Z.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX GPIOB MODER=%08lX",
             (unsigned long)GPIOA->MODER, (unsigned long)GPIOB->MODER);
    m_transport.println(buf);
    m_transport.println("Measure pins. Send SPI_START to restore.");
  } else if (strcmp(cmd.cmd, "SPI_START") == 0) {
    // Restore SPI pins and re-enable SPI1
    char buf[128];

    // Set PA5 (SCK), PA7 (MOSI) as AF5, PA6 (MISO) as AF5 with pull-down
    // PA5: AF mode (10)
    GPIOA->MODER &= ~(0x3UL << 10);
    GPIOA->MODER |= (0x2UL << 10);
    // PA6: AF mode (10) with pull-down
    GPIOA->MODER &= ~(0x3UL << 12);
    GPIOA->MODER |= (0x2UL << 12);
    GPIOA->PUPDR &= ~(0x3UL << 12);
    GPIOA->PUPDR |= (0x2UL << 12);
    // PA7: AF mode (10)
    GPIOA->MODER &= ~(0x3UL << 14);
    GPIOA->MODER |= (0x2UL << 14);

    // Set PB6 (CS) as output, high
    GPIOB->MODER &= ~(0x3UL << 12);
    GPIOB->MODER |= (0x1UL << 12);  // Output mode
    GPIOB->BSRR = (1UL << 6);       // PB6 high

    // Re-enable SPI1
    SPI1->CR1 |= SPI_CR1_SPE;

    m_transport.println("SPI1 ENABLED. Pins restored.");
    snprintf(buf, sizeof(buf), "GPIOA MODER=%08lX SPI1 CR1=%04lX SR=%04lX",
             (unsigned long)GPIOA->MODER,
             (unsigned long)SPI1->CR1,
             (unsigned long)SPI1->SR);
    m_transport.println(buf);

    // Resume motor task
    Tasks::MotorTask_Resume();
    m_transport.println("Motor task resumed.");
  } else if (strcmp(cmd.cmd, "GPIO_DUMP") == 0) {
    // Dump GPIO register states for debugging
    char buf[128];
    m_transport.println("=== GPIOA Registers ===");
    snprintf(buf, sizeof(buf), "MODER=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->MODER,
             (unsigned long)((GPIOA->MODER >> 12) & 0x3));
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "OTYPER=%04lX  OSPEEDR=%08lX",
             (unsigned long)GPIOA->OTYPER,
             (unsigned long)GPIOA->OSPEEDR);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "PUPDR=%08lX  (PA6 bits 13:12 = %lu)",
             (unsigned long)GPIOA->PUPDR,
             (unsigned long)((GPIOA->PUPDR >> 12) & 0x3));
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "IDR=%04lX  ODR=%04lX",
             (unsigned long)GPIOA->IDR,
             (unsigned long)GPIOA->ODR);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "AFR[0]=%08lX  (PA6 bits 27:24 = %lu)",
             (unsigned long)GPIOA->AFR[0],
             (unsigned long)((GPIOA->AFR[0] >> 24) & 0xF));
    m_transport.println(buf);
    m_transport.println("=== SPI1 Registers ===");
    snprintf(buf, sizeof(buf), "CR1=%04lX  CR2=%04lX  SR=%04lX",
             (unsigned long)SPI1->CR1,
             (unsigned long)SPI1->CR2,
             (unsigned long)SPI1->SR);
    m_transport.println(buf);
    m_transport.println("PA6 MODER: 00=input, 01=output, 10=AF, 11=analog");
    m_transport.println("PA6 PUPDR: 00=none, 01=up, 10=down");
  } else if (strcmp(cmd.cmd, "SPI_FLOAT_TEST") == 0) {
    // Test SPI pins as floating inputs to detect external pulls
    char buf[128];
    m_transport.println("Testing SPI pins as floating inputs...");

    // Disable SPI1
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Save current MODER
    uint32_t saved_moder = GPIOA->MODER;
    uint32_t saved_pupdr = GPIOA->PUPDR;

    // Set PA5 (SCK), PA6 (MISO), PA7 (MOSI) as inputs with no pull
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));  // Input mode
    GPIOA->PUPDR &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));  // No pull

    // Wait for pins to settle
    for (volatile int d = 0; d < 1000; d++);

    // Read pin states
    uint32_t idr = GPIOA->IDR;
    int sck_state = (idr >> 5) & 1;
    int miso_state = (idr >> 6) & 1;
    int mosi_state = (idr >> 7) & 1;

    snprintf(buf, sizeof(buf), "Floating: SCK/PA5=%d, MISO/PA6=%d, MOSI/PA7=%d",
             sck_state, miso_state, mosi_state);
    m_transport.println(buf);

    // Now enable pull-ups and re-read
    GPIOA->PUPDR |= ((0x1UL << 10) | (0x1UL << 12) | (0x1UL << 14));  // Pull-up
    for (volatile int d = 0; d < 1000; d++);

    idr = GPIOA->IDR;
    sck_state = (idr >> 5) & 1;
    miso_state = (idr >> 6) & 1;
    mosi_state = (idr >> 7) & 1;

    snprintf(buf, sizeof(buf), "With pull-up: SCK/PA5=%d, MISO/PA6=%d, MOSI/PA7=%d",
             sck_state, miso_state, mosi_state);
    m_transport.println(buf);

    // Interpretation
    m_transport.println("If a pin stays 0 with pull-up, external pull-down exists.");
    m_transport.println("If a pin floats (random), no external pull.");

    // Restore
    GPIOA->PUPDR = saved_pupdr;
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;

    m_transport.println("SPI restored.");
  } else if (strcmp(cmd.cmd, "SPI_BITBANG") == 0) {
    // Bit-bang SPI test to verify physical connections
    char buf[128];
    m_transport.println("Bit-bang SPI test (bypasses SPI peripheral)...");

    // Disable SPI1 and switch pins to GPIO
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Save current MODER for PA5,6,7
    uint32_t saved_moder = GPIOA->MODER;

    // Set PA5 (SCK) and PA7 (MOSI) as outputs, PA6 (MISO) as input
    GPIOA->MODER &= ~((0x3 << 10) | (0x3 << 12) | (0x3 << 14));  // Clear PA5,6,7
    GPIOA->MODER |= ((0x1 << 10) | (0x1 << 14));  // PA5,PA7 output; PA6 input

    // Pull CS low
    GPIOB->BSRR = (1UL << (6 + 16));  // PB6 low
    for (volatile int d = 0; d < 100; d++);

    // Send 0xD0 (GetStatus) bit by bit, MSB first
    uint8_t txByte = 0xD0;
    uint8_t rxByte = 0;

    for (int bit = 7; bit >= 0; bit--) {
      // Set MOSI
      if (txByte & (1 << bit)) {
        GPIOA->BSRR = (1 << 7);  // PA7 high
      } else {
        GPIOA->BSRR = (1 << (7 + 16));  // PA7 low
      }
      for (volatile int d = 0; d < 10; d++);

      // Rising edge of SCK
      GPIOA->BSRR = (1 << 5);  // PA5 high
      for (volatile int d = 0; d < 10; d++);

      // Sample MISO
      if (GPIOA->IDR & (1 << 6)) {
        rxByte |= (1 << bit);
      }

      // Falling edge of SCK
      GPIOA->BSRR = (1 << (5 + 16));  // PA5 low
      for (volatile int d = 0; d < 10; d++);
    }

    snprintf(buf, sizeof(buf), "TX: 0x%02X  RX: 0x%02X", txByte, rxByte);
    m_transport.println(buf);

    // Read 2 more bytes (status high and low)
    uint8_t hi = 0, lo = 0;
    for (int byteNum = 0; byteNum < 2; byteNum++) {
      uint8_t rx = 0;
      for (int bit = 7; bit >= 0; bit--) {
        // MOSI = 0 for read
        GPIOA->BSRR = (1 << (7 + 16));  // PA7 low
        for (volatile int d = 0; d < 10; d++);

        // Rising edge
        GPIOA->BSRR = (1 << 5);  // PA5 high
        for (volatile int d = 0; d < 10; d++);

        // Sample MISO
        if (GPIOA->IDR & (1 << 6)) {
          rx |= (1 << bit);
        }

        // Falling edge
        GPIOA->BSRR = (1 << (5 + 16));  // PA5 low
        for (volatile int d = 0; d < 10; d++);
      }
      if (byteNum == 0) hi = rx;
      else lo = rx;
    }

    // Pull CS high
    GPIOB->BSRR = (1UL << 6);  // PB6 high

    snprintf(buf, sizeof(buf), "STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", hi, lo, (hi << 8) | lo);
    m_transport.println(buf);

    // Check MISO state with CS high
    for (volatile int d = 0; d < 100; d++);
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    m_transport.println(buf);

    // Restore MODER and re-enable SPI
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;

    m_transport.println("Done. SPI restored.");
  } else if (strcmp(cmd.cmd, "SPI_BITBANG_M3") == 0) {
    // Bit-bang SPI Mode 3 test (CPOL=1, CPHA=1) for powerSTEP01
    char buf[128];
    m_transport.println("Bit-bang SPI Mode 3 test (CPOL=1, CPHA=1)...");

    // Disable SPI1 and switch pins to GPIO
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Save current MODER for PA5,6,7
    uint32_t saved_moder = GPIOA->MODER;

    // Set PA5 (SCK) and PA7 (MOSI) as outputs, PA6 (MISO) as input
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));  // PA5,PA7 output; PA6 input

    // Mode 3: Clock idle HIGH
    GPIOA->BSRR = (1UL << 5);  // PA5 high (SCK idle)
    for (volatile int d = 0; d < 100; d++);

    // Pull CS low
    GPIOB->BSRR = (1UL << (6 + 16));  // PB6 low
    for (volatile int d = 0; d < 100; d++);

    // Lambda for Mode 3 byte transfer
    auto transferByteM3 = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      for (int bit = 7; bit >= 0; bit--) {
        // Mode 3: Falling edge - shift out MOSI
        GPIOA->BSRR = (1UL << (5 + 16));  // SCK low (falling edge)

        // Set MOSI
        if (tx & (1 << bit)) {
          GPIOA->BSRR = (1UL << 7);  // PA7 high
        } else {
          GPIOA->BSRR = (1UL << (7 + 16));  // PA7 low
        }
        for (volatile int d = 0; d < 20; d++);

        // Mode 3: Rising edge - sample MISO
        GPIOA->BSRR = (1UL << 5);  // SCK high (rising edge)
        for (volatile int d = 0; d < 20; d++);

        // Sample MISO
        if (GPIOA->IDR & (1UL << 6)) {
          rx |= (1 << bit);
        }
      }
      return rx;
    };

    // Send GetStatus command (0xD0)
    uint8_t rx0 = transferByteM3(0xD0);
    uint8_t rx1 = transferByteM3(0x00);  // NOP to clock out STATUS[15:8]
    uint8_t rx2 = transferByteM3(0x00);  // NOP to clock out STATUS[7:0]

    // Pull CS high
    GPIOB->BSRR = (1UL << 6);  // PB6 high

    snprintf(buf, sizeof(buf), "TX: 0xD0  RX0: 0x%02X", rx0);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", rx1, rx2, (rx1 << 8) | rx2);
    m_transport.println(buf);

    // Check MISO state with CS high
    for (volatile int d = 0; d < 100; d++);
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "MISO idle (CS high): %d", miso_idle);
    m_transport.println(buf);

    // Decode status bits
    uint16_t status = (rx1 << 8) | rx2;
    if (status != 0) {
      m_transport.println("Status bits:");
      if (status & 0x0001) m_transport.println("  HiZ: outputs in high-Z");
      if (!(status & 0x0002)) m_transport.println("  BUSY: command in progress");
      if (!(status & 0x0200)) m_transport.println("  UVLO: undervoltage!");
      if (!(status & 0x2000)) m_transport.println("  OCD: overcurrent!");
      if (!(status & 0x4000)) m_transport.println("  STEP_LOSS_A: stall bridge A!");
      if (!(status & 0x8000)) m_transport.println("  STEP_LOSS_B: stall bridge B!");
      if (status & 0x0080) m_transport.println("  CMD_ERROR: bad command");
    }

    // Restore MODER and re-enable SPI
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;

    m_transport.println("Done. SPI restored.");
  } else if (strcmp(cmd.cmd, "SPI_ECHO_TEST") == 0) {
    // Test if MISO is echoing MOSI (coupling check)
    // Send different patterns and see if they appear on MISO
    char buf[128];
    m_transport.println("SPI Echo/Coupling Test...");
    m_transport.println("Sending various patterns to detect MOSI->MISO coupling");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    // Mode 3 transfer lambda
    auto xfer = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);  // SCK high (idle)
      for (volatile int d = 0; d < 20; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));  // SCK low
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);  // SCK high
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      return rx;
    };

    // Test different patterns with CS LOW
    uint8_t patterns[] = {0xD0, 0x55, 0xAA, 0xFF, 0x00, 0x29};
    m_transport.println("With CS LOW (PB6=0):");
    GPIOB->BSRR = (1UL << (6 + 16));  // CS low
    for (volatile int d = 0; d < 100; d++);

    for (int p = 0; p < 6; p++) {
      uint8_t tx = patterns[p];
      uint8_t rx0 = xfer(tx);
      uint8_t rx1 = xfer(0x00);
      snprintf(buf, sizeof(buf), "  TX=0x%02X -> RX0=0x%02X, RX1(NOP)=0x%02X %s",
               tx, rx0, rx1, (rx1 == tx) ? "<- ECHO!" : "");
      m_transport.println(buf);
    }
    GPIOB->BSRR = (1UL << 6);  // CS high

    // Now test with CS HIGH (should get nothing)
    m_transport.println("With CS HIGH (PB6=1) - chip should NOT respond:");
    for (volatile int d = 0; d < 100; d++);
    uint8_t rx_cs_hi_0 = xfer(0xD0);
    uint8_t rx_cs_hi_1 = xfer(0x00);
    snprintf(buf, sizeof(buf), "  TX=0xD0 -> RX=0x%02X, RX(NOP)=0x%02X", rx_cs_hi_0, rx_cs_hi_1);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done.");
  } else if (strcmp(cmd.cmd, "SPI_CS_TOGGLE") == 0) {
    // powerSTEP01 requires CS HIGH between EACH byte (≥625ns)!
    // This is NOT standard SPI - CS must toggle per byte
    char buf[128];
    m_transport.println("SPI with CS toggle per byte (powerSTEP01 requirement)...");
    m_transport.println("CS must go HIGH for >=625ns between each byte!");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    // Set PA5 (SCK), PA7 (MOSI) as outputs, PA6 (MISO) as input
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    // Lambda: transfer one byte with proper Mode 3 timing
    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      // Clock starts HIGH (Mode 3 idle)
      GPIOA->BSRR = (1UL << 5);  // SCK high
      for (volatile int d = 0; d < 10; d++);

      for (int bit = 7; bit >= 0; bit--) {
        // Falling edge - shift out MOSI
        GPIOA->BSRR = (1UL << (5 + 16));  // SCK low
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);

        // Rising edge - sample MISO
        GPIOA->BSRR = (1UL << 5);  // SCK high
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      return rx;
    };

    // Ensure CS HIGH and clock HIGH at start
    GPIOB->BSRR = (1UL << 6);  // PB6 high
    GPIOA->BSRR = (1UL << 5);  // SCK high (Mode 3 idle)
    for (volatile int d = 0; d < 100; d++);  // Setup time

    // === Send GetStatus command (0xD0) with CS per byte ===
    m_transport.println("GetStatus (0xD0) with CS toggle per byte:");

    // Byte 1: Command
    GPIOB->BSRR = (1UL << (6 + 16));  // CS LOW
    for (volatile int d = 0; d < 10; d++);
    uint8_t rx0 = xferByte(0xD0);
    GPIOB->BSRR = (1UL << 6);  // CS HIGH
    for (volatile int d = 0; d < 100; d++);  // >=625ns high time

    // Byte 2: NOP to get STATUS[15:8]
    GPIOB->BSRR = (1UL << (6 + 16));  // CS LOW
    for (volatile int d = 0; d < 10; d++);
    uint8_t rx1 = xferByte(0x00);
    GPIOB->BSRR = (1UL << 6);  // CS HIGH
    for (volatile int d = 0; d < 100; d++);

    // Byte 3: NOP to get STATUS[7:0]
    GPIOB->BSRR = (1UL << (6 + 16));  // CS LOW
    for (volatile int d = 0; d < 10; d++);
    uint8_t rx2 = xferByte(0x00);
    GPIOB->BSRR = (1UL << 6);  // CS HIGH

    snprintf(buf, sizeof(buf), "  Cmd RX: 0x%02X", rx0);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  STATUS: hi=0x%02X lo=0x%02X (raw=0x%04X)", rx1, rx2, (rx1 << 8) | rx2);
    m_transport.println(buf);

    // Decode status
    uint16_t status = (rx1 << 8) | rx2;
    m_transport.println("  Status decode:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d SW=%d DIR=%d",
             (status & 0x0001) ? 1 : 0,
             (status & 0x0002) ? 0 : 1,
             (status & 0x0004) ? 1 : 0,
             (status & 0x0010) ? 1 : 0);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d TH_SD=%d OCD=%d STALL=%d",
             (status & 0x0200) ? 0 : 1,
             (status & 0x0800) ? 0 : 1,
             (status & 0x8000) ? 0 : 1,
             ((status & 0x6000) != 0x6000) ? 1 : 0);
    m_transport.println(buf);

    // Check MISO idle
    int miso_idle = (GPIOA->IDR >> 6) & 1;
    snprintf(buf, sizeof(buf), "  MISO idle: %d", miso_idle);
    m_transport.println(buf);

    // Restore
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done.");
  } else if (strcmp(cmd.cmd, "PS01_DIAG") == 0) {
    // Read powerSTEP01 diagnostic registers to understand thermal issue
    char buf[128];
    m_transport.println("powerSTEP01 Diagnostic Registers:");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    // Lambda: transfer one byte with CS toggle
    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);  // SCK high (idle)
      GPIOB->BSRR = (1UL << (6 + 16));  // CS low
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));  // SCK low
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);  // SCK high
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);  // CS high
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // Lambda: read 2-byte register
    auto readReg2 = [&xferByte](uint8_t regAddr) -> uint16_t {
      xferByte(0x20 | regAddr);  // GetParam command
      uint8_t hi = xferByte(0x00);
      uint8_t lo = xferByte(0x00);
      return (hi << 8) | lo;
    };

    // Lambda: read 1-byte register
    auto readReg1 = [&xferByte](uint8_t regAddr) -> uint8_t {
      xferByte(0x20 | regAddr);  // GetParam command
      return xferByte(0x00);
    };

    // Read key registers
    uint16_t config = readReg2(0x1A);
    uint8_t adc_out = readReg1(0x12);
    uint8_t alarm_en = readReg1(0x17);
    uint8_t ocd_th = readReg1(0x13);
    uint8_t stall_th = readReg1(0x14);
    uint16_t gatecfg1 = readReg2(0x18);
    uint8_t k_therm = readReg1(0x11);

    snprintf(buf, sizeof(buf), "  CONFIG: 0x%04X", config);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  ADC_OUT: 0x%02X (raw ADC value)", adc_out);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  ALARM_EN: 0x%02X", alarm_en);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  OCD_TH: 0x%02X (overcurrent threshold)", ocd_th);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  STALL_TH: 0x%02X (stall threshold)", stall_th);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  GATECFG1: 0x%04X", gatecfg1);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "  K_THERM: 0x%02X (thermal compensation)", k_therm);
    m_transport.println(buf);

    // Decode CONFIG bits
    m_transport.println("  CONFIG decode:");
    snprintf(buf, sizeof(buf), "    OSC_SEL=%d EXT_CLK=%d SW_MODE=%d EN_VSCOMP=%d",
             config & 0x7, (config >> 3) & 1, (config >> 4) & 1, (config >> 5) & 1);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "    OC_SD=%d POW_SR=%d F_PWM_DEC=%d F_PWM_INT=%d",
             (config >> 7) & 1, (config >> 8) & 3, (config >> 10) & 7, (config >> 13) & 7);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done.");
  } else if (strcmp(cmd.cmd, "PS01_FIX_CLK") == 0) {
    // Fix powerSTEP01 clock configuration - use internal 16MHz oscillator
    // The X-NUCLEO-IHM03A1 doesn't have external clock, but EXT_CLK=1 by default!
    char buf[128];
    m_transport.println("Fixing powerSTEP01 clock config (internal 16MHz)...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    // Transfer helpers
    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      GPIOB->BSRR = (1UL << (6 + 16));
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // Read current CONFIG
    xferByte(0x20 | 0x1A);  // GetParam CONFIG
    uint8_t hi = xferByte(0x00);
    uint8_t lo = xferByte(0x00);
    uint16_t oldConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Old CONFIG: 0x%04X (EXT_CLK=%d)", oldConfig, (oldConfig >> 3) & 1);
    m_transport.println(buf);

    // New CONFIG: clear EXT_CLK bit (bit 3), keep internal oscillator
    // 0x2C88 -> 0x2C80
    uint16_t newConfig = oldConfig & ~(1 << 3);  // Clear EXT_CLK
    snprintf(buf, sizeof(buf), "  New CONFIG: 0x%04X (EXT_CLK=0)", newConfig);
    m_transport.println(buf);

    // Write new CONFIG (SetParam = 0x00 | reg)
    xferByte(0x00 | 0x1A);  // SetParam CONFIG
    xferByte((newConfig >> 8) & 0xFF);
    xferByte(newConfig & 0xFF);

    // Verify
    xferByte(0x20 | 0x1A);
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    uint16_t verifyConfig = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  Verify CONFIG: 0x%04X", verifyConfig);
    m_transport.println(buf);

    // Read status to clear flags
    xferByte(0xD0);  // GetStatus
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after fix: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done. Try MOVE command now.");
  } else if (strcmp(cmd.cmd, "PS01_SAFE") == 0) {
    // Raise thresholds and disable OCD shutdown to prevent spurious trips
    char buf[128];
    m_transport.println("Configuring safe settings (raise thresholds)...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      GPIOB->BSRR = (1UL << (6 + 16));
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // 1. Lower KVAL values (reduce current)
    m_transport.println("  KVAL_HOLD/RUN/ACC/DEC = 0x10 (low current)");
    xferByte(0x00 | 0x09); xferByte(0x10);  // KVAL_HOLD
    xferByte(0x00 | 0x0A); xferByte(0x10);  // KVAL_RUN
    xferByte(0x00 | 0x0B); xferByte(0x10);  // KVAL_ACC
    xferByte(0x00 | 0x0C); xferByte(0x10);  // KVAL_DEC

    // 2. MAX OCD threshold (0x1F = ~10A)
    m_transport.println("  OCD_TH = 0x1F (max ~10A threshold)");
    xferByte(0x00 | 0x13); xferByte(0x1F);

    // 3. MAX STALL threshold
    m_transport.println("  STALL_TH = 0x7F (max threshold)");
    xferByte(0x00 | 0x14); xferByte(0x7F);

    // 4. Set GATECFG2 for longer blanking time (TBLANK = 1000ns)
    // GATECFG2 bits: TDT[4:0], TBLANK[2:0]
    // TBLANK = 111 = 1000ns, TDT = max
    m_transport.println("  GATECFG2 = 0xFF (max blanking/deadtime)");
    xferByte(0x00 | 0x19); xferByte(0xFF);

    // 5. Disable OC_SD in CONFIG (OCD won't cause shutdown)
    xferByte(0x20 | 0x1A);  // GetParam CONFIG
    uint8_t hi = xferByte(0x00);
    uint8_t lo = xferByte(0x00);
    uint16_t config = (hi << 8) | lo;
    config &= ~(1 << 7);  // Clear OC_SD bit
    snprintf(buf, sizeof(buf), "  CONFIG = 0x%04X (OC_SD disabled)", config);
    m_transport.println(buf);
    xferByte(0x00 | 0x1A);
    xferByte((config >> 8) & 0xFF);
    xferByte(config & 0xFF);

    // 6. Disable UVLO_ADC alarm in ALARM_EN
    // ALARM_EN bit 4 = UVLO_ADC
    m_transport.println("  ALARM_EN = 0xEF (disable UVLO_ADC alarm)");
    xferByte(0x00 | 0x17); xferByte(0xEF);

    // GetStatus to clear flags
    xferByte(0xD0);
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done. Try MOVE now.");
  } else if (strcmp(cmd.cmd, "PS01_RESET") == 0) {
    // Reset device and clear all latched faults, disable thermal alarms
    char buf[128];
    m_transport.println("Resetting powerSTEP01 and clearing faults...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      GPIOB->BSRR = (1UL << (6 + 16));
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // 1. Send HardHiZ to disable bridges
    m_transport.println("  Sending HardHiZ (0xA8)...");
    xferByte(0xA8);

    // 2. Send ResetDevice command (0xC0) - clears all registers to default
    m_transport.println("  Sending ResetDevice (0xC0)...");
    xferByte(0xC0);

    // 3. Brief delay for reset to complete
    for (volatile int d = 0; d < 50000; d++);

    // 4. GetStatus to clear latched fault flags
    m_transport.println("  Clearing latched faults (GetStatus)...");
    xferByte(0xD0);
    uint8_t hi = xferByte(0x00);
    uint8_t lo = xferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS after reset: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    // 5. Configure with thermal alarms disabled
    // ALARM_EN: disable OCD(0), TH_SD(1), TH_WRN(2), UVLO_ADC(4)
    // Keep UVLO(3), STALL_A(5), STALL_B(6), SW(7) enabled
    // Mask = ~(0x17) = 0xE8, but let's just disable thermal: 0xE8
    m_transport.println("  ALARM_EN = 0xE8 (disable OCD, thermal, UVLO_ADC alarms)");
    xferByte(0x00 | 0x17); xferByte(0xE8);

    // 6. Set safe KVAL values
    m_transport.println("  KVAL = 0x10 (low current)");
    xferByte(0x00 | 0x09); xferByte(0x10);  // KVAL_HOLD
    xferByte(0x00 | 0x0A); xferByte(0x10);  // KVAL_RUN
    xferByte(0x00 | 0x0B); xferByte(0x10);  // KVAL_ACC
    xferByte(0x00 | 0x0C); xferByte(0x10);  // KVAL_DEC

    // 7. MAX OCD threshold
    m_transport.println("  OCD_TH = 0x1F, STALL_TH = 0x7F");
    xferByte(0x00 | 0x13); xferByte(0x1F);
    xferByte(0x00 | 0x14); xferByte(0x7F);

    // 8. GATECFG2 max blanking
    xferByte(0x00 | 0x19); xferByte(0xFF);

    // 9. Disable OC_SD in CONFIG
    xferByte(0x20 | 0x1A);
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    uint16_t config = (hi << 8) | lo;
    config &= ~(1 << 7);  // Clear OC_SD
    snprintf(buf, sizeof(buf), "  CONFIG = 0x%04X (OC_SD disabled)", config);
    m_transport.println(buf);
    xferByte(0x00 | 0x1A);
    xferByte((config >> 8) & 0xFF);
    xferByte(config & 0xFF);

    // 10. Final status check
    xferByte(0xD0);
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    snprintf(buf, sizeof(buf), "  Final STATUS: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    // Decode key bits
    uint16_t status = (hi << 8) | lo;
    m_transport.println("  Decoded:");
    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d DIR=%d MOT_STATUS=%d",
             (status >> 0) & 1,
             !((status >> 1) & 1),
             (status >> 4) & 1,
             (status >> 5) & 3);
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "    UVLO=%d UVLO_ADC=%d TH_SD=%d TH_WRN=%d",
             !((status >> 9) & 1),
             !((status >> 10) & 1),
             !((status >> 11) & 1),
             !((status >> 12) & 1));
    m_transport.println(buf);
    snprintf(buf, sizeof(buf), "    STALL_A=%d STALL_B=%d OCD=%d",
             !((status >> 13) & 1),
             !((status >> 14) & 1),
             !((status >> 15) & 1));
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Reset complete. Try MOVE now.");
  } else if (strcmp(cmd.cmd, "PS01_RUN") == 0) {
    // Try running motor with higher KVAL and RUN command
    // Usage: PS01_RUN [kval] [speed] [dir]
    // kval: 0x00-0xFF (default 0x40)
    // speed: 0-0xFFFFF (default 0x5000)
    // dir: 0=rev, 1=fwd (default 1)
    char buf[128];
    uint8_t kval = 0x40;  // Default higher KVAL
    uint32_t speed = 0x5000;  // Default speed
    bool fwd = true;

    if (cmd.argCount >= 1) kval = (uint8_t)strtoul(cmd.args[0], nullptr, 0);
    if (cmd.argCount >= 2) speed = (uint32_t)strtoul(cmd.args[1], nullptr, 0);
    if (cmd.argCount >= 3) fwd = strtol(cmd.args[2], nullptr, 0) != 0;

    snprintf(buf, sizeof(buf), "Running motor: KVAL=0x%02X speed=0x%lX dir=%s",
             kval, (unsigned long)speed, fwd ? "FWD" : "REV");
    m_transport.println(buf);

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      GPIOB->BSRR = (1UL << (6 + 16));
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // 1. Set KVAL registers
    m_transport.println("  Setting KVAL...");
    xferByte(0x00 | 0x09); xferByte(kval);  // KVAL_HOLD
    xferByte(0x00 | 0x0A); xferByte(kval);  // KVAL_RUN
    xferByte(0x00 | 0x0B); xferByte(kval);  // KVAL_ACC
    xferByte(0x00 | 0x0C); xferByte(kval);  // KVAL_DEC

    // 2. Set reasonable ACC/DEC (0x08A default is fine)
    m_transport.println("  Setting ACC/DEC=0x08A...");
    xferByte(0x00 | 0x05); xferByte(0x00); xferByte(0x8A);  // ACC
    xferByte(0x00 | 0x06); xferByte(0x00); xferByte(0x8A);  // DEC

    // 3. Set MAX_SPEED
    m_transport.println("  Setting MAX_SPEED=0x41...");
    xferByte(0x00 | 0x07); xferByte(0x00); xferByte(0x41);

    // 4. Clear any faults with GetStatus
    xferByte(0xD0);
    uint8_t hi = xferByte(0x00);
    uint8_t lo = xferByte(0x00);
    snprintf(buf, sizeof(buf), "  STATUS before RUN: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    // 5. Send RUN command (0x50 | direction, followed by 3 speed bytes)
    m_transport.println("  Sending RUN command...");
    xferByte(0x50 | (fwd ? 1 : 0));  // RUN | direction
    xferByte((speed >> 16) & 0x0F);  // Speed [19:16]
    xferByte((speed >> 8) & 0xFF);   // Speed [15:8]
    xferByte(speed & 0xFF);          // Speed [7:0]

    // 6. Brief delay then check status
    for (volatile int d = 0; d < 10000; d++);
    xferByte(0xD0);
    hi = xferByte(0x00);
    lo = xferByte(0x00);
    uint16_t status = (hi << 8) | lo;
    snprintf(buf, sizeof(buf), "  STATUS after RUN: 0x%04X", status);
    m_transport.println(buf);

    snprintf(buf, sizeof(buf), "    HiZ=%d BUSY=%d MOT_STATUS=%d",
             (status >> 0) & 1,
             !((status >> 1) & 1),
             (status >> 5) & 3);
    m_transport.println(buf);

    // MOT_STATUS: 0=stopped, 1=acc, 2=dec, 3=const speed
    const char* motStr[] = {"STOPPED", "ACCELERATING", "DECELERATING", "CONST_SPEED"};
    snprintf(buf, sizeof(buf), "    Motion: %s", motStr[(status >> 5) & 3]);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Use PS01_STOP to stop motor.");
  } else if (strcmp(cmd.cmd, "PS01_STOP") == 0) {
    // Stop motor
    m_transport.println("Stopping motor (SoftStop)...");

    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    auto xferByte = [](uint8_t tx) -> uint8_t {
      uint8_t rx = 0;
      GPIOA->BSRR = (1UL << 5);
      GPIOB->BSRR = (1UL << (6 + 16));
      for (volatile int d = 0; d < 10; d++);
      for (int bit = 7; bit >= 0; bit--) {
        GPIOA->BSRR = (1UL << (5 + 16));
        if (tx & (1 << bit)) GPIOA->BSRR = (1UL << 7);
        else GPIOA->BSRR = (1UL << (7 + 16));
        for (volatile int d = 0; d < 20; d++);
        GPIOA->BSRR = (1UL << 5);
        for (volatile int d = 0; d < 20; d++);
        if (GPIOA->IDR & (1UL << 6)) rx |= (1 << bit);
      }
      GPIOB->BSRR = (1UL << 6);
      for (volatile int d = 0; d < 100; d++);
      return rx;
    };

    // SoftStop command
    xferByte(0xB0);

    // Check status
    for (volatile int d = 0; d < 10000; d++);
    xferByte(0xD0);
    uint8_t hi = xferByte(0x00);
    uint8_t lo = xferByte(0x00);
    char buf[64];
    snprintf(buf, sizeof(buf), "  STATUS: 0x%04X", (hi << 8) | lo);
    m_transport.println(buf);

    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done.");
  } else if (strcmp(cmd.cmd, "LCD_DISABLE") == 0) {
    // Disable LCD/display task and float all LCD/joystick pins
    // This isolates SPI1 for motor-only testing
    char buf[128];
    m_transport.println("Disabling LCD and display task...");

    // Suspend display task to stop all LCD/joystick activity
    Tasks::DisplayTask_Suspend();
    m_transport.println("Display task suspended.");

    // Float LCD pins:
    // PC6 = LCD CS
    GPIOC->MODER &= ~(0x3UL << 12);  // PC6 input
    // PB10 = LCD DC
    GPIOB->MODER &= ~(0x3UL << 20);  // PB10 input

    // Float joystick pins (critical: PB6 conflicts with motor CS!)
    // PC7 = KEY_LEFT
    GPIOC->MODER &= ~(0x3UL << 14);  // PC7 input
    // PC9 = KEY_DOWN
    GPIOC->MODER &= ~(0x3UL << 18);  // PC9 input
    // PC10 = KEY_RIGHT
    GPIOC->MODER &= ~(0x3UL << 20);  // PC10 input
    // PC12 = KEY_UP
    GPIOC->MODER &= ~(0x3UL << 24);  // PC12 input
    // Note: PB6 (KEY_CENTER) is now encoder EA — do NOT touch it

    // Ensure PC8 (motor CS) is configured as output HIGH
    GPIOC->MODER &= ~(0x3UL << 16);
    GPIOC->MODER |= (0x1UL << 16);   // Output mode
    GPIOC->BSRR = (1UL << 8);         // PC8 high (CS idle)

    snprintf(buf, sizeof(buf), "GPIOC MODER=%08lX (PC8 bits[17:16]=%lu)",
             (unsigned long)GPIOC->MODER,
             (unsigned long)((GPIOC->MODER >> 16) & 0x3));
    m_transport.println(buf);

    // Reinitialize motor driver now that SPI1 is dedicated to motor
    m_transport.println("Reinitializing motor driver...");
    Tasks::MotorTask_Reinit();
    m_transport.println("Motor driver reinitialized.");

    m_transport.println("LCD disabled. SPI1 now exclusively for motor.");
    m_transport.println("Motor commands (MOVE, RUN, etc.) should now work.");
  } else if (strcmp(cmd.cmd, "MOSI_TEST") == 0) {
    // Test MOSI output by toggling it and verifying with scope/meter
    char buf[128];
    m_transport.println("MOSI (PA7) toggle test...");
    m_transport.println("Connect scope to PA7 or jumper PA7->PA6 for loopback.");

    // Disable SPI1
    SPI1->CR1 &= ~SPI_CR1_SPE;

    // Save MODER
    uint32_t saved_moder = GPIOA->MODER;

    // Set PA7 as output, PA6 as input
    GPIOA->MODER &= ~((0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= (0x1UL << 14);  // PA7 output

    // Toggle MOSI and read back via MISO (loopback test if jumpered)
    m_transport.println("Toggling MOSI 5 times...");
    for (int i = 0; i < 5; i++) {
      // Set MOSI HIGH
      GPIOA->BSRR = (1UL << 7);
      for (volatile int d = 0; d < 100; d++);
      int miso_hi = (GPIOA->IDR >> 6) & 1;

      // Set MOSI LOW
      GPIOA->BSRR = (1UL << (7 + 16));
      for (volatile int d = 0; d < 100; d++);
      int miso_lo = (GPIOA->IDR >> 6) & 1;

      snprintf(buf, sizeof(buf), "  [%d] MOSI=1 -> MISO=%d, MOSI=0 -> MISO=%d",
               i, miso_hi, miso_lo);
      m_transport.println(buf);
    }

    // Verify MOSI is driving correctly by checking ODR
    GPIOA->BSRR = (1UL << 7);  // Set high
    int odr_hi = (GPIOA->ODR >> 7) & 1;
    GPIOA->BSRR = (1UL << (7 + 16));  // Set low
    int odr_lo = (GPIOA->ODR >> 7) & 1;

    snprintf(buf, sizeof(buf), "MOSI ODR check: set HIGH -> ODR=%d, set LOW -> ODR=%d",
             odr_hi, odr_lo);
    m_transport.println(buf);

    if (odr_hi == 1 && odr_lo == 0) {
      m_transport.println("MOSI output is WORKING (ODR toggles correctly).");
    } else {
      m_transport.println("ERROR: MOSI output NOT toggling!");
    }

    // Restore and re-enable SPI
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done. SPI restored.");
  } else if (strcmp(cmd.cmd, "SPI_LOOPBACK") == 0) {
    // SPI loopback test: jumper PA7 (MOSI) to PA6 (MISO)
    // Sends bytes and expects to read them back
    char buf[128];
    m_transport.println("SPI LOOPBACK TEST");
    m_transport.println("Requires: Jumper wire from PA7 (MOSI) to PA6 (MISO)");
    m_transport.println("Disconnect powerSTEP01 MISO if possible, or ignore motor.");

    // Disable SPI1
    SPI1->CR1 &= ~SPI_CR1_SPE;
    uint32_t saved_moder = GPIOA->MODER;

    // Set PA5 (SCK), PA7 (MOSI) as outputs, PA6 (MISO) as input
    GPIOA->MODER &= ~((0x3UL << 10) | (0x3UL << 12) | (0x3UL << 14));
    GPIOA->MODER |= ((0x1UL << 10) | (0x1UL << 14));

    // Mode 3 bit-bang with loopback check
    uint8_t testBytes[] = {0xAA, 0x55, 0xFF, 0x00, 0xD0};
    bool allPass = true;

    for (int t = 0; t < 5; t++) {
      uint8_t tx = testBytes[t];
      uint8_t rx = 0;

      // Clock idle HIGH (Mode 3)
      GPIOA->BSRR = (1UL << 5);
      for (volatile int d = 0; d < 20; d++);

      for (int bit = 7; bit >= 0; bit--) {
        // Falling edge - shift out MOSI
        GPIOA->BSRR = (1UL << (5 + 16));  // SCK low

        if (tx & (1 << bit)) {
          GPIOA->BSRR = (1UL << 7);  // MOSI high
        } else {
          GPIOA->BSRR = (1UL << (7 + 16));  // MOSI low
        }
        for (volatile int d = 0; d < 20; d++);

        // Rising edge - sample MISO
        GPIOA->BSRR = (1UL << 5);  // SCK high
        for (volatile int d = 0; d < 20; d++);

        if (GPIOA->IDR & (1UL << 6)) {
          rx |= (1 << bit);
        }
      }

      bool pass = (tx == rx);
      if (!pass) allPass = false;
      snprintf(buf, sizeof(buf), "  TX=0x%02X  RX=0x%02X  %s",
               tx, rx, pass ? "PASS" : "FAIL");
      m_transport.println(buf);
    }

    if (allPass) {
      m_transport.println("LOOPBACK TEST PASSED! MOSI and MISO working.");
    } else {
      m_transport.println("LOOPBACK TEST FAILED. Check jumper connection.");
    }

    // Restore
    GPIOA->MODER = saved_moder;
    SPI1->CR1 |= SPI_CR1_SPE;
    m_transport.println("Done.");
  } else {
    respondErr("Unknown command. Type HELP for list.");
  }
}

void CommandParser::respondOk(const char *msg) {
  if (m_format == ResponseFormat::JSON) {
    if (msg != nullptr && msg[0] != '\0') {
      char buf[192];
      snprintf(buf, sizeof(buf), "{\"message\":\"%s\"}", msg);
      respondJsonOk(m_currentCmd, buf);
    } else {
      respondJsonOk(m_currentCmd, nullptr);
    }
  } else {
    m_transport.print("OK ");
    m_transport.println(msg);
  }
}

void CommandParser::respondErr(const char *msg) {
  if (m_format == ResponseFormat::JSON) {
    respondJsonErr(m_currentCmd, "ERROR", msg);
  } else {
    m_transport.print("ERROR ");
    m_transport.println(msg);
  }
}

void CommandParser::respondData(const char **lines, size_t count) {
  for (size_t i = 0; i < count; i++) {
    m_transport.println(lines[i]);
  }
}

void CommandParser::respondJsonOk(const char *command, const char *dataJson) {
  char buf[1024];
  if (dataJson != nullptr && dataJson[0] != '\0') {
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"command\":\"%s\",\"data\":%s}",
             command, dataJson);
  } else {
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"command\":\"%s\",\"data\":{}}",
             command);
  }
  m_transport.println(buf);
}

void CommandParser::respondJsonErr(const char *command, const char *code, const char *message) {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"status\":\"error\",\"command\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}",
           command, code, message);
  m_transport.println(buf);
}

// ============================================================================
// Motion command handlers
// ============================================================================

void CommandParser::cmdMove(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: MOVE <steps> <dir> [speed] | MOVE <rev> REV <dir> [speed]");
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

  int32_t dir = static_cast<int32_t>(atol(cmd.args[dirArgIdx]));

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

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    int32_t spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionMove(signedSteps, speedOverride);
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
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

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this move
  uint32_t speedOverride = 0;
  if (cmd.argCount > speedArgIdx) {
    int32_t spd = static_cast<int32_t>(atol(cmd.args[speedArgIdx]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionGoTo(position, speedOverride);
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
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
    speed = static_cast<int32_t>(rpm * static_cast<double>(fullSteps) / 60.0 + 0.5);
    dirArgIdx = 2;
  } else {
    speed = static_cast<int32_t>(atol(cmd.args[0]));
    dirArgIdx = 1;
  }

  int32_t dir = static_cast<int32_t>(atol(cmd.args[dirArgIdx]));

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
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdStop(const ParsedCommand &cmd) {
  // Optional "hard" argument
  bool hard = (cmd.argCount > 0 && strcmp(cmd.args[0], "hard") == 0);

  auto r = m_dispatcher.motionStop(hard);
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdEstop() {
  m_dispatcher.safetyEstop();
  respondOk("ESTOP");
}

// ============================================================================
// Configuration command handlers
// ============================================================================

void CommandParser::cmdEnable() {
  auto r = m_dispatcher.motionEnable();
  if (r.ok) {
    respondOk("ENABLED");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdDisable() {
  auto r = m_dispatcher.motionDisable();
  if (r.ok) {
    respondOk("HI-Z");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdAccel(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: ACCEL <value>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));

  // Validate acceleration (12-bit register, raw units for legacy command)
  if (value < Limits::ACCEL_MIN || value > Limits::ACCEL_MAX) {
    respondErr("accel out of range (1-4095)");
    return;
  }

  auto r = m_dispatcher.configSetAccelRaw(static_cast<uint16_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdDecel(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DECEL <value>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));

  // Validate deceleration (12-bit register, raw units for legacy command)
  if (value < Limits::ACCEL_MIN || value > Limits::ACCEL_MAX) {
    respondErr("decel out of range (1-4095)");
    return;
  }

  auto r = m_dispatcher.configSetDecelRaw(static_cast<uint16_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdMaxSpd(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MAXSPD <value>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));

  // Validate max speed (10-bit register, raw units for legacy command)
  if (value < Limits::MAXSPD_MIN || value > Limits::MAXSPD_MAX) {
    respondErr("maxspd out of range (1-1023)");
    return;
  }

  auto r = m_dispatcher.configSetMaxSpeedRaw(static_cast<uint16_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

// ============================================================================
// Physical-unit configuration commands (SCPI: MOT:CFG:*)
// ============================================================================

void CommandParser::cmdAccelPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:ACCEL <steps/s^2>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("accel must be positive");
    return;
  }

  auto r = m_dispatcher.configSetAccelPhysical(static_cast<uint32_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdDecelPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:DECEL <steps/s^2>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("decel must be positive");
    return;
  }

  auto r = m_dispatcher.configSetDecelPhysical(static_cast<uint32_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdMaxSpdPhysical(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MOT:CFG:MAXSPD <steps/s>");
    return;
  }

  int32_t value = static_cast<int32_t>(atol(cmd.args[0]));
  if (value <= 0) {
    respondErr("maxspd must be positive");
    return;
  }

  auto r = m_dispatcher.configSetMaxSpeedPhysical(static_cast<uint32_t>(value));
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

// ============================================================================
// Synchronization command handlers
// ============================================================================

void CommandParser::cmdQueue(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: QUEUE <cmd> [args...]");
    return;
  }

  // Parse the queued command type, validate, and dispatch
  const char *subCmd = cmd.args[0];
  Comms::DispatchResult r = {false, nullptr};

  if (strcmp(subCmd, "MOVE") == 0 || strcmp(subCmd, "move") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE MOVE <steps> <dir>");
      return;
    }
    int32_t steps = static_cast<int32_t>(atol(cmd.args[1]));
    int32_t dir = static_cast<int32_t>(atol(cmd.args[2]));
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
    int32_t position = static_cast<int32_t>(atol(cmd.args[1]));
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
    int32_t speed = static_cast<int32_t>(atol(cmd.args[1]));
    int32_t dir = static_cast<int32_t>(atol(cmd.args[2]));
    if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
      respondErr("dir must be 0 or 1");
      return;
    }
    if (speed < Limits::SPEED_MIN || speed > Limits::SPEED_MAX) {
      respondErr("speed out of range (0-15625 steps/s)");
      return;
    }
    // Convert steps/s to raw register value for powerSTEP01
    uint32_t speedRaw = static_cast<uint32_t>(
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

  if (r.ok) {
    char buf[32];
    snprintf(buf, sizeof(buf), "QUEUED %u",
             static_cast<unsigned>(m_dispatcher.getQueueDepth()));
    respondOk(buf);
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdArm() {
  auto r = m_dispatcher.queueArm();
  if (r.ok) {
    respondOk("ARMED");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdStart() {
  auto r = m_dispatcher.queueStart();
  if (r.ok) {
    respondOk("RUNNING");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdStartAt(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: START_AT <tick>");
    return;
  }

  uint32_t targetTick =
      static_cast<uint32_t>(strtoul(cmd.args[0], nullptr, 10));
  auto r = m_dispatcher.queueStartAt(targetTick);
  if (r.ok) {
    respondOk("RUNNING");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdClearQueue() {
  auto r = m_dispatcher.queueClear();
  if (r.ok) {
    respondOk("CLEARED");
  } else {
    respondErr(r.message);
  }
}

// ============================================================================
// Timing/diagnostics command handlers
// ============================================================================

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
  const char* stateStr = m_dispatcher.controllerStateString();

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"rx_tick\":%lu,\"tx_tick\":%lu,\"state\":\"%s\"}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick),
             stateStr);
    respondJsonOk("PING", buf);
  } else {
    // ASCII format: PONG <seq> <mcu_rx_tick> <mcu_tx_tick> <state>
    char buf[80];
    snprintf(buf, sizeof(buf), "PONG %lu %lu %lu %s",
             static_cast<unsigned long>(seq), static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick), stateStr);
    m_transport.println(buf);
  }
}

void CommandParser::cmdGetTick() {
  uint32_t tick = m_dispatcher.getTickUs();

  if (m_format == ResponseFormat::JSON) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"tick\":%lu}", static_cast<unsigned long>(tick));
    respondJsonOk("GET_TICK", buf);
  } else {
    char buf[32];
    snprintf(buf, sizeof(buf), "OK %lu", static_cast<unsigned long>(tick));
    m_transport.println(buf);
  }
}

void CommandParser::cmdGetStatus() {
  uint32_t tick = m_dispatcher.getTickUs();
  const char* stateStr = m_dispatcher.controllerStateString();
  uint32_t queueDepth = m_dispatcher.getQueueDepth();
  const char* modeStr = m_dispatcher.controlModeString();
  const char* encStatusStr = m_dispatcher.encoderStatusString();

  // Get actual position and velocity from motor telemetry
  TelemetrySnapshot snap = g_telemetry.getSnapshot();

  if (m_format == ResponseFormat::JSON) {
    // Parse status register for direction and error flags
    uint16_t sr = snap.motor.statusReg;
    int direction    = (sr & (1 << 4)) ? 1 : 0;        // bit 4: 1=FWD
    unsigned motStat = (sr >> 5) & 0x3;                 // bits 5-6
    bool cmdErr      = (sr & (1 << 7)) != 0;            // bit 7
    bool uvlo        = !(sr & (1 << 9));                // bit 9 (active low)
    // Bits 11-12: TH_STATUS 2-bit field
    // 00=Normal, 01=Warning, 10=Bridge shutdown, 11=Device shutdown
    uint8_t thStatus = (sr >> 11) & 0x3;
    bool thermalWarn = (thStatus >= 1);
    bool thermalSD   = (thStatus >= 2);
    bool ocd         = !(sr & (1 << 13));               // bit 13 (active low)
    bool stallA      = !(sr & (1 << 14));               // bit 14 (active low)
    bool stallB      = !(sr & (1 << 15));               // bit 15 (active low)

    // Filter faults against ALARM_EN config — only report faults the user
    // has enabled. STATUS register always reflects raw hardware state, but
    // disabled faults (e.g. OCD noise) should not cause GUI error state.
    bool feOcd, feThermalSD, feThermalWarn, feUvlo, feStallA, feStallB, feCmdErr;
    m_dispatcher.getFaultEnable(feOcd, feThermalSD, feThermalWarn,
                                feUvlo, feStallA, feStallB, feCmdErr);
    bool ocdEn       = ocd && feOcd;
    bool thermalSDEn = thermalSD && feThermalSD;
    bool thermalWarnEn = thermalWarn && feThermalWarn;
    bool uvloEn      = uvlo && feUvlo;
    bool stallAEn    = stallA && feStallA && snap.motor.speed > 0;
    bool stallBEn    = stallB && feStallB && snap.motor.speed > 0;
    bool cmdErrEn    = cmdErr && feCmdErr;
    bool anyError    = cmdErrEn || uvloEn || thermalSDEn || thermalWarnEn
                       || stallAEn || stallBEn || ocdEn;

    // Get heartbeat watchdog status
    bool hbEnabled; uint32_t hbTimeout, hbLastSeq, hbRemaining; bool hbTimedOut;
    m_dispatcher.safetyGetHeartbeatStatus(hbEnabled, hbTimeout, hbLastSeq,
                                           hbRemaining, hbTimedOut);

    // Bypass respondJsonOk (256-byte buffer too small) — format full envelope
    // newlib-nano: no %lld — pre-format int64_t as string
    char encCountStr[24];
    i64toa(snap.encoder.count, encCountStr, sizeof(encCountStr));

    char buf[1024];
    snprintf(buf, sizeof(buf),
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
             stateStr,
             static_cast<unsigned long>(tick),
             static_cast<unsigned>(queueDepth),
             static_cast<long>(snap.motor.position),
             static_cast<unsigned long>(snap.motor.speed),
             snap.motor.busy ? "true" : "false",
             snap.motor.hiZ ? "true" : "false",
             direction, motStat, static_cast<unsigned>(sr),
             cmdErrEn ? "true" : "false",
             ocdEn ? "true" : "false",
             thermalSDEn ? "true" : "false",
             thermalWarnEn ? "true" : "false",
             uvloEn ? "true" : "false",
             stallAEn ? "true" : "false",
             stallBEn ? "true" : "false",
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
             static_cast<long>(snap.control.baseSpeedRaw),
             static_cast<long>(snap.control.trimSpeedRaw),
             static_cast<long>(snap.control.finalSpeedRaw),
             static_cast<unsigned>(snap.control.trimFrozen),
             static_cast<unsigned>(snap.control.velQuality),
             hbEnabled ? "true" : "false",
             static_cast<unsigned long>(hbTimeout),
             static_cast<unsigned long>(hbRemaining),
             hbTimedOut ? "true" : "false",
             modeStr,
             encStatusStr,
             anyError ? "true" : "false");
    m_transport.println(buf);
  } else {
    // ASCII format: STATUS <state> <tick> <queue_depth> <mode> <encoder_status> <position> <velocity>
    char buf[96];
    snprintf(buf, sizeof(buf), "STATUS %s %lu %u %s %s %ld %lu",
             stateStr, static_cast<unsigned long>(tick),
             static_cast<unsigned>(queueDepth), modeStr,
             encStatusStr,
             static_cast<long>(snap.motor.position),
             static_cast<unsigned long>(snap.motor.speed));
    m_transport.println(buf);
  }
}

void CommandParser::cmdClearFault() {
  char faultBuf[80] = {};
  auto r = m_dispatcher.safetyClearFault(faultBuf, sizeof(faultBuf));
  if (r.ok) {
    respondOk("IDLE");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdForceClearFault() {
  auto r = m_dispatcher.safetyForceClearFault();
  if (r.ok) {
    respondOk("IDLE");
  } else {
    respondErr(r.message);
  }
}

// ============================================================================
// Heartbeat command handlers
// ============================================================================

void CommandParser::cmdHeartbeat(const ParsedCommand &cmd) {
  uint32_t seq = 0;
  if (cmd.argCount >= 1) {
    seq = static_cast<uint32_t>(atol(cmd.args[0]));
  }

  m_dispatcher.safetyHeartbeatReceived(seq);

  bool hbEnabled; uint32_t hbTimeout, hbLastSeq, hbRemaining; bool hbTimedOut;
  m_dispatcher.safetyGetHeartbeatStatus(hbEnabled, hbTimeout, hbLastSeq, hbRemaining, hbTimedOut);

  uint32_t mcu_tick = m_dispatcher.getTickUs();

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"mcu_tick\":%lu,\"remaining_ms\":%lu}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(mcu_tick),
             static_cast<unsigned long>(hbRemaining));
    respondJsonOk("HEARTBEAT", buf);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "HEARTBEAT_ACK %lu %lu %lu",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(mcu_tick),
             static_cast<unsigned long>(hbRemaining));
    m_transport.println(buf);
  }
}

void CommandParser::cmdSetHeartbeat(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_HEARTBEAT <timeout_ms>");
    return;
  }

  uint32_t requested = static_cast<uint32_t>(atol(cmd.args[0]));
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
  bool hbEnabled; uint32_t hbTimeout, hbLastSeq, hbRemaining; bool hbTimedOut;
  m_dispatcher.safetyGetHeartbeatStatus(hbEnabled, hbTimeout, hbLastSeq, hbRemaining, hbTimedOut);

  if (m_format == ResponseFormat::JSON) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"enabled\":%s,\"timeout_ms\":%lu,\"last_seq\":%lu,"
             "\"remaining_ms\":%lu,\"timed_out\":%s}",
             hbEnabled ? "true" : "false",
             static_cast<unsigned long>(hbTimeout),
             static_cast<unsigned long>(hbLastSeq),
             static_cast<unsigned long>(hbRemaining),
             hbTimedOut ? "true" : "false");
    respondJsonOk("GET_HEARTBEAT_STATUS", buf);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), "HEARTBEAT_STATUS %s %lu %lu %lu %s",
             hbEnabled ? "ENABLED" : "DISABLED",
             static_cast<unsigned long>(hbTimeout),
             static_cast<unsigned long>(hbLastSeq),
             static_cast<unsigned long>(hbRemaining),
             hbTimedOut ? "TIMED_OUT" : "OK");
    m_transport.println(buf);
  }
}

// ============================================================================
// Utility command handlers
// ============================================================================

void CommandParser::cmdHelp() {
  m_transport.println("=== Stepper Motor Controller ===");
  m_transport.println("Motion:");
  m_transport.println("  MOVE <steps> <dir>  - Relative move");
  m_transport.println("  GOTO <position>     - Absolute move");
  m_transport.println("  RUN <speed> <dir>   - Continuous run");
  m_transport.println("  STOP [hard]         - Stop motion");
  m_transport.println("  ESTOP               - Emergency stop");
  m_transport.println("Sync:");
  m_transport.println("  QUEUE <cmd> [args]  - Queue command");
  m_transport.println("  ARM                 - Prepare for start");
  m_transport.println("  START               - Begin execution");
  m_transport.println("  PING <seq>          - Latency test");
  m_transport.println("  GET_TICK            - Query tick");
  m_transport.println("  GET_STATUS          - Query status");
  m_transport.println("Heartbeat:");
  m_transport.println("  HEARTBEAT <seq>       - Reset watchdog");
  m_transport.println("  SET_HEARTBEAT <ms>    - Set timeout (0=off)");
  m_transport.println("  GET_HEARTBEAT_STATUS  - Query watchdog");
  m_transport.println("Config:");
  m_transport.println("  ENABLE/DISABLE      - Motor outputs");
  m_transport.println("  ACCEL/DECEL/MAXSPD  - Parameters");
  m_transport.println("Device:");
  m_transport.println("  GET_DEVICE_ID       - Query device ID");
  m_transport.println("  SET_DEVICE_ID <id>  - Set device ID");
  m_transport.println("  SET_ROLE <role>     - FL/FR/RL/RR/NONE");
  m_transport.println("Mode:");
  m_transport.println("  GET_MODE            - Query control mode");
  m_transport.println("  SET_MODE <mode>     - OPEN_LOOP/CLOSED_LOOP");
  m_transport.println("  GET_ENCODER_STATUS  - Encoder availability");
  m_transport.println("Format:");
  m_transport.println("  SET_FORMAT <fmt>    - ASCII or JSON");
  m_transport.println("  GET_FORMAT          - Query response format");
  m_transport.println("  SET_BAUD <rate>     - Change baud (auto-reverts in 2s)");
  m_transport.println("UI/Display:");
  m_transport.println("  UI_MODE [LOCAL|REMOTE] - Get/set UI mode");
  m_transport.println("  DISP_CLEAR [color]  - Clear display");
  m_transport.println("  DISP_TEXT x y fg bg text");
  m_transport.println("  DISP_RECT x y w h color [fill]");
  m_transport.println("  DISP_LINE x1 y1 x2 y2 color");
  m_transport.println("  DISP_BITMAP x y w h   - Binary RGB565 stream");
  m_transport.println("  DISP_BITMAP_B64 x y w h b64data");
  m_transport.println("Flash Image:");
  m_transport.println("  FLASH_INFO          - Flash capacity and slot count");
  m_transport.println("  FLASH_UPLOAD <slot> - Binary upload to flash slot");
  m_transport.println("  FLASH_SHOW <slot>   - Display image from flash");
  m_transport.println("  FLASH_ERASE_ALL     - Erase all image slots");
  m_transport.println("Utility:");
  m_transport.println("  ENCODER             - Encoder data");
  m_transport.println("  ENC_DEBUG           - Encoder HW registers");
  m_transport.println("  VER                 - Version");
  m_transport.println("  HELP                - This help");
}

void CommandParser::cmdVersion() {
  m_transport.println("Stepper Motor Controller v0.3.0");
  m_transport.println("Build: " __DATE__ " " __TIME__);
  m_transport.println("Protocol: ARM/START sync v1");

  // Show device identification
  char buf[48];
  uint16_t deviceId; const char* roleStr;
  m_dispatcher.getDeviceInfo(deviceId, roleStr);
  snprintf(buf, sizeof(buf), "Device: %u (%s)", static_cast<unsigned>(deviceId),
           roleStr);
  m_transport.println(buf);

  // Show control mode
  snprintf(buf, sizeof(buf), "Mode: %s (encoder: %s)",
           m_dispatcher.controlModeString(),
           m_dispatcher.encoderStatusString());
  m_transport.println(buf);
}

void CommandParser::cmdHome(const ParsedCommand &cmd) {
  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this move
  uint32_t speedOverride = 0;
  if (cmd.argCount >= 1) {
    int32_t spd = static_cast<int32_t>(atol(cmd.args[0]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto r = m_dispatcher.motionHome(speedOverride);
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdZero() {
  auto r = m_dispatcher.motionZero();
  if (r.ok) {
    respondOk("");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdEncoderZero() {
  m_dispatcher.encoderResetCount();
  respondOk("ENCODER_ZEROED");
}

void CommandParser::cmdZeroAll() {
  auto r = m_dispatcher.motionZero();
  m_dispatcher.encoderResetCount();
  if (r.ok) {
    respondOk("ALL_ZEROED");
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdEncoder() {
  // Check if encoder is available
  if (!Tasks::EncoderTask_IsAvailable()) {
    respondErr("Encoder not available");
    return;
  }

  // Get encoder state
  Tasks::EncoderState state = Tasks::EncoderTask_GetState();

  // newlib-nano: no %lld — pre-format int64_t as string
  char countStr[24];
  i64toa(state.count, countStr, sizeof(countStr));

  if (m_format == ResponseFormat::JSON) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"count\":%s,\"velocity\":%ld,\"index_seen\":%s,\"index_tick\":%lu,"
             "\"revolutions\":%ld,\"index_period_us\":%lu}",
             countStr,
             static_cast<long>(state.velocity),
             state.indexSeen ? "true" : "false",
             static_cast<unsigned long>(state.indexTick),
             static_cast<long>(state.revolutions),
             static_cast<unsigned long>(state.indexPeriodUs));
    respondJsonOk("ENC", buf);
  } else {
    // ASCII format: OK count=<n> vel=<n> idx=<0|1> idx_tick=<n> rev=<n>
    char buf[96];
    snprintf(buf, sizeof(buf), "count=%s vel=%ld idx=%d idx_tick=%lu rev=%ld",
             countStr,
             static_cast<long>(state.velocity),
             state.indexSeen ? 1 : 0,
             static_cast<unsigned long>(state.indexTick),
             static_cast<long>(state.revolutions));
    respondOk(buf);
  }
}

void CommandParser::cmdEncDebug() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "TIM4: CNT=%04X CR1=%04X SMCR=%04X CCMR1=%04X CCER=%04X ARR=%04X "
           "PB: MODER[12:15]=%X AFR0[24:31]=%02X IDR[6:7]=%X "
           "PC: ODR[2:3]=%X IDR[4]=%X",
           static_cast<unsigned>(TIM4->CNT & 0xFFFF),
           static_cast<unsigned>(TIM4->CR1),
           static_cast<unsigned>(TIM4->SMCR),
           static_cast<unsigned>(TIM4->CCMR1),
           static_cast<unsigned>(TIM4->CCER),
           static_cast<unsigned>(TIM4->ARR & 0xFFFF),
           static_cast<unsigned>((GPIOB->MODER >> 12) & 0xF),
           static_cast<unsigned>((GPIOB->AFR[0] >> 24) & 0xFF),
           static_cast<unsigned>((GPIOB->IDR >> 6) & 0x3),
           static_cast<unsigned>((GPIOC->ODR >> 2) & 0x3),
           static_cast<unsigned>((GPIOC->IDR >> 4) & 0x1));
  m_transport.println(buf);
}

void CommandParser::cmdEncFilter(const ParsedCommand &cmd) {
  // Query mode: no arguments → return full filter config
  if (cmd.argCount < 1) {
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    char buf[512];
    unsigned pGain = (cfg.padeGainPct > 0) ? cfg.padeGainPct : 50;
    unsigned pMax  = (cfg.padeMaxCorr > 0) ? cfg.padeMaxCorr : 50;
    unsigned bqCut = (cfg.biquadCutoffHz > 0) ? cfg.biquadCutoffHz : 10;
    unsigned ntCtr = (cfg.notchCenterHz > 0) ? cfg.notchCenterHz : 25;
    unsigned ntQ   = (cfg.notchQ10 > 0) ? cfg.notchQ10 : 50;
    unsigned hAlph = (cfg.holtAlpha > 0) ? cfg.holtAlpha : 51;
    unsigned hBeta = (cfg.holtBeta > 0) ? cfg.holtBeta : 13;
    if (m_format == ResponseFormat::JSON) {
      snprintf(buf, sizeof(buf),
        "{\"meas_window_ms\":%u,\"sample_rate_hz\":%u,"
        "\"ema_enabled\":%s,\"ema_alpha\":%u,"
        "\"sma_enabled\":%s,\"sma_window\":%u,"
        "\"pade_enabled\":%s,\"pade_gain\":%u,\"pade_max_corr\":%u,"
        "\"biquad_enabled\":%s,\"biquad_cutoff_hz\":%u,"
        "\"notch_enabled\":%s,\"notch_center_hz\":%u,\"notch_q10\":%u,"
        "\"holt_enabled\":%s,\"holt_alpha\":%u,\"holt_beta\":%u}",
        (unsigned)cfg.measWindowMs,
        (unsigned)cfg.sampleRateHz,
        (cfg.filterFlags & Tasks::ENC_FILT_EMA) ? "true" : "false",
        (unsigned)cfg.emaAlpha,
        (cfg.filterFlags & Tasks::ENC_FILT_SMA) ? "true" : "false",
        (unsigned)cfg.smaWindow,
        (cfg.filterFlags & Tasks::ENC_FILT_PADE) ? "true" : "false",
        pGain, pMax,
        (cfg.filterFlags & Tasks::ENC_FILT_BIQUAD) ? "true" : "false",
        bqCut,
        (cfg.filterFlags & Tasks::ENC_FILT_NOTCH) ? "true" : "false",
        ntCtr, ntQ,
        (cfg.filterFlags & Tasks::ENC_FILT_HOLT) ? "true" : "false",
        hAlph, hBeta);
      respondJsonOk(m_currentCmd, buf);
    } else {
      snprintf(buf, sizeof(buf),
        "window=%ums rate=%uHz EMA=%s alpha=%u SMA=%s window=%u"
        " PADE=%s gain=%u%% max=%utps BIQUAD=%s cut=%uHz"
        " NOTCH=%s ctr=%uHz Q=%u.%u HOLT=%s a=%u b=%u",
        (unsigned)cfg.measWindowMs,
        (unsigned)cfg.sampleRateHz,
        (cfg.filterFlags & Tasks::ENC_FILT_EMA) ? "ON" : "OFF",
        (unsigned)cfg.emaAlpha,
        (cfg.filterFlags & Tasks::ENC_FILT_SMA) ? "ON" : "OFF",
        (unsigned)cfg.smaWindow,
        (cfg.filterFlags & Tasks::ENC_FILT_PADE) ? "ON" : "OFF",
        pGain, pMax,
        (cfg.filterFlags & Tasks::ENC_FILT_BIQUAD) ? "ON" : "OFF",
        bqCut,
        (cfg.filterFlags & Tasks::ENC_FILT_NOTCH) ? "ON" : "OFF",
        ntCtr, ntQ / 10, ntQ % 10,
        (cfg.filterFlags & Tasks::ENC_FILT_HOLT) ? "ON" : "OFF",
        hAlph, hBeta);
      respondOk(buf);
    }
    return;
  }

  // Legacy set commands: NONE / EMA <alpha> / SMA <window>
  if (strcmp(cmd.args[0], "NONE") == 0) {
    Tasks::EncoderTask_SetFilter(0, 0);
    m_dispatcher.configSetEncFilter(0, 0);
    respondOk("NONE");
    return;
  }

  if (strcmp(cmd.args[0], "EMA") == 0) {
    if (cmd.argCount < 2) {
      respondErr("EMA requires alpha (0-255)");
      return;
    }
    long val = strtol(cmd.args[1], nullptr, 10);
    if (val < 0 || val > 255) {
      respondErr("EMA alpha must be 0-255");
      return;
    }
    Tasks::EncoderTask_SetFilter(1, static_cast<uint8_t>(val));
    m_dispatcher.configSetEncFilter(Tasks::ENC_FILT_EMA, static_cast<uint8_t>(val));
    char buf[48];
    snprintf(buf, sizeof(buf), "EMA alpha=%u", (unsigned)val);
    respondOk(buf);
    return;
  }

  if (strcmp(cmd.args[0], "SMA") == 0) {
    if (cmd.argCount < 2) {
      respondErr("SMA requires window size (2-32)");
      return;
    }
    long val = strtol(cmd.args[1], nullptr, 10);
    if (val < 2 || val > 32) {
      respondErr("SMA window must be 2-32");
      return;
    }
    Tasks::EncoderTask_SetFilter(2, static_cast<uint8_t>(val));
    m_dispatcher.configSetEncFilter(Tasks::ENC_FILT_SMA, 0);
    m_dispatcher.configSetEncSmaWindow(static_cast<uint8_t>(val));
    char buf[48];
    snprintf(buf, sizeof(buf), "SMA window=%u", (unsigned)val);
    respondOk(buf);
    return;
  }

  // Legacy: bare number treated as EMA alpha
  long val = strtol(cmd.args[0], nullptr, 10);
  if (val < 0 || val > 255) {
    respondErr("Unknown filter type or alpha out of range (0-255)");
    return;
  }
  Tasks::EncoderTask_SetFilter(1, static_cast<uint8_t>(val));
  char buf[48];
  snprintf(buf, sizeof(buf), "EMA alpha=%u", (unsigned)val);
  respondOk(buf);
}

// CTRL:ENC:FILT:* sub-commands
void CommandParser::cmdEncFilterSub(const char *sub, const ParsedCommand &cmd) {
  // CTRL:ENC:FILT:WINDOW <ms>
  if (strcmp(sub, "WINDOW") == 0) {
    if (cmd.argCount < 1) {
      respondErr("WINDOW requires ms (1-255)");
      return;
    }
    long val = strtol(cmd.args[0], nullptr, 10);
    if (val < 1 || val > 255) {
      respondErr("Window must be 1-255 ms");
      return;
    }
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    cfg.measWindowMs = static_cast<uint8_t>(val);
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[32];
    snprintf(buf, sizeof(buf), "window=%ums", (unsigned)val);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:EMA <0|1> [alpha]
  if (strcmp(sub, "EMA") == 0) {
    if (cmd.argCount < 1) {
      respondErr("EMA requires 0|1 [alpha]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_EMA;
      if (cmd.argCount >= 2) {
        long alpha = strtol(cmd.args[1], nullptr, 10);
        if (alpha < 0 || alpha > 255) {
          respondErr("EMA alpha must be 0-255");
          return;
        }
        cfg.emaAlpha = static_cast<uint8_t>(alpha);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_EMA;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[48];
    snprintf(buf, sizeof(buf), "EMA %s alpha=%u",
             (cfg.filterFlags & Tasks::ENC_FILT_EMA) ? "ON" : "OFF",
             (unsigned)cfg.emaAlpha);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:SMA <0|1> [window]
  if (strcmp(sub, "SMA") == 0) {
    if (cmd.argCount < 1) {
      respondErr("SMA requires 0|1 [window]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_SMA;
      if (cmd.argCount >= 2) {
        long win = strtol(cmd.args[1], nullptr, 10);
        if (win < 0 || win > 32) {
          respondErr("SMA window must be 0-32");
          return;
        }
        cfg.smaWindow = static_cast<uint8_t>(win);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_SMA;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[48];
    snprintf(buf, sizeof(buf), "SMA %s window=%u",
             (cfg.filterFlags & Tasks::ENC_FILT_SMA) ? "ON" : "OFF",
             (unsigned)cfg.smaWindow);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:PADE <0|1> [gain%] [maxCorr]
  if (strcmp(sub, "PADE") == 0) {
    if (cmd.argCount < 1) {
      respondErr("PADE requires 0|1 [gain% 1-100] [maxCorr 1-255]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_PADE;
      if (cmd.argCount >= 2) {
        long gain = strtol(cmd.args[1], nullptr, 10);
        if (gain < 1 || gain > 100) {
          respondErr("Gain must be 1-100%%");
          return;
        }
        cfg.padeGainPct = static_cast<uint8_t>(gain);
      }
      if (cmd.argCount >= 3) {
        long maxC = strtol(cmd.args[2], nullptr, 10);
        if (maxC < 1 || maxC > 255) {
          respondErr("MaxCorr must be 1-255 tps");
          return;
        }
        cfg.padeMaxCorr = static_cast<uint8_t>(maxC);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_PADE;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "PADE %s gain=%u%% max=%utps",
             (cfg.filterFlags & Tasks::ENC_FILT_PADE) ? "ON" : "OFF",
             (unsigned)((cfg.padeGainPct > 0) ? cfg.padeGainPct : 50),
             (unsigned)((cfg.padeMaxCorr > 0) ? cfg.padeMaxCorr : 50));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:BIQUAD <0|1> [cutoffHz]
  if (strcmp(sub, "BIQUAD") == 0) {
    if (cmd.argCount < 1) {
      respondErr("BIQUAD requires 0|1 [cutoff 1-50 Hz]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_BIQUAD;
      if (cmd.argCount >= 2) {
        long cutoff = strtol(cmd.args[1], nullptr, 10);
        if (cutoff < 1 || cutoff > 50) {
          respondErr("Cutoff must be 1-50 Hz");
          return;
        }
        cfg.biquadCutoffHz = static_cast<uint8_t>(cutoff);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_BIQUAD;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "BIQUAD %s cutoff=%uHz",
             (cfg.filterFlags & Tasks::ENC_FILT_BIQUAD) ? "ON" : "OFF",
             (unsigned)((cfg.biquadCutoffHz > 0) ? cfg.biquadCutoffHz : 10));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:NOTCH <0|1> [centerHz] [Q×10]
  if (strcmp(sub, "NOTCH") == 0) {
    if (cmd.argCount < 1) {
      respondErr("NOTCH requires 0|1 [center 1-50 Hz] [Q*10 1-100]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_NOTCH;
      if (cmd.argCount >= 2) {
        long center = strtol(cmd.args[1], nullptr, 10);
        if (center < 1 || center > 50) {
          respondErr("Center must be 1-50 Hz");
          return;
        }
        cfg.notchCenterHz = static_cast<uint8_t>(center);
      }
      if (cmd.argCount >= 3) {
        long q10 = strtol(cmd.args[2], nullptr, 10);
        if (q10 < 1 || q10 > 100) {
          respondErr("Q*10 must be 1-100");
          return;
        }
        cfg.notchQ10 = static_cast<uint8_t>(q10);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_NOTCH;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[64];
    unsigned ctr = (cfg.notchCenterHz > 0) ? cfg.notchCenterHz : 25;
    unsigned q = (cfg.notchQ10 > 0) ? cfg.notchQ10 : 50;
    snprintf(buf, sizeof(buf), "NOTCH %s center=%uHz Q=%u.%u",
             (cfg.filterFlags & Tasks::ENC_FILT_NOTCH) ? "ON" : "OFF",
             ctr, q / 10, q % 10);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:HOLT <0|1> [alpha] [beta]
  if (strcmp(sub, "HOLT") == 0) {
    if (cmd.argCount < 1) {
      respondErr("HOLT requires 0|1 [alpha 0-255] [beta 0-255]");
      return;
    }
    long enable = strtol(cmd.args[0], nullptr, 10);
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    if (enable) {
      cfg.filterFlags |= Tasks::ENC_FILT_HOLT;
      if (cmd.argCount >= 2) {
        long alpha = strtol(cmd.args[1], nullptr, 10);
        if (alpha < 0 || alpha > 255) {
          respondErr("Alpha must be 0-255");
          return;
        }
        cfg.holtAlpha = static_cast<uint8_t>(alpha);
      }
      if (cmd.argCount >= 3) {
        long beta = strtol(cmd.args[2], nullptr, 10);
        if (beta < 0 || beta > 255) {
          respondErr("Beta must be 0-255");
          return;
        }
        cfg.holtBeta = static_cast<uint8_t>(beta);
      }
    } else {
      cfg.filterFlags &= ~Tasks::ENC_FILT_HOLT;
    }
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[64];
    snprintf(buf, sizeof(buf), "HOLT %s alpha=%u beta=%u",
             (cfg.filterFlags & Tasks::ENC_FILT_HOLT) ? "ON" : "OFF",
             (unsigned)((cfg.holtAlpha > 0) ? cfg.holtAlpha : 51),
             (unsigned)((cfg.holtBeta > 0) ? cfg.holtBeta : 13));
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:RATE <hz>
  if (strcmp(sub, "RATE") == 0) {
    if (cmd.argCount < 1) {
      respondErr("RATE requires hz (100-10000)");
      return;
    }
    long val = strtol(cmd.args[0], nullptr, 10);
    if (val < 100 || val > 10000) {
      respondErr("Rate must be 100-10000 Hz");
      return;
    }
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    cfg.sampleRateHz = static_cast<uint16_t>(val);
    Tasks::EncoderTask_SetFilterConfig(cfg);
    char buf[32];
    snprintf(buf, sizeof(buf), "rate=%uHz", (unsigned)val);
    respondOk(buf);
    return;
  }

  // CTRL:ENC:FILT:SAVE
  if (strcmp(sub, "SAVE") == 0) {
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    uint8_t rateDiv = (cfg.sampleRateHz != 1000)
                    ? static_cast<uint8_t>(cfg.sampleRateHz / 100) : 0;
    m_dispatcher.configSetEncFilterFull(
        cfg.filterFlags, cfg.emaAlpha, cfg.smaWindow,
        cfg.measWindowMs, rateDiv);
    if (m_dispatcher.configSaveToFlash()) {
      respondOk("Filter config saved");
    } else {
      respondErr("Flash write failed");
    }
    return;
  }

  // CTRL:ENC:FILT:RESET
  if (strcmp(sub, "RESET") == 0) {
    Tasks::EncoderFilterConfig cfg = {};
    cfg.filterFlags = Tasks::ENC_FILT_EMA | Tasks::ENC_FILT_SMA
                    | Tasks::ENC_FILT_PADE | Tasks::ENC_FILT_BIQUAD;
    cfg.emaAlpha = 200;
    cfg.smaWindow = 8;
    cfg.measWindowMs = 40;
    cfg.sampleRateHz = 1000;
    cfg.padeGainPct = 25;
    cfg.padeMaxCorr = 50;
    cfg.biquadCutoffHz = 5;
    cfg.notchCenterHz = 0;
    cfg.notchQ10 = 0;
    cfg.holtAlpha = 0;
    cfg.holtBeta = 0;
    Tasks::EncoderTask_SetFilterConfig(cfg);
    respondOk("Filter reset to defaults");
    return;
  }

  respondErr("Unknown ENC:FILT sub-command");
}

// ============================================================================
// Device identification command handlers
// ============================================================================

void CommandParser::cmdGetDeviceId() {
  uint16_t deviceId; const char* roleStr;
  m_dispatcher.getDeviceInfo(deviceId, roleStr);

  char buf[32];
  snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(deviceId), roleStr);
  respondOk(buf);
}

void CommandParser::cmdSetDeviceId(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_DEVICE_ID <id>");
    return;
  }

  uint16_t deviceId = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));

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

  auto r = m_dispatcher.setRole(cmd.args[0]);
  if (r.ok) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Role=%s", r.message);
    respondOk(buf);
  } else {
    respondErr(r.message);
  }
}

// ============================================================================
// Control mode command handlers
// ============================================================================

void CommandParser::cmdGetMode() {
  char buf[48];
  snprintf(buf, sizeof(buf), "%s encoder=%s",
           m_dispatcher.controlModeString(),
           m_dispatcher.encoderStatusString());
  respondOk(buf);
}

void CommandParser::cmdSetMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: CTRL:MODE OPEN_LOOP|MONITOR|PID");
    return;
  }

  auto r = m_dispatcher.setControlMode(cmd.args[0]);
  if (r.ok) {
    respondOk(r.message);
  } else {
    respondErr(r.message);
  }
}

void CommandParser::cmdGetEncoderStatus() {
  const char* encStr = m_dispatcher.encoderStatusString();

  char buf[80];
  if (m_dispatcher.isEncoderAvailable()) {
    int32_t count, velocity; bool indexSeen;
    m_dispatcher.getEncoderState(count, velocity, indexSeen);
    snprintf(buf, sizeof(buf), "status=%s count=%ld vel=%ld idx=%d",
             encStr,
             static_cast<long>(count), static_cast<long>(velocity),
             indexSeen ? 1 : 0);
  } else {
    snprintf(buf, sizeof(buf), "status=%s", encStr);
  }
  respondOk(buf);
}

// ============================================================================
// Response format command handlers
// ============================================================================

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
  } else if (strcmp(formatStr, "ASCII") == 0 || strcmp(formatStr, "ascii") == 0) {
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

// ============================================================================
// Baud rate negotiation
// ============================================================================

void CommandParser::cmdSetBaud(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_BAUD <115200|230400|460800|921600>");
    return;
  }

  uint32_t rate = static_cast<uint32_t>(strtoul(cmd.args[0], nullptr, 10));

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
  m_baudRevertDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
}

void CommandParser::checkBaudRevert() {
  if (m_baudRevertRate == 0) return;

  TickType_t now = xTaskGetTickCount();
  if ((int32_t)(now - m_baudRevertDeadline) >= 0) {
    // Timeout expired — revert to safe baud rate
    m_transport.flush();
    m_transport.setBaudRate(m_baudRevertRate);
    m_baudRevertRate = 0;
  }
}

// ============================================================================
// UI mode command handlers
// ============================================================================

void CommandParser::cmdUIMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    // No argument - return current mode
    cmdUIGetMode();
    return;
  }

  // Set mode
  const char *modeStr = cmd.args[0];
  UI::UIMode mode;

  if (strcmp(modeStr, "LOCAL") == 0 || strcmp(modeStr, "local") == 0) {
    mode = UI::UIMode::LOCAL;
  } else if (strcmp(modeStr, "REMOTE") == 0 || strcmp(modeStr, "remote") == 0) {
    mode = UI::UIMode::REMOTE;
  } else {
    respondErr("Usage: UI_MODE [LOCAL|REMOTE]");
    return;
  }

  if (UI::g_uiMode.setMode(mode)) {
    respondOk(UI::UIModeManager::modeName(mode));
  } else {
    respondErr("Mode change failed");
  }
}

void CommandParser::cmdUIGetMode() {
  UI::UIMode mode = UI::g_uiMode.getMode();
  respondOk(UI::UIModeManager::modeName(mode));
}

// ============================================================================
// Display command handlers (remote rendering)
// ============================================================================

void CommandParser::cmdDispClear(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Parse optional color (default black = 0x0000)
  uint16_t color = 0x0000;
  if (cmd.argCount >= 1) {
    color = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 16));
  }

  Tasks::DisplayTask_RemoteClear(color);
  respondOk("");
}

void CommandParser::cmdDispText(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_TEXT <x> <y> <fg> <bg> <text>
  if (cmd.argCount < 5) {
    respondErr("Usage: DISP_TEXT <x> <y> <fg> <bg> <text>");
    return;
  }

  uint16_t x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t fg = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 16));
  uint16_t bg = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 16));
  const char *text = cmd.args[4];

  Tasks::DisplayTask_RemoteText(x, y, text, fg, bg);
  respondOk("");
}

void CommandParser::cmdDispRect(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_RECT <x> <y> <w> <h> <color> [fill]
  if (cmd.argCount < 5) {
    respondErr("Usage: DISP_RECT <x> <y> <w> <h> <color> [fill]");
    return;
  }

  uint16_t x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  uint16_t h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  uint16_t color = static_cast<uint16_t>(strtoul(cmd.args[4], nullptr, 16));
  bool filled = (cmd.argCount > 5 && strcmp(cmd.args[5], "fill") == 0);

  Tasks::DisplayTask_RemoteRect(x, y, w, h, color, filled);
  respondOk("");
}

void CommandParser::cmdDispLine(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_LINE <x1> <y1> <x2> <y2> <color>
  if (cmd.argCount < 5) {
    respondErr("Usage: DISP_LINE <x1> <y1> <x2> <y2> <color>");
    return;
  }

  int16_t x0 = static_cast<int16_t>(strtol(cmd.args[0], nullptr, 10));
  int16_t y0 = static_cast<int16_t>(strtol(cmd.args[1], nullptr, 10));
  int16_t x1 = static_cast<int16_t>(strtol(cmd.args[2], nullptr, 10));
  int16_t y1 = static_cast<int16_t>(strtol(cmd.args[3], nullptr, 10));
  uint16_t color = static_cast<uint16_t>(strtoul(cmd.args[4], nullptr, 16));

  Tasks::DisplayTask_RemoteLine(x0, y0, x1, y1, color);
  respondOk("");
}

void CommandParser::cmdDispBitmap(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_BITMAP <x> <y> <w> <h> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 5 : 4;

  if (cmd.argCount < minArgs) {
    respondErr("Usage: DISP_BITMAP <x> <y> <w> <h> [CRC]");
    return;
  }

  uint16_t x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  uint16_t h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));

  // Validate dimensions
  if (w == 0 || h == 0) {
    respondErr("Invalid dimensions");
    return;
  }
  if (x >= 240 || y >= 320) {
    respondErr("Position out of bounds");
    return;
  }

  // Calculate expected bytes (RGB565 = 2 bytes per pixel)
  uint32_t expectedBytes = static_cast<uint32_t>(w) * h * 2;

  constexpr uint32_t MAX_BITMAP_BYTES = 240 * 320 * 2;
  if (expectedBytes > MAX_BITMAP_BYTES) {
    respondErr("Bitmap too large");
    return;
  }

  // Start LCD streaming
  if (!Tasks::DisplayTask_StreamBitmapStart(x, y, w, h)) {
    respondErr("LCD streaming failed");
    return;
  }

  // Send ready response with expected byte count
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu", static_cast<unsigned long>(expectedBytes));
  m_transport.println(buf);
  m_transport.flush();

  // Drain trailing \r/\n from command line before binary read
  { uint8_t drain;
    while (m_transport.available() && m_transport.readByte(drain, 1)) {
      if (drain != '\r' && drain != '\n') break;
    }
  }

  // Receive binary data with timeout
  constexpr uint32_t CHUNK_SIZE = 64;
  constexpr uint32_t BYTE_TIMEOUT_MS = 100;
  uint8_t chunk[CHUNK_SIZE];
  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < expectedBytes) {
    uint32_t remaining = expectedBytes - bytesReceived;
    uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

    uint32_t chunkReceived = 0;
    while (chunkReceived < toRead) {
      uint8_t byte;
      if (m_transport.readByte(byte, BYTE_TIMEOUT_MS)) {
        chunk[chunkReceived++] = byte;
      } else {
        Tasks::DisplayTask_StreamBitmapEnd();
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "Timeout at byte %lu/%lu",
                 static_cast<unsigned long>(bytesReceived + chunkReceived),
                 static_cast<unsigned long>(expectedBytes));
        respondErr(errBuf);
        return;
      }
    }

    if (useCrc) {
      crcState = Util::crc32_update(crcState, chunk, chunkReceived);
    }

    Tasks::DisplayTask_StreamBitmapData(chunk, chunkReceived);
    bytesReceived += chunkReceived;
  }

  Tasks::DisplayTask_StreamBitmapEnd();

  if (useCrc) {
    if (!verifyCrc(crcState)) {
      return;
    }
  }

  respondOk("");
}

void CommandParser::cmdDispBitmapB64(const ParsedCommand &cmd) {
  // Check if in REMOTE mode
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_BITMAP_B64 <x> <y> <w> <h> <base64_data>
  if (cmd.argCount < 5) {
    respondErr("Usage: DISP_BITMAP_B64 <x> <y> <w> <h> <base64_data>");
    return;
  }

  uint16_t x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  uint16_t h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  const char *b64 = cmd.args[4];

  // Decode base64 to binary buffer
  // Base64 decodes to ~3/4 of input length
  size_t b64Len = strlen(b64);
  size_t maxDecoded = ((b64Len * 3) / 4) + 1;

  // Limit buffer size to prevent stack overflow
  constexpr size_t MAX_BITMAP_DECODE = 512;
  if (maxDecoded > MAX_BITMAP_DECODE) {
    respondErr("Bitmap too large. Max 512 bytes decoded.");
    return;
  }

  uint8_t decoded[MAX_BITMAP_DECODE];
  size_t decodedLen = 0;

  // Simple base64 decode lookup
  auto b64Val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '+') {
      return 62;
    }
    if (c == '/') {
      return 63;
    }
    return -1;
  };

  size_t i = 0;
  while (i < b64Len && decodedLen < MAX_BITMAP_DECODE) {
    // Get 4 base64 chars
    int v[4] = {0, 0, 0, 0};
    int validChars = 0;

    for (int j = 0; j < 4 && i < b64Len; j++) {
      if (b64[i] == '=') {
        i++;
        continue;
      }
      int val = b64Val(b64[i++]);
      if (val >= 0) {
        v[j] = val;
        validChars++;
      }
    }

    if (validChars >= 2 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[0] << 2) | (v[1] >> 4));
    }
    if (validChars >= 3 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[1] << 4) | (v[2] >> 2));
    }
    if (validChars >= 4 && decodedLen < MAX_BITMAP_DECODE) {
      decoded[decodedLen++] = static_cast<uint8_t>((v[2] << 6) | v[3]);
    }
  }

  // Verify size matches expected
  size_t expectedSize = static_cast<size_t>(w) * h * 2; // RGB565 = 2 bytes/pixel
  if (decodedLen < expectedSize) {
    char buf[48];
    snprintf(buf, sizeof(buf), "Size mismatch: got %u, expected %u",
             static_cast<unsigned>(decodedLen),
             static_cast<unsigned>(expectedSize));
    respondErr(buf);
    return;
  }

  Tasks::DisplayTask_RemoteBitmap(x, y, w, h, decoded, decodedLen);
  respondOk("");
}

void CommandParser::cmdDispIndicator(const ParsedCommand &cmd) {
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  if (cmd.argCount < 3) {
    respondErr("Usage: DISP_INDICATOR <angle> <rot_dir> <has_trans>");
    return;
  }

  int32_t angle = strtol(cmd.args[0], nullptr, 10);
  int32_t rotDir = strtol(cmd.args[1], nullptr, 10);
  int32_t hasTrans = strtol(cmd.args[2], nullptr, 10);

  if (angle < 0 || angle > 359) { respondErr("Invalid angle"); return; }
  if (rotDir < -1 || rotDir > 1) { respondErr("Invalid rotation_dir"); return; }
  if (hasTrans < 0 || hasTrans > 1) { respondErr("Invalid has_translation"); return; }

  Tasks::DisplayTask_RemoteIndicator(
      static_cast<uint16_t>(angle),
      static_cast<int8_t>(rotDir),
      hasTrans != 0);
  respondOk("");
}

void CommandParser::cmdDispBitmapRle(const ParsedCommand &cmd) {
  if (UI::g_uiMode.getMode() != UI::UIMode::REMOTE) {
    respondErr("Not in REMOTE mode");
    return;
  }

  // Usage: DISP_BITMAP_RLE <x> <y> <w> <h> <compressed_bytes> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 6 : 5;

  if (cmd.argCount < minArgs) {
    respondErr("Usage: DISP_BITMAP_RLE <x> <y> <w> <h> <compressed_bytes> [CRC]");
    return;
  }

  uint16_t x = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t y = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t w = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));
  uint16_t h = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  uint32_t compressedBytes = strtoul(cmd.args[4], nullptr, 10);

  if (w == 0 || h == 0) { respondErr("Invalid dimensions"); return; }
  if (x >= 240 || y >= 320) { respondErr("Position out of bounds"); return; }
  if (compressedBytes == 0) { respondErr("Invalid compressed size"); return; }

  constexpr uint32_t MAX_COMPRESSED = 240 * 320 * 3;
  if (compressedBytes > MAX_COMPRESSED) {
    respondErr("Compressed size too large");
    return;
  }

  if (!Tasks::DisplayTask_StreamBitmapStart(x, y, w, h)) {
    respondErr("LCD streaming failed");
    return;
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu", static_cast<unsigned long>(compressedBytes));
  m_transport.println(buf);
  m_transport.flush();

  // Drain trailing \r/\n from command line before binary read
  { uint8_t drain;
    while (m_transport.available() && m_transport.readByte(drain, 1)) {
      if (drain != '\r' && drain != '\n') break;
    }
  }

  // RLE streaming decoder state machine
  enum RleState { HEADER, LITERAL, REPEAT };
  RleState rleState = HEADER;
  uint16_t runCount = 0;
  uint8_t pixelBuf[2] = {0, 0};
  uint8_t pixelIdx = 0;
  uint32_t totalPixels = static_cast<uint32_t>(w) * h;
  uint32_t decodedPixels = 0;

  constexpr uint32_t BYTE_TIMEOUT_MS = 100;
  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < compressedBytes) {
    uint8_t byte;
    if (!m_transport.readByte(byte, BYTE_TIMEOUT_MS)) {
      Tasks::DisplayTask_StreamBitmapEnd();
      char errBuf[48];
      snprintf(errBuf, sizeof(errBuf), "Timeout at byte %lu/%lu",
               static_cast<unsigned long>(bytesReceived),
               static_cast<unsigned long>(compressedBytes));
      respondErr(errBuf);
      return;
    }
    if (useCrc) {
      crcState = Util::crc32_update(crcState, &byte, 1);
    }
    bytesReceived++;

    switch (rleState) {
      case HEADER:
        if (byte & 0x80) {
          runCount = static_cast<uint16_t>(byte - 125);
          rleState = REPEAT;
          pixelIdx = 0;
        } else {
          runCount = static_cast<uint16_t>(byte + 1);
          rleState = LITERAL;
          pixelIdx = 0;
        }
        break;

      case LITERAL:
        pixelBuf[pixelIdx++] = byte;
        if (pixelIdx >= 2) {
          Tasks::DisplayTask_StreamBitmapData(pixelBuf, 2);
          decodedPixels++;
          pixelIdx = 0;
          runCount--;
          if (runCount == 0) rleState = HEADER;
        }
        break;

      case REPEAT:
        pixelBuf[pixelIdx++] = byte;
        if (pixelIdx >= 2) {
          for (uint16_t i = 0; i < runCount; i++) {
            Tasks::DisplayTask_StreamBitmapData(pixelBuf, 2);
            decodedPixels++;
          }
          rleState = HEADER;
          pixelIdx = 0;
        }
        break;
    }
  }

  Tasks::DisplayTask_StreamBitmapEnd();

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState)) {
      return;
    }
  }

  // Verify decoded pixel count
  if (decodedPixels != totalPixels) {
    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "RLE decode: got %lu pixels, expected %lu",
             static_cast<unsigned long>(decodedPixels),
             static_cast<unsigned long>(totalPixels));
    respondErr(errBuf);
    return;
  }

  respondOk("");
}

// ============================================================================
// Debug command handlers
// ============================================================================

void CommandParser::cmdMotorDebug() {
  Tasks::MotorDebugInfo info;
  if (!Tasks::MotorTask_GetDebugInfo(info)) {
    respondErr("Motor not initialized");
    return;
  }

  // Format status bits (powerSTEP01 layout)
  const char* hiZ = ((info.status & 0x0001) != 0) ? "HiZ" : "Active";
  const char* busy = ((info.status & 0x0002) != 0) ? "Idle" : "BUSY";
  bool cmdErr = (info.status & 0x0080) != 0;
  bool uvlo = (info.status & 0x0200) == 0;          // bit 9, active low
  uint8_t thStatus = (info.status >> 11) & 0x3;      // bits 11-12: TH_STATUS
  bool ocd = (info.status & 0x2000) == 0;             // bit 13, active low
  bool stepLossA = (info.status & 0x4000) == 0;       // bit 14, active low
  bool stepLossB = (info.status & 0x8000) == 0;       // bit 15, active low

  char buf[256];  // NOLINT(modernize-avoid-c-arrays)
  snprintf(buf, sizeof(buf),
           "STATUS=%04X (%s %s%s%s%s%s%s%s)\n"
           "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
           "ACC=%u DEC=%u MAXSPD=%u POS=%ld",
           (unsigned)info.status, hiZ, busy,
           cmdErr ? " CMDERR" : "",
           uvlo ? " UVLO" : "",
           thStatus >= 2 ? " TH_SD" : (thStatus >= 1 ? " TH_WARN" : ""),
           ocd ? " OCD" : "",
           stepLossA ? " STALL_A" : "",
           stepLossB ? " STALL_B" : "",
           (unsigned)info.kvalHold, (unsigned)info.kvalRun,
           (unsigned)info.kvalAcc, (unsigned)info.kvalDec,
           (unsigned)info.accel, (unsigned)info.decel,
           (unsigned)info.maxSpeed, (long)info.absPos);
  m_transport.println(buf);

  // Register readback (verify writes took effect)
  snprintf(buf, sizeof(buf),
           "CHIP REGS: OCD_TH=%02X STALL_TH=%02X CONFIG=%04X ALARM_EN=%02X FS_SPD=%03X",
           (unsigned)info.ocdTh, (unsigned)info.stallTh,
           (unsigned)info.config, (unsigned)info.alarmEn,
           (unsigned)info.fsSpd);
  m_transport.println(buf);

  // GPIO diagnostics for SPI1 motor path
  snprintf(buf, sizeof(buf),
           "CS(PC8): MODER=%X ODR=%X IDR=%X  "
           "RST(PA9): ODR=%X  "
           "SPI1: PA5_M=%X PA6_M=%X PA7_M=%X  "
           "FLAG=%X BUSY=%X",
           (unsigned)((GPIOC->MODER >> 16) & 0x3),
           (unsigned)((GPIOC->ODR >> 8) & 0x1),
           (unsigned)((GPIOC->IDR >> 8) & 0x1),
           (unsigned)((GPIOA->ODR >> 9) & 0x1),
           (unsigned)((GPIOA->MODER >> 10) & 0x3),
           (unsigned)((GPIOA->MODER >> 12) & 0x3),
           (unsigned)((GPIOA->MODER >> 14) & 0x3),
           (unsigned)((Pins::IHM03A1::FLAG_PORT->IDR >> Pins::IHM03A1::FLAG_PIN) & 0x1),
           (unsigned)((Pins::IHM03A1::BUSY_PORT->IDR >> Pins::IHM03A1::BUSY_PIN) & 0x1));
  m_transport.println(buf);
}

// ============================================================================
// Trace command handlers
// ============================================================================

void CommandParser::cmdTraceDump() {
  size_t count = Trace::getCount();
  char buf[80];
  snprintf(buf, sizeof(buf), "TRACE: %u entries", static_cast<unsigned>(count));
  m_transport.println(buf);

  Trace::Entry e;
  for (size_t i = 0; i < count; i++) {
    if (!Trace::getEntry(i, e)) break;
    snprintf(buf, sizeof(buf), "[%3u] T=%lu %c %s %lu",
             static_cast<unsigned>(i),
             static_cast<unsigned long>(e.tick),
             (e.dir == Trace::ENTRY) ? '>' : '<',
             e.tag,
             static_cast<unsigned long>(e.arg0));
    m_transport.println(buf);
  }
}

void CommandParser::cmdTraceReset() {
  Trace::reset();
  respondOk("Trace cleared");
}

// ============================================================================
// Motor configuration command handlers
// ============================================================================

void CommandParser::cmdMotorConfigShow() {
  char buf[384];
  m_dispatcher.formatMotorConfig(buf, sizeof(buf));
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

  uint8_t hold = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 16));
  uint8_t run = static_cast<uint8_t>(strtoul(cmd.args[1], nullptr, 16));
  uint8_t acc = static_cast<uint8_t>(strtoul(cmd.args[2], nullptr, 16));
  uint8_t dec = static_cast<uint8_t>(strtoul(cmd.args[3], nullptr, 16));

  m_dispatcher.configSetKval(hold, run, acc, dec);

  char buf[64];
  snprintf(buf, sizeof(buf), "KVAL set: H=%02X R=%02X A=%02X D=%02X (not saved)",
           hold, run, acc, dec);
  respondOk(buf);
}

void CommandParser::cmdMotorConfigOcd(const ParsedCommand &cmd) {
  // Usage: MCONFIG_OCD <threshold>
  if (cmd.argCount < 1) {
    respondErr("Usage: MCONFIG_OCD <threshold> (0-31, ~375mA/step)");
    return;
  }

  uint8_t thresh = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 10));
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

  uint8_t thresh = static_cast<uint8_t>(strtoul(cmd.args[0], nullptr, 10));
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
    respondErr("Usage: MCONFIG_FAULT <ocd> <th_sd> <th_w> <uvlo> <sta> <stb> <cmd>\n"
               "   or: MCONFIG_FAULT ACTION <0=HardStop|1=HardHiZ|2=SoftStop>");
    return;
  }

  // Check for ACTION subcommand
  if (strcmp(cmd.args[0], "ACTION") == 0 || strcmp(cmd.args[0], "action") == 0) {
    if (cmd.argCount < 2) {
      respondErr("Usage: MCONFIG_FAULT ACTION <0|1|2>");
      return;
    }
    uint8_t action = static_cast<uint8_t>(strtoul(cmd.args[1], nullptr, 10));
    if (action > 2) {
      respondErr("Action must be 0=HardStop, 1=HardHiZ, 2=SoftStop");
      return;
    }
    m_dispatcher.configSetFaultAction(action);
    const char* actionStr = action == 0 ? "HardStop" :
                            action == 1 ? "HardHiZ" : "SoftStop";
    char buf[48];
    snprintf(buf, sizeof(buf), "Fault action set: %s (not saved)", actionStr);
    respondOk(buf);
    return;
  }

  // Fault enable flags
  if (cmd.argCount < 7) {
    respondErr("Need 7 flags: ocd th_sd th_w uvlo stall_a stall_b cmd_err (0|1)");
    return;
  }

  bool ocd       = (strtoul(cmd.args[0], nullptr, 10) != 0);
  bool thermalSD  = (strtoul(cmd.args[1], nullptr, 10) != 0);
  bool thermalWarn= (strtoul(cmd.args[2], nullptr, 10) != 0);
  bool uvlo       = (strtoul(cmd.args[3], nullptr, 10) != 0);
  bool stallA     = (strtoul(cmd.args[4], nullptr, 10) != 0);
  bool stallB     = (strtoul(cmd.args[5], nullptr, 10) != 0);
  bool cmdErr     = (strtoul(cmd.args[6], nullptr, 10) != 0);

  m_dispatcher.configSetFaultEnableFlags(ocd, thermalSD, thermalWarn,
                                          uvlo, stallA, stallB, cmdErr);
  respondOk("Fault enables set (not saved)");
}

void CommandParser::cmdMotorConfigMotion(const ParsedCommand &cmd) {
  // Usage: MCONFIG_MOTION <acc> <dec> <maxspd>
  if (cmd.argCount < 3) {
    respondErr("Usage: MCONFIG_MOTION <acc> <dec> <maxspd>");
    return;
  }

  uint16_t acc = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t dec = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  uint16_t maxSpd = static_cast<uint16_t>(strtoul(cmd.args[2], nullptr, 10));

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
  snprintf(buf, sizeof(buf), "Motion params set: ACC=%u DEC=%u MAX=%u (not saved)",
           acc, dec, maxSpd);
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
  if (!Tasks::MotorTask_ApplyConfig()) {
    respondErr("Failed to apply config to motor");
    return;
  }

  // Persist to flash so config survives reboot
  if (!m_dispatcher.configSaveToFlash()) {
    respondErr("Applied but flash write failed");
    return;
  }

  // Readback chip registers to verify writes actually reached the powerSTEP01
  Tasks::MotorDebugInfo info;
  if (Tasks::MotorTask_GetDebugInfo(info)) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "Config applied. CHIP: OCD_TH=%02X STALL_TH=%02X ALARM_EN=%02X CONFIG=%04X FS_SPD=%03X",
             (unsigned)info.ocdTh, (unsigned)info.stallTh,
             (unsigned)info.alarmEn, (unsigned)info.config, (unsigned)info.fsSpd);
    respondOk(buf);
  } else {
    respondOk("Config applied and saved to flash");
  }
}

// ============================================================================
// Event command handlers
// ============================================================================

void CommandParser::cmdEventEnable(const ParsedCommand &cmd) {
  // Parse optional mask argument (default: all events)
  uint8_t mask = EVT_MASK_ALL;
  if (cmd.argCount >= 1) {
    char* endp = nullptr;
    long val = strtol(cmd.args[0], &endp, 0);  // base 0: auto-detect dec/hex
    if (endp == cmd.args[0] || val < 0 || val > 0xFF) {
      respondErr("Invalid mask (0-255)");
      return;
    }
    mask = static_cast<uint8_t>(val) & EVT_MASK_ALL;
  }

  // Read current motor status for snapshot-on-enable
  Comms::TelemetrySnapshot snap = Comms::g_telemetry.getSnapshot();
  uint16_t currentStatus = snap.motor.statusReg;

  m_dispatcher.enableEvents(mask, currentStatus);

  if (m_format == ResponseFormat::JSON) {
    char dataBuf[32];
    snprintf(dataBuf, sizeof(dataBuf), "{\"mask\":%u}", static_cast<unsigned>(mask));
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
  uint32_t sent, lostCritical, lostInfo;
  uint8_t evtMask, queueDepth;
  m_dispatcher.getEventStats(sent, lostCritical, lostInfo, evtMask, queueDepth);
  uint32_t lastSeq = Tasks::CommsTask_GetLastEventSeq();

  if (m_format == ResponseFormat::JSON) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"mask\":%u,\"sent\":%lu,\"lost_critical\":%lu,\"lost_info\":%lu,"
             "\"last_seq\":%lu,\"depth\":%u}",
             static_cast<unsigned>(evtMask),
             static_cast<unsigned long>(sent),
             static_cast<unsigned long>(lostCritical),
             static_cast<unsigned long>(lostInfo),
             static_cast<unsigned long>(lastSeq),
             static_cast<unsigned>(queueDepth));
    respondJsonOk(m_currentCmd, buf);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "EVENT_STATUS mask=%u sent=%lu lost_critical=%lu lost_info=%lu last_seq=%lu depth=%u",
             static_cast<unsigned>(evtMask),
             static_cast<unsigned long>(sent),
             static_cast<unsigned long>(lostCritical),
             static_cast<unsigned long>(lostInfo),
             static_cast<unsigned long>(lastSeq),
             static_cast<unsigned>(queueDepth));
    m_transport.println(buf);
  }
}

// =============================================================================
// Flash image commands (delegate to FlashImageService)
// =============================================================================

void CommandParser::cmdFlashInfo() {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  auto info = svc.getInfo();

  if (m_format == ResponseFormat::JSON) {
    char data[128];
    snprintf(data, sizeof(data),
             "{\"manufacturer\":\"%02X\",\"capacity_kb\":%lu,\"max_slots\":%lu}",
             info.manufacturer,
             static_cast<unsigned long>(info.capacityBytes / 1024),
             static_cast<unsigned long>(info.maxSlots));
    respondJsonOk(m_currentCmd, data);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "FLASH_INFO mfr=%02X cap=%luKB slots=%lu",
             info.manufacturer,
             static_cast<unsigned long>(info.capacityBytes / 1024),
             static_cast<unsigned long>(info.maxSlots));
    respondOk(buf);
  }
}

void CommandParser::cmdFlashUpload(const ParsedCommand &cmd) {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: FLASH_UPLOAD <slot> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 2 : 1;

  if (cmd.argCount < minArgs) {
    respondErr("Usage: FLASH_UPLOAD <slot> [CRC]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= svc.maxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  constexpr uint32_t expectedBytes = Services::FLASH_IMAGE_SIZE;

  // Erase the slot first
  if (!svc.eraseSlot(slot)) {
    respondErr("Flash erase failed");
    return;
  }

  // Send ready response
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(expectedBytes));
  m_transport.println(buf);
  m_transport.flush();

  // Drain trailing \r/\n from command line before binary read
  { uint8_t drain;
    while (m_transport.available() && m_transport.readByte(drain, 1)) {
      if (drain != '\r' && drain != '\n') break;
    }
  }

  // Receive binary data and program page-by-page
  constexpr uint32_t PAGE_SIZE = Services::FLASH_PAGE_SIZE;
  constexpr uint32_t BYTE_TIMEOUT_MS = 100;
  uint8_t page[PAGE_SIZE];
  uint32_t bytesReceived = 0;
  uint32_t crcState = 0xFFFFFFFF;

  while (bytesReceived < expectedBytes) {
    uint32_t remaining = expectedBytes - bytesReceived;
    uint32_t toRead = (remaining < PAGE_SIZE) ? remaining : PAGE_SIZE;

    // Read one page worth of data
    uint32_t pageReceived = 0;
    while (pageReceived < toRead) {
      uint8_t byte;
      if (m_transport.readByte(byte, BYTE_TIMEOUT_MS)) {
        page[pageReceived++] = byte;
      } else {
        char errBuf[48];
        snprintf(errBuf, sizeof(errBuf), "Timeout at byte %lu/%lu",
                 static_cast<unsigned long>(bytesReceived + pageReceived),
                 static_cast<unsigned long>(expectedBytes));
        respondErr(errBuf);
        return;
      }
    }

    if (useCrc) {
      crcState = Util::crc32_update(crcState, page, toRead);
    }

    // Program the page to flash
    if (!svc.writeSlotData(slot, bytesReceived, page, toRead)) {
      respondErr("Flash program failed");
      return;
    }

    bytesReceived += toRead;
  }

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState)) {
      return;
    }
  }

  // Read-back verification: read first 4 bytes from flash to confirm write
  uint8_t verify[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  svc.readSlotChunk(slot, 0, verify, 4);
  char okBuf[96];
  snprintf(okBuf, sizeof(okBuf),
           "Upload complete (%lu bytes, verify %02X%02X%02X%02X)",
           static_cast<unsigned long>(bytesReceived),
           verify[0], verify[1], verify[2], verify[3]);
  respondOk(okBuf);
}

void CommandParser::cmdFlashShow(const ParsedCommand &cmd) {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  if (cmd.argCount < 1) {
    respondErr("Usage: FLASH_SHOW <slot>");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= svc.maxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  // Automatically switch to REMOTE mode for display control
  UI::g_uiMode.setMode(UI::UIMode::REMOTE);

  // Start LCD streaming (full screen)
  if (!Tasks::DisplayTask_StreamBitmapStart(0, 0, 240, 320)) {
    respondErr("LCD streaming failed");
    return;
  }

  // Double-buffered flash→LCD pipeline: DMA1 (SPI2 flash read) overlaps
  // with DMA2 (SPI1 LCD write) since they use independent DMA controllers.
  constexpr uint32_t CHUNK_SIZE = 512;
  uint8_t buf[2][CHUNK_SIZE];
  int cur = 0;
  uint32_t offset = 0;
  uint32_t nonZeroCount = 0;
  uint32_t nonFFCount = 0;
  uint8_t first4[4] = {0};

  // Read first chunk synchronously
  if (!svc.readSlotChunk(slot, 0, buf[cur], CHUNK_SIZE)) {
    Tasks::DisplayTask_StreamBitmapEnd();
    respondErr("Flash read failed");
    return;
  }
  for (uint32_t i = 0; i < 4; i++) first4[i] = buf[cur][i];
  offset = CHUNK_SIZE;

  // Pipeline: start next flash read, then write current chunk to LCD
  while (offset < Services::FLASH_IMAGE_SIZE) {
    uint32_t remaining = Services::FLASH_IMAGE_SIZE - offset;
    uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

    // Start async flash read into other buffer (DMA1 on SPI2)
    if (!svc.readSlotChunkStart(slot, offset, buf[1 - cur], toRead)) {
      Tasks::DisplayTask_StreamBitmapEnd();
      respondErr("Flash read failed");
      return;
    }

    // While flash DMA runs, write current buffer to LCD (DMA2 on SPI1)
    for (uint32_t i = 0; i < CHUNK_SIZE; i++) {
      if (buf[cur][i] != 0x00) nonZeroCount++;
      if (buf[cur][i] != 0xFF) nonFFCount++;
    }
    Tasks::DisplayTask_StreamBitmapData(buf[cur], CHUNK_SIZE);

    // Wait for flash read to complete
    svc.readSlotChunkFinish();

    cur = 1 - cur;
    offset += toRead;
  }

  // Write final chunk to LCD
  uint32_t lastSize = Services::FLASH_IMAGE_SIZE - (offset - CHUNK_SIZE);
  if (lastSize > CHUNK_SIZE) lastSize = CHUNK_SIZE;
  for (uint32_t i = 0; i < lastSize; i++) {
    if (buf[cur][i] != 0x00) nonZeroCount++;
    if (buf[cur][i] != 0xFF) nonFFCount++;
  }
  Tasks::DisplayTask_StreamBitmapData(buf[cur], lastSize);

  Tasks::DisplayTask_StreamBitmapEnd();
  char okBuf[96];
  snprintf(okBuf, sizeof(okBuf),
           "slot=%lu first=%02X%02X%02X%02X nonZero=%lu nonFF=%lu",
           (unsigned long)slot, first4[0], first4[1], first4[2], first4[3],
           (unsigned long)nonZeroCount, (unsigned long)nonFFCount);
  respondOk(okBuf);
}

void CommandParser::cmdFlashEraseAll() {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  if (!svc.eraseAll()) {
    respondErr("Flash erase failed");
    return;
  }

  respondOk("All image slots erased");
}

void CommandParser::cmdFlashDump(const ParsedCommand &cmd) {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: FLASH_DUMP <slot> [offset] [len]
  if (cmd.argCount < 1) {
    respondErr("Usage: FLASH_DUMP <slot> [offset] [len]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= svc.maxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  uint32_t offset = 0;
  uint32_t len = 64;  // Default: dump 64 bytes
  if (cmd.argCount >= 2) offset = strtoul(cmd.args[1], nullptr, 10);
  if (cmd.argCount >= 3) len = strtoul(cmd.args[2], nullptr, 10);
  if (len > 256) len = 256;  // Cap at 256 bytes
  if (offset + len > Services::FLASH_IMAGE_SIZE) {
    respondErr("Offset+len exceeds image size");
    return;
  }

  uint8_t buf[256];
  if (!svc.readSlotChunk(slot, offset, buf, len)) {
    respondErr("Flash read failed");
    return;
  }

  // Print hex dump in 16-byte rows
  char line[80];
  for (uint32_t i = 0; i < len; i += 16) {
    int pos = snprintf(line, sizeof(line), "%06lX:",
                       static_cast<unsigned long>(svc.slotAddress(slot) + offset + i));
    for (uint32_t j = 0; j < 16 && (i + j) < len; j++) {
      pos += snprintf(line + pos, sizeof(line) - pos, " %02X", buf[i + j]);
    }
    m_transport.println(line);
  }
  respondOk("Dump complete");
}

void CommandParser::cmdFlashTest() {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // All diagnostics packed into final response (client discards println lines)
  char result[256];
  int rpos = 0;

  // Step 0: Read JEDEC ID NOW (verifies SPI2 bus is still alive)
  auto info = svc.getInfo();
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   "jedec=%02X/%02X/%02X",
                   info.manufacturer, info.memoryType, info.capacityCode);

  // Step 0b: Check SPI2 peripheral registers
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " SPI2:CR1=%04lX,SR=%04lX",
                   (unsigned long)SPI2->CR1, (unsigned long)SPI2->SR);

  // Step 0c: Dump SPI2 pin config (MODER + AFR for SCK/MISO/MOSI)
  {
    // SCK = PB13
    auto sp = Pins::SPI2_Bus::SCK_PORT;
    auto sn = Pins::SPI2_Bus::SCK_PIN;
    uint32_t sckM = (sp->MODER >> (sn * 2)) & 0x3;
    uint32_t sckAF = (sp->AFR[sn / 8] >> ((sn % 8) * 4)) & 0xF;
    // MISO = PC2
    auto mp = Pins::SPI2_Bus::MISO_PORT;
    auto mn = Pins::SPI2_Bus::MISO_PIN;
    uint32_t misoM = (mp->MODER >> (mn * 2)) & 0x3;
    uint32_t misoAF = (mp->AFR[mn / 8] >> ((mn % 8) * 4)) & 0xF;
    uint32_t misoIDR = (mp->IDR >> mn) & 0x1;
    // MOSI = PC3
    auto op = Pins::SPI2_Bus::MOSI_PORT;
    auto on = Pins::SPI2_Bus::MOSI_PIN;
    uint32_t mosiM = (op->MODER >> (on * 2)) & 0x3;
    uint32_t mosiAF = (op->AFR[on / 8] >> ((on % 8) * 4)) & 0xF;
    rpos += snprintf(result + rpos, sizeof(result) - rpos,
                     " SCK:M%lu/AF%lu MI:M%lu/AF%lu/IDR%lu MO:M%lu/AF%lu",
                     sckM, sckAF, misoM, misoAF, misoIDR, mosiM, mosiAF);
  }

  // Step 1: Read before erase (first 4 bytes)
  uint8_t before[4];
  svc.readSlotChunk(0, 0, before, 4);
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " pre=%02X%02X%02X%02X",
                   before[0], before[1], before[2], before[3]);

  // Step 2: Erase slot 0
  if (!svc.eraseSlot(0)) {
    rpos += snprintf(result + rpos, sizeof(result) - rpos, " erase=ERR");
    respondErr(result);
    return;
  }

  // Step 3: Read after erase (should be all FF)
  uint8_t afterErase[16];
  svc.readSlotChunk(0, 0, afterErase, 16);
  bool eraseOk = true;
  for (int i = 0; i < 16; i++) {
    if (afterErase[i] != 0xFF) { eraseOk = false; break; }
  }
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " era=%s/%02X%02X%02X%02X",
                   eraseOk ? "OK" : "FAIL",
                   afterErase[0], afterErase[1], afterErase[2], afterErase[3]);

  // Step 4: Write test pattern to first page
  uint8_t pattern[256];
  for (int i = 0; i < 256; i++) pattern[i] = (i & 1) ? 0x55 : 0xAA;
  if (!svc.writeSlotData(0, 0, pattern, 256)) {
    rpos += snprintf(result + rpos, sizeof(result) - rpos, " wr=ERR");
    respondErr(result);
    return;
  }

  // Step 5: Read back and compare
  uint8_t readback[256];
  svc.readSlotChunk(0, 0, readback, 256);
  int mismatches = 0;
  for (int i = 0; i < 256; i++) {
    if (readback[i] != pattern[i]) mismatches++;
  }
  rpos += snprintf(result + rpos, sizeof(result) - rpos,
                   " wr=%s(%d) rb=%02X%02X%02X%02X",
                   mismatches == 0 ? "OK" : "FAIL",
                   256 - mismatches,
                   readback[0], readback[1], readback[2], readback[3]);

  if (mismatches == 0 && eraseOk) {
    respondOk(result);
  } else {
    respondErr(result);
  }
}

// ========================================================================
// CRC32 helpers
// ========================================================================

bool CommandParser::hasCrcFlag(const ParsedCommand &cmd) const {
  if (cmd.argCount < 1) return false;
  return (strcmp(cmd.args[cmd.argCount - 1], "CRC") == 0);
}

bool CommandParser::verifyCrc(uint32_t computedCrc) {
  // Finalize CRC
  uint32_t expected = computedCrc ^ 0xFFFFFFFF;

  // Read 4 CRC bytes (little-endian)
  constexpr uint32_t CRC_TIMEOUT_MS = 200;
  uint8_t crcBytes[4];
  for (int i = 0; i < 4; i++) {
    if (!m_transport.readByte(crcBytes[i], CRC_TIMEOUT_MS)) {
      respondErr("CRC bytes timeout");
      return false;
    }
  }

  uint32_t received = static_cast<uint32_t>(crcBytes[0])
                    | (static_cast<uint32_t>(crcBytes[1]) << 8)
                    | (static_cast<uint32_t>(crcBytes[2]) << 16)
                    | (static_cast<uint32_t>(crcBytes[3]) << 24);

  if (received != expected) {
    char buf[64];
    snprintf(buf, sizeof(buf), "CRC mismatch: expected %08lX got %08lX",
             static_cast<unsigned long>(expected),
             static_cast<unsigned long>(received));
    respondErr(buf);
    return false;
  }

  return true;
}

// ========================================================================
// FLASH_UPLOAD_RLE: upload RLE-compressed image to flash slot
// ========================================================================

void CommandParser::cmdFlashUploadRle(const ParsedCommand &cmd) {
  auto &svc = Services::g_flashImageService;
  if (!svc.isAvailable()) {
    respondErr("Flash not available");
    return;
  }

  // Usage: FLASH_UPLOAD_RLE <slot> <compressed_bytes> [CRC]
  bool useCrc = hasCrcFlag(cmd);
  uint32_t minArgs = useCrc ? 3 : 2;  // slot, comp_bytes [, CRC]

  if (cmd.argCount < minArgs) {
    respondErr("Usage: FLASH_UPLOAD_RLE <slot> <compressed_bytes> [CRC]");
    return;
  }

  uint32_t slot = strtoul(cmd.args[0], nullptr, 10);
  if (slot >= svc.maxSlots()) {
    respondErr("Slot out of range");
    return;
  }

  uint32_t compressedBytes = strtoul(cmd.args[1], nullptr, 10);
  if (compressedBytes == 0) {
    respondErr("Invalid compressed size");
    return;
  }

  constexpr uint32_t MAX_COMPRESSED = 240 * 320 * 3;
  if (compressedBytes > MAX_COMPRESSED) {
    respondErr("Compressed size too large");
    return;
  }

  // Erase the slot first
  if (!svc.eraseSlot(slot)) {
    respondErr("Flash erase failed");
    return;
  }

  // Send ready response
  char buf[32];
  snprintf(buf, sizeof(buf), "OK READY %lu",
           static_cast<unsigned long>(compressedBytes));
  m_transport.println(buf);
  m_transport.flush();

  // Drain trailing \r/\n from command line before binary read
  { uint8_t drain;
    while (m_transport.available() && m_transport.readByte(drain, 1)) {
      if (drain != '\r' && drain != '\n') break;
    }
  }

  // RLE streaming decoder → page buffer → flash
  enum RleState { HEADER, LITERAL, REPEAT };
  RleState rleState = HEADER;
  uint16_t runCount = 0;
  uint8_t pixelBuf[2] = {0, 0};
  uint8_t pixelIdx = 0;
  uint32_t totalPixels = 240UL * 320;
  uint32_t decodedPixels = 0;

  constexpr uint32_t PAGE_SIZE = Services::FLASH_PAGE_SIZE;
  constexpr uint32_t BYTE_TIMEOUT_MS = 100;
  uint8_t page[PAGE_SIZE];
  uint32_t pageOffset = 0;   // Bytes in current page buffer
  uint32_t flashOffset = 0;  // Byte offset into the flash slot

  uint32_t crcState = 0xFFFFFFFF;
  uint32_t bytesReceived = 0;

  // Helper lambda: flush current page to flash
  auto flushPage = [&]() -> bool {
    if (pageOffset == 0) return true;
    if (!svc.writeSlotData(slot, flashOffset, page, pageOffset)) {
      return false;
    }
    flashOffset += pageOffset;
    pageOffset = 0;
    return true;
  };

  // Helper: emit one decoded pixel (2 bytes) into the page buffer
  auto emitPixel = [&](uint8_t hi, uint8_t lo) -> bool {
    page[pageOffset++] = hi;
    if (pageOffset >= PAGE_SIZE) {
      if (!flushPage()) return false;
    }
    page[pageOffset++] = lo;
    if (pageOffset >= PAGE_SIZE) {
      if (!flushPage()) return false;
    }
    decodedPixels++;
    return true;
  };

  while (bytesReceived < compressedBytes) {
    uint8_t byte;
    if (!m_transport.readByte(byte, BYTE_TIMEOUT_MS)) {
      char errBuf[48];
      snprintf(errBuf, sizeof(errBuf), "Timeout at byte %lu/%lu",
               static_cast<unsigned long>(bytesReceived),
               static_cast<unsigned long>(compressedBytes));
      respondErr(errBuf);
      return;
    }
    if (useCrc) {
      crcState = Util::crc32_update(crcState, &byte, 1);
    }
    bytesReceived++;

    switch (rleState) {
      case HEADER:
        if (byte & 0x80) {
          runCount = static_cast<uint16_t>(byte - 125);
          rleState = REPEAT;
          pixelIdx = 0;
        } else {
          runCount = static_cast<uint16_t>(byte + 1);
          rleState = LITERAL;
          pixelIdx = 0;
        }
        break;

      case LITERAL:
        pixelBuf[pixelIdx++] = byte;
        if (pixelIdx >= 2) {
          if (!emitPixel(pixelBuf[0], pixelBuf[1])) {
            respondErr("Flash program failed");
            return;
          }
          pixelIdx = 0;
          runCount--;
          if (runCount == 0) rleState = HEADER;
        }
        break;

      case REPEAT:
        pixelBuf[pixelIdx++] = byte;
        if (pixelIdx >= 2) {
          for (uint16_t i = 0; i < runCount; i++) {
            if (!emitPixel(pixelBuf[0], pixelBuf[1])) {
              respondErr("Flash program failed");
              return;
            }
          }
          rleState = HEADER;
          pixelIdx = 0;
        }
        break;
    }
  }

  // Flush remaining page data
  if (!flushPage()) {
    respondErr("Flash program failed (final page)");
    return;
  }

  // Verify CRC if requested
  if (useCrc) {
    if (!verifyCrc(crcState)) {
      return;  // verifyCrc already sent error response
    }
  }

  // Verify decoded pixel count
  if (decodedPixels != totalPixels) {
    char errBuf[64];
    snprintf(errBuf, sizeof(errBuf), "RLE decode: got %lu pixels, expected %lu",
             static_cast<unsigned long>(decodedPixels),
             static_cast<unsigned long>(totalPixels));
    respondErr(errBuf);
    return;
  }

  // Read-back verification: read first 4 bytes from flash to confirm write
  uint8_t verify[4] = {0xFF, 0xFF, 0xFF, 0xFF};
  svc.readSlotChunk(slot, 0, verify, 4);

  char okBuf[80];
  snprintf(okBuf, sizeof(okBuf),
           "Upload complete (%lu px, verify %02X%02X%02X%02X)",
           static_cast<unsigned long>(decodedPixels),
           verify[0], verify[1], verify[2], verify[3]);
  respondOk(okBuf);
}

// ============================================================================
// Phase A: Unit model, safety, and infrastructure commands
// ============================================================================

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
  Comms::TelemetrySnapshot snap = Comms::g_telemetry.getSnapshot();
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
  Tasks::MotorTask_Suspend();
  uint8_t readback = 0;
  bool ok = Tasks::MotorTask_SetStepModeSafe(static_cast<uint8_t>(mode), readback);
  Tasks::MotorTask_Resume();

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
    snprintf(json, sizeof(json),
             "{\"step_mode\":%u,\"microstep_ratio\":%u,\"microsteps_per_rev\":%lu}",
             (unsigned)readback, 1U << readback,
             static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:STEP_MODE", json);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), "STEP_MODE=%u MICROSTEP_RATIO=1/%u MICROSTEPS_PER_REV=%lu",
             (unsigned)readback, 1U << readback,
             static_cast<unsigned long>(ustepsPerRev));
    respondOk(buf);
  }
}

// DRV:STEP_MODE? — read step mode from hardware
void CommandParser::cmdDrvStepModeQuery() {
  Tasks::MotorDebugInfo info = {};
  Tasks::MotorTask_GetDebugInfo(info);
  uint8_t mode = info.stepMode;
  uint32_t ustepsPerRev = static_cast<uint32_t>(m_dispatcher.getFullStepsPerRev()) * (1U << mode);

  if (m_format == ResponseFormat::JSON) {
    char json[128];
    snprintf(json, sizeof(json),
             "{\"step_mode\":%u,\"microstep_ratio\":%u,\"microsteps_per_rev\":%lu}",
             (unsigned)mode, 1U << mode,
             static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:STEP_MODE?", json);
  } else {
    char buf[80];
    snprintf(buf, sizeof(buf), "STEP_MODE=%u MICROSTEP_RATIO=1/%u MICROSTEPS_PER_REV=%lu",
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
             "{\"full_steps_per_rev\":%lu,\"microsteps_per_rev\":%lu}",
             steps, static_cast<unsigned long>(ustepsPerRev));
    respondJsonOk("DRV:FULL_STEPS", json);
  } else {
    char buf[64];
    snprintf(buf, sizeof(buf), "FULL_STEPS=%lu MICROSTEPS_PER_REV=%lu",
             steps, static_cast<unsigned long>(ustepsPerRev));
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

// SYST:DELAY <ms> — set transport delay compensation
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

// SYST:DELAY? — query transport delay compensation
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

// CTRL:FOLLOW? — query following error
void CommandParser::cmdFollowingError() {
  // Expanded supervisor status including state, tier, and following error
  uint8_t svState, svTier, svRetryCount;
  int32_t svPosError, svVelError, svSetpoint;
  int16_t svPidOutput;
  uint32_t svErrorDurationMs;
  m_dispatcher.getSupervisorTelemetry(svState, svTier, svPosError, svVelError,
                                       svSetpoint, svPidOutput, svRetryCount,
                                       svErrorDurationMs);
  uint8_t mode = m_dispatcher.getControlMode();

  static const char* stateNames[] = {"IDLE", "MOVING", "HOLDING", "RECOVERY", "FAULT"};
  static const char* tierNames[] = {"OBSERVE", "SOFT_CORRECT", "PAUSE_RECOVER", "FAULT_LATCH"};
  uint8_t si = svState;
  uint8_t ti = svTier;
  const char* stateName = (si < 5) ? stateNames[si] : "UNKNOWN";
  const char* tierName  = (ti < 4) ? tierNames[ti] : "UNKNOWN";

  if (m_format == ResponseFormat::JSON) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"state\":\"%s\",\"tier\":\"%s\",\"mode\":%u,"
             "\"pos_error\":%ld,\"vel_error\":%ld,"
             "\"setpoint\":%ld,\"pid_out\":%d,"
             "\"retries\":%u,\"error_ms\":%lu}",
             stateName, tierName, (unsigned)mode,
             static_cast<long>(svPosError),
             static_cast<long>(svVelError),
             static_cast<long>(svSetpoint),
             (int)svPidOutput,
             (unsigned)svRetryCount,
             static_cast<unsigned long>(svErrorDurationMs));
    respondJsonOk("CTRL:FOLLOW?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "state=%s tier=%s err=%ld vel_err=%ld retries=%u",
             stateName, tierName,
             static_cast<long>(svPosError),
             static_cast<long>(svVelError),
             (unsigned)svRetryCount);
    respondOk(buf);
  }
}

void CommandParser::cmdFollowThreshQuery() {
  int16_t me, he, hl;
  uint16_t mt, ht;
  uint8_t mr;
  m_dispatcher.getFollowThresholds(me, mt, he, ht, hl, mr);

  if (m_format == ResponseFormat::JSON) {
    char json[160];
    snprintf(json, sizeof(json),
             "{\"move_error\":%d,\"move_time_ms\":%u,"
             "\"hold_error\":%d,\"hold_time_ms\":%u,"
             "\"hard_limit\":%d,\"max_retries\":%u}",
             (int)me, (unsigned)mt, (int)he, (unsigned)ht,
             (int)hl, (unsigned)mr);
    respondJsonOk("CTRL:FOLLOW:THRESH?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "E_MOVE=%d T_MOVE=%u E_HOLD=%d T_HOLD=%u E_HARD=%d N_RETRY=%u",
             (int)me, (unsigned)mt, (int)he, (unsigned)ht,
             (int)hl, (unsigned)mr);
    respondOk(buf);
  }
}

void CommandParser::cmdFollowThreshSet(const ParsedCommand &cmd) {
  if (cmd.argCount < 6) {
    respondErr("Usage: CTRL:FOLLOW:THRESH <E_MOVE> <T_MOVE> <E_HOLD> <T_HOLD> <E_HARD> <N_RETRY>");
    return;
  }

  int16_t  moveErr   = static_cast<int16_t>(strtol(cmd.args[0], nullptr, 10));
  uint16_t moveTime  = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));
  int16_t  holdErr   = static_cast<int16_t>(strtol(cmd.args[2], nullptr, 10));
  uint16_t holdTime  = static_cast<uint16_t>(strtoul(cmd.args[3], nullptr, 10));
  int16_t  hardLimit = static_cast<int16_t>(strtol(cmd.args[4], nullptr, 10));
  uint8_t  maxRetry  = static_cast<uint8_t>(strtoul(cmd.args[5], nullptr, 10));

  m_dispatcher.setFollowThresholds(moveErr, moveTime, holdErr, holdTime,
                                    hardLimit, maxRetry);

  // Apply immediately to supervisor
  m_dispatcher.applySupervisorConfig();

  respondOk("Thresholds set");
}

void CommandParser::cmdFollowSave() {
  if (m_dispatcher.configSaveToFlash()) {
    respondOk("Follow config saved");
  } else {
    respondErr("Flash write failed");
  }
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
  if (*s == '-') { sign = -1; s++; }
  int integer = 0;
  while (*s >= '0' && *s <= '9') { integer = integer * 10 + (*s - '0'); s++; }
  int frac = 0;
  int fracDigits = 0;
  if (*s == '.') {
    s++;
    while (*s >= '0' && *s <= '9' && fracDigits < 2) {
      frac = frac * 10 + (*s - '0'); s++; fracDigits++;
    }
    while (fracDigits < 2) { frac *= 10; fracDigits++; }
  }
  return static_cast<int16_t>(sign * (integer * 100 + frac));
}

void CommandParser::cmdTrimQuery() {
  int16_t kp100, ki100, kd100;
  uint16_t outLim, iLim;
  m_dispatcher.getTrimGains(kp100, ki100, kd100, outLim, iLim);
  uint8_t maxPct = (kd100 > 0 && kd100 <= 50) ? static_cast<uint8_t>(kd100) : 8;

  TelemetrySnapshot snap = g_telemetry.getSnapshot();
  bool active = snap.control.tracking;
  bool frozen = snap.control.trimFrozen != 0;

  if (m_format == ResponseFormat::JSON) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"kp\":\"%d.%02d\",\"ki\":\"%d.%02d\","
             "\"out_limit\":%u,\"i_limit\":%u,\"max_pct\":%u,"
             "\"active\":%s,\"frozen\":%s,"
             "\"base_spd\":%ld,\"trim_spd\":%ld,\"final_spd\":%ld,"
             "\"vel_quality\":%u}",
             kp100 / 100, (kp100 < 0 ? -kp100 : kp100) % 100,
             ki100 / 100, (ki100 < 0 ? -ki100 : ki100) % 100,
             (unsigned)outLim, (unsigned)iLim, (unsigned)maxPct,
             active ? "true" : "false",
             frozen ? "true" : "false",
             static_cast<long>(snap.control.baseSpeedRaw),
             static_cast<long>(snap.control.trimSpeedRaw),
             static_cast<long>(snap.control.finalSpeedRaw),
             static_cast<unsigned>(snap.control.velQuality));
    respondJsonOk("CTRL:TRIM?", json);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "TRIM Kp=%d.%02d Ki=%d.%02d out=%u i=%u maxPct=%u %s%s",
             kp100 / 100, (kp100 < 0 ? -kp100 : kp100) % 100,
             ki100 / 100, (ki100 < 0 ? -ki100 : ki100) % 100,
             (unsigned)outLim, (unsigned)iLim, (unsigned)maxPct,
             active ? "ACTIVE" : "IDLE",
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
    snprintf(json, sizeof(json),
             "{\"kp\":\"%d.%02d\",\"ki\":\"%d.%02d\"}",
             kp100 / 100, (kp100 < 0 ? -kp100 : kp100) % 100,
             ki100 / 100, (ki100 < 0 ? -ki100 : ki100) % 100);
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

  uint16_t outLim = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));
  uint16_t iLim = static_cast<uint16_t>(strtoul(cmd.args[1], nullptr, 10));

  m_dispatcher.setTrimLimits(outLim, iLim);

  // Reconfigure live trim controller
  m_dispatcher.applyTrimConfig();

  if (m_format == ResponseFormat::JSON) {
    char json[64];
    snprintf(json, sizeof(json),
             "{\"out_limit\":%u,\"i_limit\":%u}",
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
  if (m_dispatcher.configSaveToFlash()) {
    respondOk("Trim config saved");
  } else {
    respondErr("Flash write failed");
  }
}

// =========================================================================
// CTRL:PID:* — Legacy aliases (route to trim commands)
// =========================================================================

void CommandParser::cmdPidQuery() {
  cmdTrimQuery();  // Same output
}

void CommandParser::cmdPidSetGains(const ParsedCommand &cmd) {
  // Accept 2 or 3 args: CTRL:PID:GAINS <kp> <ki> [<kd>]
  // kd is ignored (always 0 for PI trim), but don't reject old 3-arg callers
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:PID:GAINS <kp> <ki> [<kd>]");
    return;
  }
  cmdTrimSetGains(cmd);  // Uses first 2 args
}

void CommandParser::cmdPidSetLimits(const ParsedCommand &cmd) {
  cmdTrimSetLimits(cmd);
}

void CommandParser::cmdPidReset() {
  cmdTrimReset();
}

void CommandParser::cmdPidSave() {
  cmdTrimSave();
}

// --- System Identification commands ---

void CommandParser::cmdSysIdStep(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:SYSID:STEP <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  uint16_t targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs = (cmd.argCount >= 3)
      ? static_cast<uint16_t>(atol(cmd.args[2])) : 5000;
  uint16_t settleMs = (cmd.argCount >= 4)
      ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
    return;
  }

  if (m_dispatcher.sysIdStart(0, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID step started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRamp(const ParsedCommand &cmd) {
  if (cmd.argCount < 3) {
    respondErr("Usage: CTRL:SYSID:RAMP <start_sps> <end_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  uint16_t startSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  uint16_t targetSpd = static_cast<uint16_t>(atol(cmd.args[1]));
  bool forward = (atol(cmd.args[2]) != 0);
  uint16_t durMs = (cmd.argCount >= 4)
      ? static_cast<uint16_t>(atol(cmd.args[3])) : 5000;
  uint16_t settleMs = (cmd.argCount >= 5)
      ? static_cast<uint16_t>(atol(cmd.args[4])) : 1000;

  if (targetSpd > 15625 || startSpd > 15625) {
    respondErr("speed must be 0-15625 sps");
    return;
  }

  if (m_dispatcher.sysIdStart(1, targetSpd, startSpd, forward, durMs, settleMs, 0)) {
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
    snprintf(data, sizeof(data),
             "{\"phase\":%u,\"samples\":%u}",
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
  if (m_dispatcher.sysIdGetPhase() != 4) {  // 4 = DONE
    respondErr("No data ready (test not DONE)");
    return;
  }

  uint16_t offset = (cmd.argCount >= 1)
      ? static_cast<uint16_t>(atol(cmd.args[0])) : 0;
  uint16_t count = (cmd.argCount >= 2)
      ? static_cast<uint16_t>(atol(cmd.args[1])) : 20;
  uint16_t total;
  const auto *samples = m_dispatcher.sysIdGetSamples(total);

  if (offset >= total) {
    respondErr("offset past end");
    return;
  }
  if (offset + count > total) {
    count = total - offset;
  }

  // Header line
  char buf[64];
  snprintf(buf, sizeof(buf), "SYSID_DATA %u %u %u",
           static_cast<unsigned>(offset), static_cast<unsigned>(count),
           static_cast<unsigned>(total));
  m_transport.println(buf);

  // CSV data: time_ms,setpoint_tps,actual_tps,pos_error
  for (uint16_t i = 0; i < count; i++) {
    const auto &s = samples[offset + i];
    snprintf(buf, sizeof(buf), "%u,%d,%d,%d",
             static_cast<unsigned>(s.time_ms),
             static_cast<int>(s.setpoint_tps),
             static_cast<int>(s.actual_tps),
             static_cast<int>(s.pos_error));
    m_transport.println(buf);
  }

  m_transport.println("END");
}

void CommandParser::cmdSysIdSine(const ParsedCommand &cmd) {
  // CTRL:SYSID:SINE <baseline_sps> <amplitude_sps> <freq_mHz> <dir> [dur_ms] [settle_ms]
  if (cmd.argCount < 4) {
    respondErr("Usage: CTRL:SYSID:SINE <baseline> <amplitude> <freq_mHz> <dir> [dur] [settle]");
    return;
  }

  uint16_t baseline = static_cast<uint16_t>(atol(cmd.args[0]));
  uint16_t amplitude = static_cast<uint16_t>(atol(cmd.args[1]));
  uint16_t freqMHz = static_cast<uint16_t>(atol(cmd.args[2]));
  bool forward = (atol(cmd.args[3]) != 0);
  uint16_t durMs = (cmd.argCount >= 5)
      ? static_cast<uint16_t>(atol(cmd.args[4])) : 10000;
  uint16_t settleMs = (cmd.argCount >= 6)
      ? static_cast<uint16_t>(atol(cmd.args[5])) : 1000;

  if (baseline > 15625 || amplitude > 15625) {
    respondErr("speed must be 0-15625 sps");
    return;
  }
  if (freqMHz == 0 || freqMHz > 5000) {
    respondErr("freq_mHz must be 1-5000 (0.001-5.0 Hz)");
    return;
  }

  if (m_dispatcher.sysIdStart(2, amplitude, baseline, forward, durMs, settleMs, freqMHz)) {
    respondOk("SYSID sine started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdTrapezoid(const ParsedCommand &cmd) {
  // CTRL:SYSID:TRAPEZOID <speed_sps> <dir> [dur_ms] [settle_ms]
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:SYSID:TRAPEZOID <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  uint16_t targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs = (cmd.argCount >= 3)
      ? static_cast<uint16_t>(atol(cmd.args[2])) : 8000;
  uint16_t settleMs = (cmd.argCount >= 4)
      ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
    return;
  }

  if (m_dispatcher.sysIdStart(3, targetSpd, 0, forward, durMs, settleMs, 0)) {
    respondOk("SYSID trapezoid started");
  } else {
    respondErr("SYSID already running");
  }
}

void CommandParser::cmdSysIdRect(const ParsedCommand &cmd) {
  // CTRL:SYSID:RECT <speed_sps> <dir> [dur_ms] [settle_ms]
  if (cmd.argCount < 2) {
    respondErr("Usage: CTRL:SYSID:RECT <speed_sps> <dir> [dur_ms] [settle_ms]");
    return;
  }

  uint16_t targetSpd = static_cast<uint16_t>(atol(cmd.args[0]));
  bool forward = (atol(cmd.args[1]) != 0);
  uint16_t durMs = (cmd.argCount >= 3)
      ? static_cast<uint16_t>(atol(cmd.args[2])) : 6000;
  uint16_t settleMs = (cmd.argCount >= 4)
      ? static_cast<uint16_t>(atol(cmd.args[3])) : 1000;

  if (targetSpd == 0 || targetSpd > 15625) {
    respondErr("speed_sps must be 1-15625");
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

} // namespace Comms
