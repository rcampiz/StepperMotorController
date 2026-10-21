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

#include "harness/defs/harness_l1_l2.hpp"
#include "L2_protocol/boundary/icommand_dispatcher.hpp"
#include "harness/pins/dispatch_result.hpp"
#include "L2_protocol/boundary/icommand_processor.hpp"
#include <stdint.h>

namespace Protocol {

class Base64Reader;   // L2_protocol/base64_reader.hpp: upload payload decoder

struct StatusJsonView;   // status_json.hpp

// Import types moved to Harness:: that are used throughout CommandParser
using Harness::ServiceStatus;
using Harness::statusToString;
using Harness::statusToCode;
using Protocol::ICommandDispatcher;

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
 * @brief Dispatch statistics (read by UI for observability)
 */
struct DispatchStats {
  uint32_t totalCommands;     // Commands parsed and dispatched
  uint32_t unknownCommands;   // Unrecognized command/namespace
  uint32_t parseErrors;       // Lines that failed to parse
  uint32_t rxOverflowLines;   // Lines refused because the RX ring overflowed during them (F1.1)
  // Ring buffer of recent command names
  static constexpr uint8_t RECENT_SIZE = 8;
  char     recentCmds[RECENT_SIZE][24];
  uint8_t  recentHead;
  uint8_t  recentCount;
};

/**
 * @brief Command parser and dispatcher
 */
class CommandParser : public Protocol::ICommandProcessor {
public:
  /**
   * @brief Construct parser with transport
   * @param transport Transport interface for I/O
   */
  explicit CommandParser(Transport::ITransport &transport,
                        ICommandDispatcher &dispatcher);

  /**
   * @brief Process incoming data (call periodically)
   *
   * Reads available bytes, builds command line,
   * parses and dispatches complete commands.
   */
  void process() override;

  /**
   * @brief Send OK response (format-aware)
   * @param msg Response message (ignored in JSON mode)
   */
  void respondOk(const char *msg);

  /**
   * @brief Send error response (format-aware)
   * @param msg Error message
   */
  void respondErr(const char *msg);               ///< code: USAGE for "Usage:" text, else ERROR
  void respondErr(const char *code, const char *msg);   ///< explicit code (F6)
  /// ERROR with a range taken from the driver's caps: fmt holds one or two %ld
  void respondRange(const char *fmt, long a, long b = 0);
  /// OUT_OF_RANGE "<what> must be <lo>-<hi> <unit>", the range from the driver's caps
  void respondOutOfRange(const char *what, double lo, double hi, const char *unit);
  /// False (and INVALID_STATE sent) when the driver has no protocol 2 chip units
  bool requireChipUnits(const char *replacement);

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
   * @brief Convenience: respondOk on success, respondErr(statusToString) on failure
   */
  void respondStatus(ServiceStatus r, const char *okMsg);

  /**
   * @brief Get current response format
   */
  ResponseFormat getFormat() const { return m_format; }

  /**
   * @brief Get dispatch statistics snapshot
   */
  const DispatchStats& getDispatchStats() const { return m_dispatchStats; }

  /**
   * @brief Check baud rate auto-revert timeout
   *
   * Call periodically from comms task. If a baud change was made
   * and no valid command has been received within 2 seconds,
   * reverts to the previous baud rate.
   */
  void checkBaudRevert() override;

  /// Format and send an async event (ICommandProcessor)
  void emitBanner() override;
  void emitEvent(const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms) override;
  void emitTelemetry(const Protocol::TelemetrySnapshot& snap) override;
  void emitJoystick(UI::JoyDirection dir, bool pressed) override;

  /// Get dispatch statistics (ICommandProcessor)
  void getDispatchStats(Harness::DispatchStats& out) const override;

  /**
   * @brief Set response format
   */
  void setFormat(ResponseFormat format) { m_format = format; }

