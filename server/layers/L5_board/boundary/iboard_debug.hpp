/**
 * @file iboard_debug.hpp
 * @brief Board:: provider — register-level bring-up commands (H_L4_L5)
 *
 * Implemented by L5_board/debug/board_debug.cpp (moved from harness/debug_commands.cpp).
 * Pure register access plus text output through the response sink; no task or RTOS
 * knowledge. Task context only.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "harness/pins/iresponse_sink.hpp"

namespace Board {

class IBoardDebug {
public:
    virtual ~IBoardDebug() = default;

    /// True if `cmd` is one of the raw commands (lets the gate above report "locked").
    virtual bool knowsCommand(const char* cmd) const = 0;

    /// Execute a raw command. Returns true if `cmd` was recognised.
    virtual bool rawCommand(const char* cmd, const char* const* args, uint8_t argCount,
                            Harness::IResponseSink& out) = 0;

    virtual int formatSpi2Diag(char* buf, size_t bufSize) = 0;
};

} // namespace Board
