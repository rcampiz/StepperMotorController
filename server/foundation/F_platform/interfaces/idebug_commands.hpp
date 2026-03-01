/**
 * @file idebug_commands.hpp
 * @brief Interface for hardware debug/bringup commands
 *
 * Defined in Foundation so L2 (command_parser) can dispatch debug commands
 * without including CMSIS headers. Implemented by DebugCommandHandler in wiring/.
 */

#pragma once

#include "F_platform/interfaces/itransport.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Comms {

class IDebugCommands {
public:
    virtual ~IDebugCommands() = default;

    /**
     * @brief Try to handle a debug/hardware command
     * @param cmd Command name (e.g. "SPI_DEBUG", "MOTOR_DEBUG", "ENC_DEBUG")
     * @param args Array of argument strings
     * @param argCount Number of arguments
     * @param transport Transport for output
     * @return true if command was handled, false if not recognized
     */
    virtual bool dispatch(const char* cmd, const char* const* args,
                          uint8_t argCount, ITransport& transport) = 0;

    /**
     * @brief Format SPI2 peripheral + pin diagnostics into buffer
     *
     * Used by cmdFlashTest() to include hardware register state in its
     * diagnostic output without command_parser.cpp needing CMSIS.
     *
     * @param buf Output buffer
     * @param bufSize Buffer size
     * @return Number of chars written (0 if not available)
     */
    virtual int formatSpi2Diag(char* buf, size_t bufSize) { (void)buf; (void)bufSize; return 0; }
};

} // namespace Comms