  /**
   * @brief Set debug command handler for hardware debug commands
   */

private:
  /**
   * F12: every multi-line reply ends with a terminator. ASCII: the lines, then `OK <n>`
   * (n = lines sent). JSON: one line {"status":"ok","command":"<cmd>","data":{<extra>,
   * "lines":[...],"count":n}} streamed line by line, so no buffer holds the reply.
   * Also the IResponseSink handed to L3 for debug output (keeps the transport inside L2);
   * outside begin()/end() it passes text straight through.
   */
  class LineReply : public Harness::IResponseSink {
  public:
    static constexpr size_t LINE_MAX = 160;   ///< longer lines wrap
    explicit LineReply(Transport::ITransport& t) : m_t(t) {}
    /// Start a reply; `extraJson` (no braces, no trailing comma) precedes `lines` in JSON.
    void begin(bool json, const char* command, const char* extraJson = nullptr);
    void write(const char* s) override;       ///< partial text, '\n' ends a line
    void writeLine(const char* s) override;   ///< text plus a line ending
    void end();                               ///< flush and send the terminator
    void abort();                             ///< drop the reply if nothing went out, else close it
    uint32_t count() const { return m_count; }
  private:
    void put(const char* s, bool endLine);
    void emitLine();
    void sendHead();
    Transport::ITransport& m_t;
    const char* m_command = nullptr;
    const char* m_extra = nullptr;
    bool m_json = false;
    bool m_active = false;
    bool m_headSent = false;
    uint32_t m_count = 0;
    size_t m_len = 0;
    char m_line[LINE_MAX + 1];
  };
  Transport::ITransport &m_transport;
  LineReply m_sink;
  ICommandDispatcher &m_dispatcher;
  char m_buffer[CMD_BUFFER_SIZE];
  size_t m_bufIndex;
  ResponseFormat m_format;       // Current response format (ASCII or JSON)
  bool m_telemetryCompact = false;   // SYST:TLM:FMT: JSON pushes carry the compact object
  bool m_echo = true;            // F7: terminal echo; off by default in JSON mode, SYST:ECHO ON|OFF
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

  // Table-driven dispatch types and helpers.
  // Each dispatchFoo() function (below) defines static const arrays of these,
  // then calls dispatch1/dispatch0 to find a matching suffix and call the handler.
  struct CmdEntry  { const char* name; void (CommandParser::*fn)(const ParsedCommand&); };
  struct CmdEntry0 { const char* name; void (CommandParser::*fn)(); };
  bool dispatch1(const char* suffix, const CmdEntry* table, size_t count, const ParsedCommand& cmd);
  bool dispatch0(const char* suffix, const CmdEntry0* table, size_t count);

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

  // Motion command handlers
  void cmdMove(const ParsedCommand &cmd);
  void cmdGoTo(const ParsedCommand &cmd);
  void cmdRun(const ParsedCommand &cmd);
  void cmdStop(const ParsedCommand &cmd);
  void cmdEstop();

  // Configuration command handlers
  void cmdEnable();
  void cmdDisable();
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
  void cmdTasks();
  void cmdMem();
  void cmdHarness();
  void cmdClearFault();
  void cmdForceClearFault();

  // Heartbeat command handlers
  void cmdHeartbeat(const ParsedCommand &cmd);
  void cmdSetHeartbeat(const ParsedCommand &cmd);
  void cmdGetHeartbeatStatus();

  // Telemetry push (F9)
  void cmdTelemetryRate(const ParsedCommand &cmd);
  void cmdTelemetryRateQuery();
  void cmdTelemetryFormat(const ParsedCommand &cmd);
  void cmdTelemetryFormatQuery();
  uint32_t telemetryFloorMs() const;   ///< smallest push period for the active line size
  /// Fill the DIAG:STAT? view (shared by the JSON telemetry push)
  void fillStatusView(const TelemetrySnapshot &snap, StatusJsonView &v);

  // Utility command handlers
  void cmdVersion();
  void cmdHelp();   ///< SYST:HELP? from the generated help_table.inc (F8)
  void cmdHome(const ParsedCommand &cmd);
  void cmdZero();
  void cmdEncoderZero();
  void cmdZeroAll();
  void cmdEncoder();
  void cmdEncFilter(const ParsedCommand &cmd);
  void cmdEncFilterSub(const char *sub, const ParsedCommand &cmd);

  // Device identification command handlers
  void cmdGetDeviceId();
  void cmdGetRole();
  void cmdIdn();
  void cmdEcho(const ParsedCommand &cmd);
  void cmdEchoQuery();
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

  // Dispatch statistics
  DispatchStats m_dispatchStats = {};
  uint32_t m_lineOverflowBase = 0;   ///< rxOverflowCount() when the current line started (F1.1)

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
  void cmdUiEstopHold(const ParsedCommand &cmd);
  void cmdLcdReg(const ParsedCommand &cmd);
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
   * @brief Receive the 4-byte CRC (base64, after the data) and verify against computed value
   * @param computedCrc Running CRC state (pre-finalized, i.e. before XOR)
   * @param payload The upload's base64 reader (the CRC is encoded together with the data)
   * @return true if CRC matches
   */
  bool verifyCrc(uint32_t computedCrc, Base64Reader &payload);

