/**
 * @file boundary/idebugcommands.hpp
 * @brief Consumer port — Services side of the Scheduler::IDebugCommands harness connection
 *
 * Structurally identical to Scheduler::IDebugCommands.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/itransport.hpp"
#include <stdint.h>
#include <stddef.h>

namespace Services {

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
                          uint8_t argCount, Transport::ITransport& transport) = 0;

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

} // namespace Services
