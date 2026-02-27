/**
 * @file command_parser.hpp
 * @brief ASCII command protocol parser
 *
 * Line-based protocol:
 *   Command: <CMD> [ARG1] [ARG2]...\n
 *   Response: OK [<data>]\n or ERROR <code> [<msg>]\n
 *
 * Supports synchronized multi-controller operation:
 *   - Command queuing (QUEUE)
 *   - Arm/Start synchronization (ARM, START, START_AT)
 *   - Latency measurement (PING/PONG with timestamps)
 *   - Tick counter queries (GET_TICK)
 *
 * See docs/HOST_INTERFACE_AND_SYNC.md for protocol specification.
 */

#ifndef COMMAND_PARSER_HPP
#define COMMAND_PARSER_HPP

#include "L1_transport/transport_interface.hpp"
#include <stdint.h>

namespace Comms {

// Maximum command line length
constexpr size_t CMD_BUFFER_SIZE = 128;

// Maximum number of arguments
constexpr size_t MAX_ARGS = 8;

// Command queue depth
constexpr size_t CMD_QUEUE_DEPTH = 8;

/**
 * @brief Error codes for command responses
 */
enum class ErrorCode : uint8_t {
  OK = 0,
  INVALID_CMD,
  INVALID_PARAM,
  QUEUE_FULL,
  QUEUE_EMPTY,
  NOT_ARMED,
  ALREADY_RUNNING,
  DRIVER_FAULT,
  STALL,
  OVERCURRENT
};

/**
 * @brief Controller state machine states
 */
enum class ControllerState : uint8_t {
  IDLE,     // Ready for commands
  ARMED,    // Commands queued, awaiting START
  RUNNING,  // Motion in progress
  STOPPING, // Decelerating
  FAULT,    // Error, requires CLEAR_FAULT
  ESTOP     // Emergency stop, outputs disabled
};

/**
 * @brief Response format selection
 */
enum class ResponseFormat : uint8_t {
  ASCII, // Traditional OK/ERR format (default)
  JSON   // JSON structured responses
};

/**
 * @brief Parsed command structure
 */
struct ParsedCommand {
  char cmd[24];            // Command word (uppercase) - sized for SCPI names (e.g. "UI:DISP:BITMAP:B64")
  char args[MAX_ARGS][32]; // Arguments as strings
  uint8_t argCount;        // Number of arguments
  bool valid;              // Parse succeeded
};

/**
 * @brief Command parser and dispatcher
 */
class CommandParser {
public:
  /**
   * @brief Construct parser with transport
   * @param transport Transport interface for I/O
   */
  explicit CommandParser(ITransport &transport);

  /**
   * @brief Process incoming data (call periodically)
   *
   * Reads available bytes, builds command line,
   * parses and dispatches complete commands.
   */
  void process();

  /**
   * @brief Send OK response (format-aware)
   * @param msg Response message (ignored in JSON mode)
   */
  void respondOk(const char *msg);

  /**
   * @brief Send error response (format-aware)
   * @param msg Error message
   */
  void respondErr(const char *msg);

  /**
   * @brief Send multi-line data response
   * @param lines Array of line strings
   * @param count Number of lines
   */
  void respondData(const char **lines, size_t count);

  /**
   * @brief Send JSON success response
   * @param command Echoed command name
   * @param dataJson JSON object string for data field (or nullptr)
   */
  void respondJsonOk(const char *command, const char *dataJson = nullptr);

  /**
   * @brief Send JSON error response
   * @param command Echoed command name
   * @param code Error code string
   * @param message Human-readable error message
   */
  void respondJsonErr(const char *command, const char *code, const char *message);

  /**
   * @brief Get current response format
   */
  ResponseFormat getFormat() const { return m_format; }

  /**
   * @brief Check baud rate auto-revert timeout
   *
   * Call periodically from comms task. If a baud change was made
   * and no valid command has been received within 2 seconds,
   * reverts to the previous baud rate.
   */
  void checkBaudRevert();

  /**
   * @brief Set response format
   */
  void setFormat(ResponseFormat format) { m_format = format; }

private:
  ITransport &m_transport;
  char m_buffer[CMD_BUFFER_SIZE];
  size_t m_bufIndex;
  ResponseFormat m_format;       // Current response format (ASCII or JSON)
  char m_currentCmd[32];         // Current command being processed (for JSON echo)

  /**
   * @brief Parse command line into ParsedCommand
   * @param line Null-terminated command line
   * @return Parsed command structure
   */
  ParsedCommand parse(const char *line);

  /**
   * @brief Dispatch parsed command to appropriate handler
   * @param cmd Parsed command
   */
  void dispatch(const ParsedCommand &cmd);