  /// An upload keeps the Comms task inside one handler for seconds, during which no
  /// SYST:HB line can be parsed. The payload bytes themselves prove the host is alive, so
  /// the receive loops feed the watchdog with the last heartbeat's sequence number (F2).
  void feedWatchdogDuringUpload();

  // Trace command handlers
  void cmdTraceDump();
  void cmdTraceReset();

  // Motor configuration command handlers
  void cmdMotorConfigShow();
  void cmdMotorConfigSave();
  void cmdMotorConfigLoad();
  void cmdMotorConfigReset();
  void cmdMotorConfigKval(const ParsedCommand &cmd);
  void cmdMotorConfigDrive(const ParsedCommand &cmd);
  void cmdMotorConfigTval(const ParsedCommand &cmd);
  void cmdMotorConfigPred(const ParsedCommand &cmd);
  void cmdMotorConfigDecay(const ParsedCommand &cmd);
  void cmdMotorConfigIdle(const ParsedCommand &cmd);
  void cmdMotorConfigOcd(const ParsedCommand &cmd);
  void cmdMotorConfigStall(const ParsedCommand &cmd);
  void cmdMotorConfigFault(const ParsedCommand &cmd);
  void cmdMotorConfigMotion(const ParsedCommand &cmd);
  void cmdMotorConfigStepMode(const ParsedCommand &cmd);
  void cmdMotorConfigApply();

  // Protocol 3: the driver's capabilities and its settings in physical units
  // (handlers/driver_settings_handlers.cpp). A setter's "<name>?" twin is the query.
  void cmdDrvCaps();                                  // DRV:CAPS?
  void cmdDrvPhysShow();                              // DRV:CFG:PHYS?
  void cmdDrvCurrent(const ParsedCommand &cmd);       // DRV:CFG:CURRENT[?]
  void cmdDrvScale(const ParsedCommand &cmd);       // DRV:CFG:SCALE <percent> / DRV:CFG:SCALE?
  void cmdDrvDuty(const ParsedCommand &cmd);          // DRV:CFG:DUTY[?]
  void cmdDrvUstepConfig(const ParsedCommand &cmd);   // DRV:CFG:USTEP[?]
  void cmdDrvAccel(const ParsedCommand &cmd);         // DRV:CFG:ACCEL[?]
  void cmdDrvDecel(const ParsedCommand &cmd);         // DRV:CFG:DECEL[?]
  void cmdDrvMaxSpeed(const ParsedCommand &cmd);      // DRV:CFG:MAXSPD[?]
  void cmdDrvOvercurrent(const ParsedCommand &cmd);   // DRV:CFG:OVERCURRENT[?]
  void cmdDrvStallThreshold(const ParsedCommand &cmd);// DRV:CFG:STALL_TH[?]
  void cmdDrvAlarms(const ParsedCommand &cmd);        // DRV:CFG:ALARMS[?]
  void cmdDrvUstep(const ParsedCommand &cmd);         // DRV:USTEP: now, Hi-Z safe
  void cmdDrvUstepQuery();                            // DRV:USTEP?: read back from the driver
  /// The settings reply of a physical setter or its query (one OK line / one JSON object)
  void replyDriverSettings(const char *what, bool changed);

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

  // Resonance avoider (CTRL:RES:*)
  void cmdResQuery();                                // CTRL:RES? — model, limits, live state
  void cmdResConfig(const ParsedCommand &cmd);       // CTRL:RES:CFG <en> [f1a] [exp] [band] [boost] [cut] [mask]
  void cmdResSave();                                 // CTRL:RES:SAVE

  // System identification commands
  void cmdSysIdStep(const ParsedCommand &cmd);       // CTRL:SYSID:STEP
  void cmdSysIdRamp(const ParsedCommand &cmd);       // CTRL:SYSID:RAMP
  void cmdSysIdSine(const ParsedCommand &cmd);       // CTRL:SYSID:SINE
  void cmdSysIdTrapezoid(const ParsedCommand &cmd);  // CTRL:SYSID:TRAPEZOID
  void cmdSysIdRect(const ParsedCommand &cmd);       // CTRL:SYSID:RECT
  void cmdSysIdStatus();                             // CTRL:SYSID:STATUS?
  void cmdSysIdData(const ParsedCommand &cmd);       // CTRL:SYSID:DATA?
  void cmdSysIdAbort();                              // CTRL:SYSID:ABORT
  void cmdCaptureFreeze();                           // CTRL:CAP:FREEZE
  void cmdCaptureData(const ParsedCommand &cmd);     // CTRL:CAP:DATA? <offset> <count>
  void cmdCaptureResume();                           // CTRL:CAP:RESUME
};

} // namespace Protocol

#endif // COMMAND_PARSER_HPP
