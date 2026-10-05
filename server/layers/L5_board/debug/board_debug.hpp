/**
 * @file board_debug.hpp
 * @brief L5 register-level bring-up commands (Board::IBoardDebug)
 *
 * Moved from harness/debug_commands.cpp. Pure register pokes + text output through
 * Harness::IResponseSink. Task-aware commands (task suspend/resume, controller reinit,
 * MOTOR_DEBUG) live in Services::DebugService; this class only does the hardware part.
 */
#pragma once

#include "L5_board/boundary/iboard_debug.hpp"

namespace Board {

class BoardDebug : public Board::IBoardDebug {
public:
    bool knowsCommand(const char* cmd) const override;
    bool rawCommand(const char* cmd, const char* const* args,
                    uint8_t argCount, Harness::IResponseSink& transport) override;
    int formatSpi2Diag(char* buf, size_t bufSize) override;

private:
    void cmdSpiDebug(Harness::IResponseSink& t);
    void cmdSpiModeTest(Harness::IResponseSink& t);
    void cmdCsLow(Harness::IResponseSink& t);
    void cmdCsHigh(Harness::IResponseSink& t);
    void cmdMisoPulldown(Harness::IResponseSink& t);
    void cmdMisoNopull(Harness::IResponseSink& t);
    void cmdStbyRelease(Harness::IResponseSink& t);
    void cmdStbyHold(Harness::IResponseSink& t);
    void cmdGpioState(Harness::IResponseSink& t);
    void cmdPa6Hiz(Harness::IResponseSink& t);
    void cmdPa6Restore(Harness::IResponseSink& t);
    void cmdSpiStop(Harness::IResponseSink& t);
    void cmdSpiStart(Harness::IResponseSink& t);
    void cmdGpioDump(Harness::IResponseSink& t);
    void cmdSpiFloatTest(Harness::IResponseSink& t);
    void cmdSpiBitbang(Harness::IResponseSink& t);
    void cmdSpiBitbangM3(Harness::IResponseSink& t);
    void cmdSpiEchoTest(Harness::IResponseSink& t);
    void cmdSpiCsToggle(Harness::IResponseSink& t);
    void cmdPs01Diag(Harness::IResponseSink& t);
    void cmdPs01FixClk(Harness::IResponseSink& t);
    void cmdPs01Safe(Harness::IResponseSink& t);
    void cmdPs01Reset(Harness::IResponseSink& t);
    void cmdPs01Run(const char* const* args, uint8_t argCount, Harness::IResponseSink& t);
    void cmdPs01Stop(Harness::IResponseSink& t);
    void cmdLcdDisable(Harness::IResponseSink& t);
    void cmdMosiTest(Harness::IResponseSink& t);
    void cmdSpiLoopback(Harness::IResponseSink& t);
    void cmdEncDebug(Harness::IResponseSink& t);
};

} // namespace Board
