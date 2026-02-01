/**
 * @file command_parser.cpp
 * @brief ASCII command protocol parser implementation
 */

#include "comms/command_parser.hpp"
#include <cstring>
#include <cctype>

namespace Comms {

CommandParser::CommandParser(ITransport& transport)
    : m_transport(transport)
    , m_bufIndex(0)
{
    memset(m_buffer, 0, sizeof(m_buffer));
}

void CommandParser::process()
{
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

ParsedCommand CommandParser::parse(const char* line)
{
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
        if (!*line) break;

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

void CommandParser::dispatch(const ParsedCommand& cmd)
{
    // TODO: Implement command dispatch
    // For now, just echo unknown command
    if (strcmp(cmd.cmd, "HELP") == 0 || strcmp(cmd.cmd, "?") == 0) {
        cmdHelp();
    } else if (strcmp(cmd.cmd, "STATUS") == 0) {
        cmdStatus();
    } else if (strcmp(cmd.cmd, "VER") == 0 || strcmp(cmd.cmd, "VERSION") == 0) {
        cmdVersion();
    } else if (strcmp(cmd.cmd, "MOVE") == 0) {
        cmdMove(cmd);
    } else if (strcmp(cmd.cmd, "GOTO") == 0) {
        cmdGoTo(cmd);
    } else if (strcmp(cmd.cmd, "RUN") == 0) {
        cmdRun(cmd);
    } else if (strcmp(cmd.cmd, "STOP") == 0) {
        cmdStop();
    } else if (strcmp(cmd.cmd, "HALT") == 0) {
        cmdHalt();
    } else if (strcmp(cmd.cmd, "HOME") == 0) {
        cmdHome();
    } else if (strcmp(cmd.cmd, "ZERO") == 0) {
        cmdZero();
    } else if (strcmp(cmd.cmd, "ENCODER") == 0 || strcmp(cmd.cmd, "ENC") == 0) {
        cmdEncoder();
    } else {
        respondErr("Unknown command. Type HELP for list.");
    }
}

void CommandParser::respondOk(const char* msg)
{
    m_transport.print("OK: ");
    m_transport.println(msg);
}

void CommandParser::respondErr(const char* msg)
{
    m_transport.print("ERR: ");
    m_transport.println(msg);
}

void CommandParser::respondData(const char** lines, size_t count)
{
    m_transport.println("DATA:");
    for (size_t i = 0; i < count; i++) {
        m_transport.println(lines[i]);
    }
    m_transport.println("");
}

// Command handler stubs
void CommandParser::cmdHelp()
{
    m_transport.println("=== Stepper Motor Controller ===");
    m_transport.println("Commands:");
    m_transport.println("  MOVE <steps>     - Relative move (signed)");
    m_transport.println("  GOTO <position>  - Absolute move");
    m_transport.println("  RUN <dir> <spd>  - Continuous run (CW/CCW)");
    m_transport.println("  STOP             - Soft stop");
    m_transport.println("  HALT             - Hard stop");
    m_transport.println("  HOME             - Go to home position");
    m_transport.println("  ZERO             - Set current as zero");
    m_transport.println("  STATUS           - Motor status");
    m_transport.println("  ENCODER          - Encoder status");
    m_transport.println("  VER              - Firmware version");
    m_transport.println("  HELP             - This help");
}

void CommandParser::cmdStatus()
{
    // TODO: Query motor status from MotorTask telemetry
    respondOk("Status not implemented");
}

void CommandParser::cmdVersion()
{
    m_transport.println("Stepper Motor Controller v0.1.0");
    m_transport.println("Build: " __DATE__ " " __TIME__);
}

void CommandParser::cmdMove(const ParsedCommand& cmd)
{
    if (cmd.argCount < 1) {
        respondErr("Usage: MOVE <steps>");
        return;
    }
    // TODO: Parse steps and send to MotorTask queue
    respondOk("Move command queued");
}

void CommandParser::cmdGoTo(const ParsedCommand& cmd)
{
    if (cmd.argCount < 1) {
        respondErr("Usage: GOTO <position>");
        return;
    }
    // TODO: Parse position and send to MotorTask queue
    respondOk("GoTo command queued");
}

void CommandParser::cmdRun(const ParsedCommand& cmd)
{
    if (cmd.argCount < 2) {
        respondErr("Usage: RUN <CW|CCW> <speed>");
        return;
    }
    // TODO: Parse direction/speed and send to MotorTask queue
    respondOk("Run command queued");
}

void CommandParser::cmdStop()
{
    // TODO: Send soft stop to MotorTask queue
    respondOk("Stop command queued");
}

void CommandParser::cmdHalt()
{
    // TODO: Send hard stop to MotorTask queue
    respondOk("Halt command queued");
}

void CommandParser::cmdHome()
{
    // TODO: Send home command to MotorTask queue
    respondOk("Home command queued");
}

void CommandParser::cmdZero()
{
    // TODO: Send zero/reset position to MotorTask queue
    respondOk("Position zeroed");
}

void CommandParser::cmdEncoder()
{
    // TODO: Query encoder status from EncoderTask telemetry
    respondOk("Encoder status not implemented");
}

void CommandParser::cmdAccel(const ParsedCommand& cmd)
{
    (void)cmd;
    // TODO: Set acceleration
}

void CommandParser::cmdDecel(const ParsedCommand& cmd)
{
    (void)cmd;
    // TODO: Set deceleration
}

void CommandParser::cmdMaxSpd(const ParsedCommand& cmd)
{
    (void)cmd;
    // TODO: Set max speed
}

} // namespace Comms