  // Namespace dispatch handlers (SCPI two-level dispatch)
  void dispatchMotion(const char *suffix, const ParsedCommand &cmd);
  void dispatchSystem(const char *suffix, const ParsedCommand &cmd);
  void dispatchSync(const char *suffix, const ParsedCommand &cmd);
  void dispatchDiag(const char *suffix, const ParsedCommand &cmd);
  void dispatchCtrl(const char *suffix, const ParsedCommand &cmd);
  void dispatchDevice(const char *suffix, const ParsedCommand &cmd);
  void dispatchUI(const char *suffix, const ParsedCommand &cmd);
  void dispatchDebug(const char *suffix, const ParsedCommand &cmd);
  void dispatchDriver(const char *suffix, const ParsedCommand &cmd);
  void dispatchLegacy(const ParsedCommand &cmd);

  // Motion command handlers
  void cmdMove(const ParsedCommand &cmd);
  void cmdGoTo(const ParsedCommand &cmd);
  void cmdRun(const ParsedCommand &cmd);
  void cmdStop(const ParsedCommand &cmd);
  void cmdEstop();

  // Configuration command handlers
  void cmdEnable();
  void cmdDisable();
  void cmdAccel(const ParsedCommand &cmd);       // Legacy: raw register values
  void cmdDecel(const ParsedCommand &cmd);       // Legacy: raw register values
  void cmdMaxSpd(const ParsedCommand &cmd);      // Legacy: raw register values
  void cmdAccelPhysical(const ParsedCommand &cmd);  // SCPI: steps/s^2
  void cmdDecelPhysical(const ParsedCommand &cmd);  // SCPI: steps/s^2
  void cmdMaxSpdPhysical(const ParsedCommand &cmd); // SCPI: steps/s

  // Synchronization command handlers
  void cmdQueue(const ParsedCommand &cmd);
  void cmdArm();
  void cmdStart();
  void cmdStartAt(const ParsedCommand &cmd);
  void cmdClearQueue();

  // Timing/diagnostics command handlers
  void cmdPing(const ParsedCommand &cmd);
  void cmdGetTick();
  void cmdGetStatus();
  void cmdClearFault();
  void cmdForceClearFault();

  // Heartbeat command handlers
  void cmdHeartbeat(const ParsedCommand &cmd);
  void cmdSetHeartbeat(const ParsedCommand &cmd);
  void cmdGetHeartbeatStatus();

  // Utility command handlers
  void cmdHelp();
  void cmdVersion();
  void cmdHome(const ParsedCommand &cmd);
  void cmdZero();
  void cmdEncoderZero();
  void cmdZeroAll();
  void cmdEncoder();
  void cmdEncDebug();
  void cmdEncFilter(const ParsedCommand &cmd);
  void cmdEncFilterSub(const char *sub, const ParsedCommand &cmd);

  // Device identification command handlers
  void cmdGetDeviceId();
  void cmdSetDeviceId(const ParsedCommand &cmd);
  void cmdSetRole(const ParsedCommand &cmd);

  // Control mode command handlers
  void cmdGetMode();
  void cmdSetMode(const ParsedCommand &cmd);
  void cmdGetEncoderStatus();

  // Response format command handlers
  void cmdSetFormat(const ParsedCommand &cmd);
  void cmdGetFormat();

  // Baud rate negotiation
  void cmdSetBaud(const ParsedCommand &cmd);

  // Baud auto-revert state
  uint32_t m_baudRevertRate = 0;     // 0 = no pending revert
  uint32_t m_baudRevertDeadline = 0; // tick count deadline

  // UI mode command handlers
  void cmdUIMode(const ParsedCommand &cmd);
  void cmdUIGetMode();

  // Display command handlers (remote rendering)
  void cmdDispClear(const ParsedCommand &cmd);
  void cmdDispText(const ParsedCommand &cmd);
  void cmdDispRect(const ParsedCommand &cmd);
  void cmdDispLine(const ParsedCommand &cmd);
  void cmdDispBitmap(const ParsedCommand &cmd);
  void cmdDispBitmapB64(const ParsedCommand &cmd);
  void cmdDispIndicator(const ParsedCommand &cmd);
  void cmdDispBitmapRle(const ParsedCommand &cmd);

  // Flash image command handlers (delegate to FlashImageService)
  void cmdFlashInfo();
  void cmdFlashUpload(const ParsedCommand &cmd);
  void cmdFlashUploadRle(const ParsedCommand &cmd);
  void cmdFlashShow(const ParsedCommand &cmd);
  void cmdFlashEraseAll();
  void cmdFlashDump(const ParsedCommand &cmd);
  void cmdFlashTest();

