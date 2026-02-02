/**
 * @file command_parser.cpp
 * @brief ASCII command protocol parser implementation
 *
 * Supports synchronized multi-controller operation.
 * See docs/HOST_INTERFACE_AND_SYNC.md for protocol specification.
 */

#include "comms/command_parser.hpp"
#include "services/command_queue.hpp"
#include "services/control_mode.hpp"
#include "services/device_config.hpp"
#include "services/tick_timer.hpp"
#include "tasks/display_task.hpp"
#include "tasks/encoder_task.hpp"
#include "tasks/motor_task.hpp"
#include "ui/ui_mode.hpp"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Comms {

CommandParser::CommandParser(ITransport &transport)
    : m_transport(transport), m_bufIndex(0) {
  memset(m_buffer, 0, sizeof(m_buffer));
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
  // Motion commands
  if (strcmp(cmd.cmd, "MOVE") == 0) {
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
  // Timing/diagnostics commands
  else if (strcmp(cmd.cmd, "PING") == 0) {
    cmdPing(cmd);
  } else if (strcmp(cmd.cmd, "GET_TICK") == 0) {
    cmdGetTick();
  } else if (strcmp(cmd.cmd, "GET_STATUS") == 0) {
    cmdGetStatus();
  } else if (strcmp(cmd.cmd, "CLEAR_FAULT") == 0) {
    cmdClearFault();
  }
  // Utility commands
  else if (strcmp(cmd.cmd, "HELP") == 0 || strcmp(cmd.cmd, "?") == 0) {
    cmdHelp();
  } else if (strcmp(cmd.cmd, "VER") == 0 || strcmp(cmd.cmd, "VERSION") == 0) {
    cmdVersion();
  } else if (strcmp(cmd.cmd, "HOME") == 0) {
    cmdHome();
  } else if (strcmp(cmd.cmd, "ZERO") == 0) {
    cmdZero();
  } else if (strcmp(cmd.cmd, "ENCODER") == 0 || strcmp(cmd.cmd, "ENC") == 0) {
    cmdEncoder();
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
  } else {
    respondErr("Unknown command. Type HELP for list.");
  }
}

void CommandParser::respondOk(const char *msg) {
  m_transport.print("OK ");
  m_transport.println(msg);
}

void CommandParser::respondErr(const char *msg) {
  m_transport.print("ERROR ");
  m_transport.println(msg);
}

void CommandParser::respondData(const char **lines, size_t count) {
  for (size_t i = 0; i < count; i++) {
    m_transport.println(lines[i]);
  }
}

// ============================================================================
// Motion command handlers
// ============================================================================

void CommandParser::cmdMove(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: MOVE <steps> <dir>");
    return;
  }
  // TODO: Parse steps/dir and send to MotorTask queue
  respondOk("");
}

void CommandParser::cmdGoTo(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: GOTO <position>");
    return;
  }
  // TODO: Parse position and send to MotorTask queue
  respondOk("");
}

void CommandParser::cmdRun(const ParsedCommand &cmd) {
  if (cmd.argCount < 2) {
    respondErr("Usage: RUN <speed> <dir>");
    return;
  }
  // TODO: Parse speed/dir and send to MotorTask queue
  respondOk("");
}

void CommandParser::cmdStop(const ParsedCommand &cmd) {
  // Optional "hard" argument
  bool hard = (cmd.argCount > 0 && strcmp(cmd.args[0], "hard") == 0);
  (void)hard;
  // TODO: Send stop to MotorTask queue
  respondOk("");
}

void CommandParser::cmdEstop() {
  // Emergency stop - halt motion, disable outputs, set ESTOP state
  Services::g_commandQueue.emergencyStop();
  respondOk("ESTOP");
}

// ============================================================================
// Configuration command handlers
// ============================================================================

void CommandParser::cmdEnable() {
  // TODO: Enable motor outputs
  respondOk("");
}

void CommandParser::cmdDisable() {
  // TODO: Disable motor outputs (Hi-Z)
  respondOk("");
}

void CommandParser::cmdAccel(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: ACCEL <value>");
    return;
  }
  // TODO: Set acceleration
  respondOk("");
}

void CommandParser::cmdDecel(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: DECEL <value>");
    return;
  }
  // TODO: Set deceleration
  respondOk("");
}

