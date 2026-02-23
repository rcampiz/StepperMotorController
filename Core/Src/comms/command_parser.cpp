/**
 * @file command_parser.cpp
 * @brief ASCII command protocol parser implementation
 *
 * Supports synchronized multi-controller operation.
 * See docs/HOST_INTERFACE_AND_SYNC.md for protocol specification.
 */

#include "comms/command_parser.hpp"
#include "comms/telemetry.hpp"
#include "services/command_queue.hpp"
#include "services/event_service.hpp"
#include "tasks/comms_task.hpp"
#include "services/config_service.hpp"
#include "services/control_mode.hpp"
#include "services/device_config.hpp"
#include "services/motion_service.hpp"
#include "services/motor_config.hpp"
#include "services/safety_service.hpp"
#include "services/tick_timer.hpp"
#include "services/trace.hpp"
#include "services/flash_image_service.hpp"
#include "util/crc32.hpp"
#include "tasks/display_task.hpp"
#include "tasks/encoder_task.hpp"
#include "tasks/motor_task.hpp"
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

CommandParser::CommandParser(ITransport &transport)
    : m_transport(transport), m_bufIndex(0), m_format(ResponseFormat::ASCII) {
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
  char buf[256];
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
    respondErr("Usage: MOVE <steps> <dir>");
    return;
  }

  int32_t steps = static_cast<int32_t>(atol(cmd.args[0]));
  int32_t dir = static_cast<int32_t>(atol(cmd.args[1]));

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
  if (cmd.argCount >= 3) {
    int32_t spd = static_cast<int32_t>(atol(cmd.args[2]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto result = Services::Motion::move(signedSteps, speedOverride);
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdGoTo(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: GOTO <position> [speed_steps_s]");
    return;
  }

  int32_t position = static_cast<int32_t>(atol(cmd.args[0]));

  // Validate position (22-bit signed range)
  if (position < Limits::POS_MIN || position > Limits::POS_MAX) {
    respondErr("position out of range (-2097152 to 2097151)");
    return;
  }

  // Optional speed override (steps/s) — temporarily sets MAX_SPEED for this move
  uint32_t speedOverride = 0;
  if (cmd.argCount >= 2) {
    int32_t spd = static_cast<int32_t>(atol(cmd.args[1]));
    if (spd < 1 || spd > Limits::SPEED_MAX) {
      respondErr("speed out of range (1-15625 steps/s)");
      return;
    }
    speedOverride = static_cast<uint32_t>(spd);
  }

  auto result = Services::Motion::goTo(position, speedOverride);
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdRun(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: RUN <speed> <dir>");
    return;
  }

  int32_t speed = static_cast<int32_t>(atol(cmd.args[0]));
  int32_t dir = static_cast<int32_t>(atol(cmd.args[1]));

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

  auto result = Services::Motion::run(static_cast<uint32_t>(speed), dir == 1);
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdStop(const ParsedCommand &cmd) {
  // Optional "hard" argument
  bool hard = (cmd.argCount > 0 && strcmp(cmd.args[0], "hard") == 0);

  auto result = Services::Motion::stop(hard);
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdEstop() {
  Services::Safety::emergencyStop();
  respondOk("ESTOP");
}

// ============================================================================
// Configuration command handlers
// ============================================================================

void CommandParser::cmdEnable() {
  auto result = Services::Motion::enable();
  if (result == Services::Motion::Result::OK) {
    respondOk("ENABLED");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdDisable() {
  auto result = Services::Motion::disable();
  if (result == Services::Motion::Result::OK) {
    respondOk("HI-Z");
  } else {
    respondErr(Services::Motion::resultToString(result));
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

  auto result = Services::Config::setAccelRaw(static_cast<uint16_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  auto result = Services::Config::setDecelRaw(static_cast<uint16_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  auto result = Services::Config::setMaxSpeedRaw(static_cast<uint16_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  auto result = Services::Config::setAccelPhysical(static_cast<uint32_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else if (result == Services::Config::Result::INVALID_PARAM) {
    respondErr("accel out of range (1-59590 steps/s^2)");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  auto result = Services::Config::setDecelPhysical(static_cast<uint32_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else if (result == Services::Config::Result::INVALID_PARAM) {
    respondErr("decel out of range (1-59590 steps/s^2)");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  auto result = Services::Config::setMaxSpeedPhysical(static_cast<uint32_t>(value));
  if (result == Services::Config::Result::OK) {
    respondOk("");
  } else if (result == Services::Config::Result::INVALID_PARAM) {
    respondErr("maxspd out of range (1-15609 steps/s)");
  } else {
    respondErr(Services::Config::resultToString(result));
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

  // Parse the queued command type and build MotorCommand
  Tasks::MotorCommand motorCmd = {};
  const char *subCmd = cmd.args[0];

  if (strcmp(subCmd, "MOVE") == 0 || strcmp(subCmd, "move") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE MOVE <steps> <dir>");
      return;
    }
    int32_t steps = static_cast<int32_t>(atol(cmd.args[1]));
    int32_t dir = static_cast<int32_t>(atol(cmd.args[2]));
    // Validate direction
    if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
      respondErr("dir must be 0 or 1");
      return;
    }
    // Validate steps
    if (steps < 0 || steps > Limits::POS_MAX) {
      respondErr("steps out of range (0-2097151)");
      return;
    }
    motorCmd.type = Tasks::MotorCmdType::Move;
    motorCmd.param1 = (dir == 0) ? -steps : steps;
  } else if (strcmp(subCmd, "GOTO") == 0 || strcmp(subCmd, "goto") == 0) {
    if (cmd.argCount < 2) {
      respondErr("Usage: QUEUE GOTO <position>");
      return;
    }
    int32_t position = static_cast<int32_t>(atol(cmd.args[1]));
    // Validate position
    if (position < Limits::POS_MIN || position > Limits::POS_MAX) {
      respondErr("position out of range (-2097152 to 2097151)");
      return;
    }
    motorCmd.type = Tasks::MotorCmdType::GoTo;
    motorCmd.param1 = position;
  } else if (strcmp(subCmd, "RUN") == 0 || strcmp(subCmd, "run") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE RUN <speed> <dir>");
      return;
    }
    int32_t speed = static_cast<int32_t>(atol(cmd.args[1]));
    int32_t dir = static_cast<int32_t>(atol(cmd.args[2]));
    // Validate direction
    if (dir < Limits::DIR_MIN || dir > Limits::DIR_MAX) {
      respondErr("dir must be 0 or 1");
      return;
    }
    // Validate speed (in steps/s)
    if (speed < Limits::SPEED_MIN || speed > Limits::SPEED_MAX) {
      respondErr("speed out of range (0-15625 steps/s)");
      return;
    }
    // Convert steps/s to raw register value for powerSTEP01
    uint32_t speedRaw = static_cast<uint32_t>(
        (static_cast<uint64_t>(speed) * 1048576ULL) / 15625ULL);
    motorCmd.type = Tasks::MotorCmdType::Run;
    motorCmd.param1 = static_cast<int32_t>(speedRaw);
    motorCmd.param2 = dir;
  } else if (strcmp(subCmd, "STOP") == 0 || strcmp(subCmd, "stop") == 0) {
    bool hard = (cmd.argCount > 1 && strcmp(cmd.args[1], "hard") == 0);
    motorCmd.type =
        hard ? Tasks::MotorCmdType::HardStop : Tasks::MotorCmdType::SoftStop;
  } else if (strcmp(subCmd, "HOME") == 0 || strcmp(subCmd, "home") == 0) {
    motorCmd.type = Tasks::MotorCmdType::GoHome;
  } else if (strcmp(subCmd, "ZERO") == 0 || strcmp(subCmd, "zero") == 0) {
    motorCmd.type = Tasks::MotorCmdType::ResetPos;
  } else {
    respondErr("Unknown queue command");
    return;
  }

  // Add to command queue
  Services::QueueResult result =
      Services::g_commandQueue.queueCommand(motorCmd);
  if (result == Services::QueueResult::OK) {
    char buf[32];
    snprintf(buf, sizeof(buf), "QUEUED %u",
             static_cast<unsigned>(Services::g_commandQueue.getQueueDepth()));
    respondOk(buf);
  } else {
    respondErr(Services::resultToString(result));
  }
}

void CommandParser::cmdArm() {
  Services::QueueResult result = Services::g_commandQueue.arm();
  if (result == Services::QueueResult::OK) {
    respondOk("ARMED");
  } else {
    respondErr(Services::resultToString(result));
  }
}

void CommandParser::cmdStart() {
  Services::QueueResult result = Services::g_commandQueue.start();
  if (result == Services::QueueResult::OK) {
    respondOk("RUNNING");
  } else {
    respondErr(Services::resultToString(result));
  }
}

void CommandParser::cmdStartAt(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: START_AT <tick>");
    return;
  }

  uint32_t targetTick =
      static_cast<uint32_t>(strtoul(cmd.args[0], nullptr, 10));
  Services::QueueResult result = Services::g_commandQueue.startAt(targetTick);
  if (result == Services::QueueResult::OK) {
    respondOk("RUNNING");
  } else {
    respondErr(Services::resultToString(result));
  }
}

void CommandParser::cmdClearQueue() {
  Services::QueueResult result = Services::g_commandQueue.clearQueue();
  if (result == Services::QueueResult::OK) {
    respondOk("CLEARED");
  } else {
    respondErr(Services::resultToString(result));
  }
}

// ============================================================================
// Timing/diagnostics command handlers
// ============================================================================

void CommandParser::cmdPing(const ParsedCommand &cmd) {
  // Capture RX timestamp (ideally would be captured at byte reception, but
  // capturing at dispatch start is a reasonable approximation)
  uint32_t rx_tick = Services::TickTimer_GetTick();

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
  uint32_t tx_tick = Services::TickTimer_GetTick();

  // Get current state
  Services::ControllerState state = Services::g_commandQueue.getState();

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"rx_tick\":%lu,\"tx_tick\":%lu,\"state\":\"%s\"}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick),
             Services::stateToString(state));
    respondJsonOk("PING", buf);
  } else {
    // ASCII format: PONG <seq> <mcu_rx_tick> <mcu_tx_tick> <state>
    char buf[80];
    snprintf(buf, sizeof(buf), "PONG %lu %lu %lu %s",
             static_cast<unsigned long>(seq), static_cast<unsigned long>(rx_tick),
             static_cast<unsigned long>(tx_tick), Services::stateToString(state));
    m_transport.println(buf);
  }
}

void CommandParser::cmdGetTick() {
  uint32_t tick = Services::TickTimer_GetTick();

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
  uint32_t tick = Services::TickTimer_GetTick();
  Services::ControllerState state = Services::g_commandQueue.getState();
  size_t queueDepth = Services::g_commandQueue.getQueueDepth();
  Services::ControlMode mode = Services::g_controlMode.getMode();
  Services::EncoderStatus encStatus =
      Services::g_controlMode.getEncoderStatus();

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
    const auto& faultEn = Services::g_motorConfig.getFaultEnable();
    bool ocdEn       = ocd && faultEn.ocd;
    bool thermalSDEn = thermalSD && faultEn.thermalSD;
    bool thermalWarnEn = thermalWarn && faultEn.thermalWarn;
    bool uvloEn      = uvlo && faultEn.uvlo;
    bool stallAEn    = stallA && faultEn.stallA && snap.motor.speed > 0;
    bool stallBEn    = stallB && faultEn.stallB && snap.motor.speed > 0;
    bool cmdErrEn    = cmdErr && faultEn.cmdErr;
    bool anyError    = cmdErrEn || uvloEn || thermalSDEn || thermalWarnEn
                       || stallAEn || stallBEn || ocdEn;

    // Get heartbeat watchdog status
    Services::Safety::HeartbeatStatus hbStatus;
    Services::Safety::getHeartbeatStatus(hbStatus);
    bool hbEnabled = hbStatus.enabled;
    uint32_t hbTimeout = hbStatus.timeoutMs;
    uint32_t hbRemaining = hbStatus.remainingMs;
    bool hbTimedOut = hbStatus.timedOut;

    // Bypass respondJsonOk (256-byte buffer too small) — format full envelope
    // newlib-nano: no %lld — pre-format int64_t as string
    char encCountStr[24];
    i64toa(snap.encoder.count, encCountStr, sizeof(encCountStr));

    char buf[700];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"command\":\"GET_STATUS\",\"data\":"
             "{\"state\":\"%s\",\"tick\":%lu,\"queue_depth\":%u,"
             "\"motor\":{\"position\":%ld,\"speed\":%lu,\"busy\":%s,\"hi_z\":%s,"
             "\"direction\":%d,\"mot_status\":%u,\"status_reg\":\"%04X\","
             "\"cmd_err\":%s,\"ocd\":%s,\"thermal_sd\":%s,"
             "\"thermal_warn\":%s,\"uvlo\":%s,\"stall_a\":%s,\"stall_b\":%s},"
             "\"encoder\":{\"count\":%s,\"velocity\":%ld,\"index_seen\":%s,"
             "\"revolutions\":%ld,\"index_period_us\":%lu},"
             "\"heartbeat\":{\"enabled\":%s,\"timeout_ms\":%lu,"
             "\"remaining_ms\":%lu,\"timed_out\":%s},"
             "\"mode\":\"%s\",\"encoder_status\":\"%s\",\"error\":%s}}",
             Services::stateToString(state),
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
             hbEnabled ? "true" : "false",
             static_cast<unsigned long>(hbTimeout),
             static_cast<unsigned long>(hbRemaining),
             hbTimedOut ? "true" : "false",
             Services::modeToString(mode),
             Services::encoderStatusToString(encStatus),
             anyError ? "true" : "false");
    m_transport.println(buf);
  } else {
    // ASCII format: STATUS <state> <tick> <queue_depth> <mode> <encoder_status> <position> <velocity>
    char buf[96];
    snprintf(buf, sizeof(buf), "STATUS %s %lu %u %s %s %ld %lu",
             Services::stateToString(state), static_cast<unsigned long>(tick),
             static_cast<unsigned>(queueDepth), Services::modeToString(mode),
             Services::encoderStatusToString(encStatus),
             static_cast<long>(snap.motor.position),
             static_cast<unsigned long>(snap.motor.speed));
    m_transport.println(buf);
  }
}

void CommandParser::cmdClearFault() {
  char faultBuf[80] = {};
  auto result = Services::Safety::clearFault(faultBuf, sizeof(faultBuf));
  if (result == Services::Safety::Result::OK) {
    respondOk("IDLE");
  } else if (result == Services::Safety::Result::FAULT_ACTIVE) {
    respondErr(faultBuf);
  } else {
    respondErr("Not in fault/estop state");
  }
}

void CommandParser::cmdForceClearFault() {
  auto result = Services::Safety::forceClearFault();
  if (result == Services::Safety::Result::OK) {
    respondOk("IDLE");
  } else {
    respondErr("Not in fault/estop state");
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

  Services::Safety::heartbeatReceived(seq);

  Services::Safety::HeartbeatStatus hb;
  Services::Safety::getHeartbeatStatus(hb);

  uint32_t mcu_tick = Services::TickTimer_GetTick();

  if (m_format == ResponseFormat::JSON) {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"seq\":%lu,\"mcu_tick\":%lu,\"remaining_ms\":%lu}",
             static_cast<unsigned long>(seq),
             static_cast<unsigned long>(mcu_tick),
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

  uint32_t requested = static_cast<uint32_t>(atol(cmd.args[0]));
  uint32_t accepted = Services::Safety::setHeartbeatTimeout(requested);

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
  Services::Safety::HeartbeatStatus hb;
  Services::Safety::getHeartbeatStatus(hb);

  if (m_format == ResponseFormat::JSON) {
    char buf[160];
    snprintf(buf, sizeof(buf),
             "{\"enabled\":%s,\"timeout_ms\":%lu,\"last_seq\":%lu,"
             "\"remaining_ms\":%lu,\"timed_out\":%s}",
             hb.enabled ? "true" : "false",
             static_cast<unsigned long>(hb.timeoutMs),
             static_cast<unsigned long>(hb.lastSeq),
             static_cast<unsigned long>(hb.remainingMs),
             hb.timedOut ? "true" : "false");
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
  uint16_t deviceId = Services::g_deviceConfig.getDeviceId();
  Services::WheelRole role = Services::g_deviceConfig.getRole();
  snprintf(buf, sizeof(buf), "Device: %u (%s)", static_cast<unsigned>(deviceId),
           Services::roleToString(role));
  m_transport.println(buf);

  // Show control mode
  Services::ControlMode mode = Services::g_controlMode.getMode();
  Services::EncoderStatus encStatus =
      Services::g_controlMode.getEncoderStatus();
  snprintf(buf, sizeof(buf), "Mode: %s (encoder: %s)",
           Services::modeToString(mode),
           Services::encoderStatusToString(encStatus));
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

  auto result = Services::Motion::home(speedOverride);
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdZero() {
  auto result = Services::Motion::zero();
  if (result == Services::Motion::Result::OK) {
    respondOk("");
  } else {
    respondErr(Services::Motion::resultToString(result));
  }
}

void CommandParser::cmdEncoderZero() {
  Tasks::EncoderTask_ResetCount();
  respondOk("ENCODER_ZEROED");
}

void CommandParser::cmdZeroAll() {
  auto result = Services::Motion::zero();
  Tasks::EncoderTask_ResetCount();
  if (result == Services::Motion::Result::OK) {
    respondOk("ALL_ZEROED");
  } else {
    respondErr(Services::Motion::resultToString(result));
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

// Apply encoder filter and persist to flash
static void applyAndPersistFilter(uint8_t type, uint8_t param) {
  Tasks::EncoderTask_SetFilter(type, param);
  Services::g_motorConfig.setEncFilter(type, param);
  Services::g_motorConfig.saveToFlash();
}

void CommandParser::cmdEncFilter(const ParsedCommand &cmd) {
  // Query mode: no arguments
  if (cmd.argCount < 1) {
    uint8_t type = 0, param = 0;
    Tasks::EncoderTask_GetFilter(type, param);
    char buf[64];
    if (m_format == ResponseFormat::JSON) {
      const char *typeName = (type == 1) ? "EMA" : (type == 2) ? "SMA" : "NONE";
      snprintf(buf, sizeof(buf),
               "{\"filter_type\":\"%s\",\"param\":%u}", typeName, (unsigned)param);
      respondJsonOk(m_currentCmd, buf);
    } else {
      if (type == 1) {
        snprintf(buf, sizeof(buf), "EMA alpha=%u", (unsigned)param);
      } else if (type == 2) {
        snprintf(buf, sizeof(buf), "SMA window=%u", (unsigned)param);
      } else {
        snprintf(buf, sizeof(buf), "NONE");
      }
      respondOk(buf);
    }
    return;
  }

  // Check if first arg is a filter type name
  if (strcmp(cmd.args[0], "NONE") == 0) {
    applyAndPersistFilter(0, 0);
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
    applyAndPersistFilter(1, static_cast<uint8_t>(val));
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
    applyAndPersistFilter(2, static_cast<uint8_t>(val));
    char buf[48];
    snprintf(buf, sizeof(buf), "SMA window=%u", (unsigned)val);
    respondOk(buf);
    return;
  }

  // Legacy: bare number treated as EMA alpha (backward compatible)
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

// ============================================================================
// Device identification command handlers
// ============================================================================

void CommandParser::cmdGetDeviceId() {
  uint16_t deviceId = Services::g_deviceConfig.getDeviceId();
  Services::WheelRole role = Services::g_deviceConfig.getRole();

  char buf[32];
  snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(deviceId),
           Services::roleToString(role));
  respondOk(buf);
}

void CommandParser::cmdSetDeviceId(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_DEVICE_ID <id>");
    return;
  }

  uint16_t deviceId = static_cast<uint16_t>(strtoul(cmd.args[0], nullptr, 10));

  if (Services::g_deviceConfig.setDeviceId(deviceId)) {
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

  Services::WheelRole role = Services::parseRole(cmd.args[0]);

  if (Services::g_deviceConfig.setRole(role)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Role=%s", Services::roleToString(role));
    respondOk(buf);
  } else {
    respondErr("Flash write failed");
  }
}

// ============================================================================
// Control mode command handlers
// ============================================================================

void CommandParser::cmdGetMode() {
  Services::ControlMode mode = Services::g_controlMode.getMode();
  Services::EncoderStatus encStatus =
      Services::g_controlMode.getEncoderStatus();

  char buf[48];
  snprintf(buf, sizeof(buf), "%s encoder=%s", Services::modeToString(mode),
           Services::encoderStatusToString(encStatus));
  respondOk(buf);
}

void CommandParser::cmdSetMode(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: SET_MODE <OPEN_LOOP|CLOSED_LOOP>");
    return;
  }

  Services::ControlMode mode = Services::parseMode(cmd.args[0]);

  // Attempt to set mode (will fail if CLOSED_LOOP requested without encoder)
  if (Services::g_controlMode.setMode(mode)) {
    respondOk(Services::modeToString(mode));
  } else {
    // Mode change failed
    if (mode == Services::ControlMode::CLOSED_LOOP) {
      Services::EncoderStatus encStatus =
          Services::g_controlMode.getEncoderStatus();
      char buf[64];
      snprintf(buf, sizeof(buf), "Cannot enter CLOSED_LOOP: encoder %s",
               Services::encoderStatusToString(encStatus));
      respondErr(buf);
    } else {
      respondErr("Mode change failed");
    }
  }
}

void CommandParser::cmdGetEncoderStatus() {
  Services::EncoderStatus status = Services::g_controlMode.getEncoderStatus();

  char buf[80];
  if (status == Services::EncoderStatus::READY &&
      Tasks::EncoderTask_IsAvailable()) {
    // Include encoder data if available
    Tasks::EncoderState state = Tasks::EncoderTask_GetState();
    snprintf(buf, sizeof(buf), "status=%s count=%ld vel=%ld idx=%d",
             Services::encoderStatusToString(status),
             static_cast<long>(state.count), static_cast<long>(state.velocity),
             state.indexSeen ? 1 : 0);
  } else {
    snprintf(buf, sizeof(buf), "status=%s",
             Services::encoderStatusToString(status));
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
  const auto& cfg = Services::g_motorConfig.getConfig();
  bool valid = Services::g_motorConfig.isValid();

  char buf[384];
  snprintf(buf, sizeof(buf),
           "Motor Config %s\n"
           "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
           "OCD_TH=%02X STALL_TH=%02X\n"
           "ACC=%u DEC=%u MAXSPD=%u MINSPD=%u FS_SPD=%u\n"
           "Faults: OCD=%d TH_SD=%d TH_W=%d UVLO=%d STALL_A=%d STALL_B=%d CMD=%d\n"
           "Action: %s\n"
           "StepMode: %u (1/%u)",
           valid ? "(from flash)" : "(defaults)",
           (unsigned)cfg.kvalHold, (unsigned)cfg.kvalRun,
           (unsigned)cfg.kvalAcc, (unsigned)cfg.kvalDec,
           (unsigned)cfg.ocdThreshold, (unsigned)cfg.stallThreshold,
           (unsigned)cfg.acceleration, (unsigned)cfg.deceleration,
           (unsigned)cfg.maxSpeed, (unsigned)cfg.minSpeed, (unsigned)cfg.fsSpeed,
           cfg.faultEnable.ocd, cfg.faultEnable.thermalSD,
           cfg.faultEnable.thermalWarn, cfg.faultEnable.uvlo,
           cfg.faultEnable.stallA, cfg.faultEnable.stallB, cfg.faultEnable.cmdErr,
           cfg.faultAction == 0 ? "HardStop" :
           cfg.faultAction == 1 ? "HardHiZ" : "SoftStop",
           (unsigned)(cfg.stepMode & 0x07), 1U << (cfg.stepMode & 0x07));
  m_transport.println(buf);
}

void CommandParser::cmdMotorConfigSave() {
  if (Services::g_motorConfig.saveToFlash()) {
    respondOk("Config saved to flash");
  } else {
    respondErr("Flash write failed");
  }
}

void CommandParser::cmdMotorConfigLoad() {
  if (Services::g_motorConfig.loadFromFlash()) {
    respondOk("Config loaded from flash");
  } else {
    respondErr("No valid config in flash");
  }
}

void CommandParser::cmdMotorConfigReset() {
  if (Services::g_motorConfig.factoryReset()) {
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

  Services::g_motorConfig.setKval(hold, run, acc, dec);

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

  Services::g_motorConfig.setOcdThreshold(thresh);

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

  Services::g_motorConfig.setStallThreshold(thresh);

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
    Services::g_motorConfig.setFaultAction(action);
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

  Services::FaultEnableFlags flags = {};
  flags.ocd = (strtoul(cmd.args[0], nullptr, 10) != 0) ? 1 : 0;
  flags.thermalSD = (strtoul(cmd.args[1], nullptr, 10) != 0) ? 1 : 0;
  flags.thermalWarn = (strtoul(cmd.args[2], nullptr, 10) != 0) ? 1 : 0;
  flags.uvlo = (strtoul(cmd.args[3], nullptr, 10) != 0) ? 1 : 0;
  flags.stallA = (strtoul(cmd.args[4], nullptr, 10) != 0) ? 1 : 0;
  flags.stallB = (strtoul(cmd.args[5], nullptr, 10) != 0) ? 1 : 0;
  flags.cmdErr = (strtoul(cmd.args[6], nullptr, 10) != 0) ? 1 : 0;

  Services::g_motorConfig.setFaultEnable(flags);
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

  Services::g_motorConfig.setMotionParams(acc, dec, maxSpd);

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

  Services::g_motorConfig.setStepMode(mode);

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
  if (!Services::g_motorConfig.saveToFlash()) {
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

  Services::Event::enable(mask, currentStatus);

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
  Services::Event::disable();
  respondOk("");
}

void CommandParser::cmdEventStatus() {
  Services::Event::Stats st = Services::Event::getStats();
  uint32_t lastSeq = Tasks::CommsTask_GetLastEventSeq();

  if (m_format == ResponseFormat::JSON) {
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"mask\":%u,\"sent\":%lu,\"lost_critical\":%lu,\"lost_info\":%lu,"
             "\"last_seq\":%lu,\"depth\":%u}",
             static_cast<unsigned>(st.enableMask),
             static_cast<unsigned long>(st.sent),
             static_cast<unsigned long>(st.lostCritical),
             static_cast<unsigned long>(st.lostInfo),
             static_cast<unsigned long>(lastSeq),
             static_cast<unsigned>(st.queueDepth));
    respondJsonOk(m_currentCmd, buf);
  } else {
    char buf[128];
    snprintf(buf, sizeof(buf),
             "EVENT_STATUS mask=%u sent=%lu lost_critical=%lu lost_info=%lu last_seq=%lu depth=%u",
             static_cast<unsigned>(st.enableMask),
             static_cast<unsigned long>(st.sent),
             static_cast<unsigned long>(st.lostCritical),
             static_cast<unsigned long>(st.lostInfo),
             static_cast<unsigned long>(lastSeq),
             static_cast<unsigned>(st.queueDepth));
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

  // Read from flash and stream to LCD in chunks
  constexpr uint32_t CHUNK_SIZE = 512;
  uint8_t chunk[CHUNK_SIZE];
  uint32_t offset = 0;
  uint32_t nonZeroCount = 0;
  uint32_t nonFFCount = 0;
  uint8_t first4[4] = {0};
  bool gotFirst = false;

  while (offset < Services::FLASH_IMAGE_SIZE) {
    uint32_t remaining = Services::FLASH_IMAGE_SIZE - offset;
    uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

    if (!svc.readSlotChunk(slot, offset, chunk, toRead)) {
      Tasks::DisplayTask_StreamBitmapEnd();
      respondErr("Flash read failed");
      return;
    }

    // Capture first 4 bytes and count non-trivial data
    if (!gotFirst) {
      for (uint32_t i = 0; i < 4 && i < toRead; i++) first4[i] = chunk[i];
      gotFirst = true;
    }
    for (uint32_t i = 0; i < toRead; i++) {
      if (chunk[i] != 0x00) nonZeroCount++;
      if (chunk[i] != 0xFF) nonFFCount++;
    }

    Tasks::DisplayTask_StreamBitmapData(chunk, toRead);
    offset += toRead;
  }

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

} // namespace Comms