  /**
   * @brief Check if last argument of a command is "CRC"
   * @return true if CRC mode requested
   */
  bool hasCrcFlag(const ParsedCommand &cmd) const;

  /**
   * @brief Receive 4-byte CRC and verify against computed value
   * @param computedCrc Running CRC state (pre-finalized, i.e. before XOR)
   * @return true if CRC matches
   */
  bool verifyCrc(uint32_t computedCrc);

  // Debug command handlers
  void cmdMotorDebug();

  // Trace command handlers
  void cmdTraceDump();
  void cmdTraceReset();

  // Motor configuration command handlers
  void cmdMotorConfigShow();
  void cmdMotorConfigSave();
  void cmdMotorConfigLoad();
  void cmdMotorConfigReset();
  void cmdMotorConfigKval(const ParsedCommand &cmd);
  void cmdMotorConfigOcd(const ParsedCommand &cmd);
  void cmdMotorConfigStall(const ParsedCommand &cmd);
  void cmdMotorConfigFault(const ParsedCommand &cmd);
  void cmdMotorConfigMotion(const ParsedCommand &cmd);
  void cmdMotorConfigStepMode(const ParsedCommand &cmd);
  void cmdMotorConfigApply();

  // Event command handlers (see docs/PROTOCOL_EVENTS_V1.md)
  void cmdEventEnable(const ParsedCommand &cmd);
  void cmdEventDisable();
  void cmdEventStatus();

  // Phase A: unit model + safety commands
  void cmdDrvStepMode(const ParsedCommand &cmd);   // Hi-Z safe step mode change
  void cmdDrvStepModeQuery();                       // Read step mode from hardware
  void cmdDrvFullSteps(const ParsedCommand &cmd);   // Set full steps/rev
  void cmdDrvFullStepsQuery();                      // Query full steps/rev
  void cmdDrvEncoderPPR(const ParsedCommand &cmd);  // Set encoder PPR
  void cmdDrvEncoderPPRQuery();                     // Query encoder PPR
  void cmdSystDelay(const ParsedCommand &cmd);      // Set transport delay
  void cmdSystDelayQuery();                         // Query transport delay
  void cmdFollowingError();                         // CTRL:FOLLOW? — supervisor status
  void cmdFollowThreshQuery();                      // CTRL:FOLLOW:THRESH?
  void cmdFollowThreshSet(const ParsedCommand &cmd);// CTRL:FOLLOW:THRESH <params>
  void cmdFollowSave();                             // CTRL:FOLLOW:SAVE
  void cmdFollowClear();                            // CTRL:FOLLOW:CLEAR

  // Speed-trim controller commands (CTRL:TRIM:*)
  void cmdTrimQuery();                               // CTRL:TRIM? — query config + live state
  void cmdTrimSetGains(const ParsedCommand &cmd);    // CTRL:TRIM:GAINS <kp> <ki>
  void cmdTrimSetLimits(const ParsedCommand &cmd);   // CTRL:TRIM:LIMITS <out> <int>
  void cmdTrimSetMaxPct(const ParsedCommand &cmd);   // CTRL:TRIM:MAXPCT <1-50>
  void cmdTrimReset();                               // CTRL:TRIM:RESET
  void cmdTrimSave();                                // CTRL:TRIM:SAVE

  // Legacy PID aliases (route to trim commands)
  void cmdPidQuery();                                // CTRL:PID? → CTRL:TRIM?
  void cmdPidSetGains(const ParsedCommand &cmd);     // CTRL:PID:GAINS → CTRL:TRIM:GAINS
  void cmdPidSetLimits(const ParsedCommand &cmd);    // CTRL:PID:LIMITS → CTRL:TRIM:LIMITS
  void cmdPidReset();                                // CTRL:PID:RESET → CTRL:TRIM:RESET
  void cmdPidSave();                                 // CTRL:PID:SAVE → CTRL:TRIM:SAVE

  // System identification commands
  void cmdSysIdStep(const ParsedCommand &cmd);       // CTRL:SYSID:STEP
  void cmdSysIdRamp(const ParsedCommand &cmd);       // CTRL:SYSID:RAMP
  void cmdSysIdSine(const ParsedCommand &cmd);       // CTRL:SYSID:SINE
  void cmdSysIdTrapezoid(const ParsedCommand &cmd);  // CTRL:SYSID:TRAPEZOID
  void cmdSysIdRect(const ParsedCommand &cmd);       // CTRL:SYSID:RECT
  void cmdSysIdStatus();                             // CTRL:SYSID:STATUS?
  void cmdSysIdData(const ParsedCommand &cmd);       // CTRL:SYSID:DATA?
  void cmdSysIdAbort();                              // CTRL:SYSID:ABORT
};

} // namespace Comms

#endif // COMMAND_PARSER_HPP
