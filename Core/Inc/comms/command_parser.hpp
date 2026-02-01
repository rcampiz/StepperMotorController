/**
 * @file command_parser.hpp
 * @brief ASCII command protocol parser
 *
 * Line-based protocol:
 *   Command: <CMD> [ARG1] [ARG2]...\r\n
 *   Response: OK: <msg>\r\n or ERR: <msg>\r\n
 */

#ifndef COMMAND_PARSER_HPP
#define COMMAND_PARSER_HPP

#include "comms/transport_interface.hpp"
#include <cstdint>

namespace Comms {

// Maximum command line length
constexpr size_t CMD_BUFFER_SIZE = 128;

// Maximum number of arguments
constexpr size_t MAX_ARGS = 4;

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

    // Command handlers (stubs)
    void cmdHelp();
    void cmdStatus();
    void cmdVersion();
    void cmdMove(const ParsedCommand& cmd);
    void cmdGoTo(const ParsedCommand& cmd);
    void cmdRun(const ParsedCommand& cmd);
    void cmdStop();
    void cmdHalt();
    void cmdHome();
    void cmdZero();
    void cmdEncoder();
    void cmdAccel(const ParsedCommand& cmd);
    void cmdDecel(const ParsedCommand& cmd);
    void cmdMaxSpd(const ParsedCommand& cmd);
};

} // namespace Comms

#endif // COMMAND_PARSER_HPP
