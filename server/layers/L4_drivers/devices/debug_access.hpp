/**
 * @file debug_access.hpp
 * @brief Drivers::IDebugAccess over Board::IBoardDebug — raw bring-up pass-through
 *
 * Bring-up commands are register-level by nature, so L4 adds no logic here; it is the
 * adjacent-layer path (rule 2) that keeps L3 from naming an L5 type.
 */
#pragma once
#include "L4_drivers/boundary/idebug_access.hpp"
#include "harness/defs/harness_l4_l5.hpp"

namespace Drivers {

class DebugAccess : public IDebugAccess {
public:
    explicit DebugAccess(Board::IBoardDebug& board) : m_board(board) {}

    bool knowsCommand(const char* cmd) const override { return m_board.knowsCommand(cmd); }
    bool rawCommand(const char* cmd, const char* const* args, uint8_t argCount,
                    Harness::IResponseSink& out) override {
        return m_board.rawCommand(cmd, args, argCount, out);
    }
    int formatSpi2Diag(char* buf, size_t bufSize) override {
        return m_board.formatSpi2Diag(buf, bufSize);
    }

private:
    Board::IBoardDebug& m_board;
};

} // namespace Drivers