void CommandParser::cmdMaxSpd(const ParsedCommand &cmd) {
  if (cmd.argCount < 1) {
    respondErr("Usage: MAXSPD <value>");
    return;
  }
  // TODO: Set max speed
  respondOk("");
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
    motorCmd.type = Tasks::MotorCmdType::Move;
    int32_t steps = static_cast<int32_t>(atol(cmd.args[1]));
    int32_t dir = static_cast<int32_t>(atol(cmd.args[2]));
    motorCmd.param1 = (dir == 0) ? -steps : steps;
  } else if (strcmp(subCmd, "GOTO") == 0 || strcmp(subCmd, "goto") == 0) {
    if (cmd.argCount < 2) {
      respondErr("Usage: QUEUE GOTO <position>");
      return;
    }
    motorCmd.type = Tasks::MotorCmdType::GoTo;
    motorCmd.param1 = static_cast<int32_t>(atol(cmd.args[1]));
  } else if (strcmp(subCmd, "RUN") == 0 || strcmp(subCmd, "run") == 0) {
    if (cmd.argCount < 3) {
      respondErr("Usage: QUEUE RUN <speed> <dir>");
      return;
    }
    motorCmd.type = Tasks::MotorCmdType::Run;
    motorCmd.param1 = static_cast<int32_t>(atol(cmd.args[1]));
    motorCmd.param2 = static_cast<int32_t>(atol(cmd.args[2]));
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

  // Format: PONG <seq> <mcu_rx_tick> <mcu_tx_tick> <state>
  char buf[80];
  snprintf(buf, sizeof(buf), "PONG %lu %lu %lu %s",
           static_cast<unsigned long>(seq), static_cast<unsigned long>(rx_tick),
           static_cast<unsigned long>(tx_tick), Services::stateToString(state));
  m_transport.println(buf);
}

void CommandParser::cmdGetTick() {
  // Return current tick counter value in microseconds
  // Format: OK <tick>
  uint32_t tick = Services::TickTimer_GetTick();
  char buf[32];
  snprintf(buf, sizeof(buf), "OK %lu", static_cast<unsigned long>(tick));
  m_transport.println(buf);
}

void CommandParser::cmdGetStatus() {
  // Return full status
  // Format: STATUS <state> <tick> <queue_depth> <mode> <encoder_status>
  // <position> <velocity>
  uint32_t tick = Services::TickTimer_GetTick();
  Services::ControllerState state = Services::g_commandQueue.getState();
  size_t queueDepth = Services::g_commandQueue.getQueueDepth();
  Services::ControlMode mode = Services::g_controlMode.getMode();
  Services::EncoderStatus encStatus =
      Services::g_controlMode.getEncoderStatus();

  char buf[96];
  // TODO: Get actual position and velocity from motor telemetry
  snprintf(buf, sizeof(buf), "STATUS %s %lu %u %s %s 0 0",
           Services::stateToString(state), static_cast<unsigned long>(tick),
           static_cast<unsigned>(queueDepth), Services::modeToString(mode),
           Services::encoderStatusToString(encStatus));
  m_transport.println(buf);
}

void CommandParser::cmdClearFault() {
  Services::QueueResult result = Services::g_commandQueue.clearFault();
  if (result == Services::QueueResult::OK) {
    respondOk("IDLE");
  } else {
    respondErr(Services::resultToString(result));
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
  m_transport.println("UI/Display:");
  m_transport.println("  UI_MODE [LOCAL|REMOTE] - Get/set UI mode");
  m_transport.println("  DISP_CLEAR [color]  - Clear display");
  m_transport.println("  DISP_TEXT x y fg bg text");
  m_transport.println("  DISP_RECT x y w h color [fill]");
  m_transport.println("  DISP_LINE x1 y1 x2 y2 color");
  m_transport.println("  DISP_BITMAP_B64 x y w h b64data");
  m_transport.println("Utility:");
  m_transport.println("  ENCODER             - Encoder data");
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

void CommandParser::cmdHome() {
  // TODO: Send home command to MotorTask queue
  respondOk("");
}

void CommandParser::cmdZero() {
  // TODO: Send zero/reset position to MotorTask queue
  respondOk("");
}

void CommandParser::cmdEncoder() {
  // Check if encoder is available
  if (!Tasks::EncoderTask_IsAvailable()) {
    respondErr("Encoder not available");
    return;
  }

  // Get encoder state
  Tasks::EncoderState state = Tasks::EncoderTask_GetState();

  // Format: OK count=<n> vel=<n> idx=<0|1> idx_tick=<n>
  char buf[64];
  snprintf(buf, sizeof(buf), "count=%ld vel=%ld idx=%d idx_tick=%lu",
           static_cast<long>(state.count), static_cast<long>(state.velocity),
           state.indexSeen ? 1 : 0,
           static_cast<unsigned long>(state.indexTick));
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

  // Usage: DISP_BITMAP <x> <y> <w> <h>
  // Response: OK READY, then host sends raw binary RGB565 data
  if (cmd.argCount < 4) {
    respondErr("Usage: DISP_BITMAP <x> <y> <w> <h>");
    return;
  }

  // TODO: Implement binary streaming mode
  // For now, return not implemented
  respondErr("Binary streaming not yet implemented. Use DISP_BITMAP_B64.");
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

} // namespace Comms
