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

#include "comms/transport_interface.hpp"
#include <cstdint>

namespace Comms {

// Maximum command line length
constexpr size_t CMD_BUFFER_SIZE = 128;

// Maximum number of arguments
constexpr size_t MAX_ARGS = 6;

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
    IDLE,       // Ready for commands
    ARMED,      // Commands queued, awaiting START
    RUNNING,    // Motion in progress
    STOPPING,   // Decelerating
    FAULT,      // Error, requires CLEAR_FAULT
    ESTOP       // Emergency stop, outputs disabled
};

/**
 * @brief Parsed command structure
 */
struct ParsedCommand {
    char cmd[16];            // Command word (uppercase)
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
    explicit CommandParser(ITransport& transport);

    /**
     * @brief Process incoming data (call periodically)
     *
     * Reads available bytes, builds command line,
     * parses and dispatches complete commands.
     */
    void process();

    /**
     * @brief Send OK response
     * @param msg Response message
     */
    void respondOk(const char* msg);

    /**
     * @brief Send error response
     * @param msg Error message
     */
    void respondErr(const char* msg);

    /**
     * @brief Send multi-line data response
     * @param lines Array of line strings
     * @param count Number of lines
     */
    void respondData(const char** lines, size_t count);

private:
    ITransport& m_transport;
    char m_buffer[CMD_BUFFER_SIZE];
    size_t m_bufIndex;

    /**
     * @brief Parse command line into ParsedCommand
     * @param line Null-terminated command line
     * @return Parsed command structure
     */
    ParsedCommand parse(const char* line);

    /**
     * @brief Dispatch parsed command to appropriate handler
     * @param cmd Parsed command
     */
    void dispatch(const ParsedCommand& cmd);

    // Motion command handlers
    void cmdMove(const ParsedCommand& cmd);
    void cmdGoTo(const ParsedCommand& cmd);
    void cmdRun(const ParsedCommand& cmd);
    void cmdStop(const ParsedCommand& cmd);
    void cmdEstop();

    // Configuration command handlers
    void cmdEnable();
    void cmdDisable();
    void cmdAccel(const ParsedCommand& cmd);
    void cmdDecel(const ParsedCommand& cmd);
    void cmdMaxSpd(const ParsedCommand& cmd);

    // Synchronization command handlers
    void cmdQueue(const ParsedCommand& cmd);
    void cmdArm();
    void cmdStart();
    void cmdStartAt(const ParsedCommand& cmd);
    void cmdClearQueue();

    // Timing/diagnostics command handlers
    void cmdPing(const ParsedCommand& cmd);
    void cmdGetTick();
    void cmdGetStatus();
    void cmdClearFault();

    // Utility command handlers
    void cmdHelp();
    void cmdVersion();
    void cmdHome();
    void cmdZero();
    void cmdEncoder();

    // Device identification command handlers
    void cmdGetDeviceId();
    void cmdSetDeviceId(const ParsedCommand& cmd);
    void cmdSetRole(const ParsedCommand& cmd);

    // Control mode command handlers
    void cmdGetMode();
    void cmdSetMode(const ParsedCommand& cmd);
    void cmdGetEncoderStatus();
};

} // namespace Comms

#endif // COMMAND_PARSER_HPP
