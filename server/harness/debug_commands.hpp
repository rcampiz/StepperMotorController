/**
 * @file debug_commands.hpp
 * @brief Hardware debug/bringup command handler
 *
 * Implements IDebugCommands — contains all register-touching debug commands
 * extracted from command_parser.cpp. Lives in harness/ (composition root)
 * so it can include CMSIS, board headers, and task APIs.
 */

#pragma once

#include "harness/pins/idebug_commands.hpp"

class DebugCommandHandler : public Harness::IDebugCommands {
public:
    bool dispatch(const char* cmd, const char* const* args,
                  uint8_t argCount, Harness::ITransport& transport) override;

    int formatSpi2Diag(char* buf, size_t bufSize) override;

private:
    // SPI/GPIO debug commands
    void cmdSpiDebug(Harness::ITransport& t);
    void cmdSpiModeTest(Harness::ITransport& t);
    void cmdCsLow(Harness::ITransport& t);
    void cmdCsHigh(Harness::ITransport& t);
    void cmdMisoPulldown(Harness::ITransport& t);
    void cmdMisoNopull(Harness::ITransport& t);
    void cmdStbyRelease(Harness::ITransport& t);
    void cmdStbyHold(Harness::ITransport& t);
    void cmdGpioState(Harness::ITransport& t);
    void cmdPa6Hiz(Harness::ITransport& t);
    void cmdPa6Restore(Harness::ITransport& t);
    void cmdSpiStop(Harness::ITransport& t);
    void cmdSpiStart(Harness::ITransport& t);
    void cmdGpioDump(Harness::ITransport& t);
    void cmdSpiFloatTest(Harness::ITransport& t);
    void cmdSpiBitbang(Harness::ITransport& t);
    void cmdSpiBitbangM3(Harness::ITransport& t);
    void cmdSpiEchoTest(Harness::ITransport& t);
    void cmdSpiCsToggle(Harness::ITransport& t);

    // powerSTEP01 bringup commands
    void cmdPs01Diag(Harness::ITransport& t);
    void cmdPs01FixClk(Harness::ITransport& t);
    void cmdPs01Safe(Harness::ITransport& t);
    void cmdPs01Reset(Harness::ITransport& t);
    void cmdPs01Run(const char* const* args, uint8_t argCount, Harness::ITransport& t);
    void cmdPs01Stop(Harness::ITransport& t);

    // LCD/motor isolation
    void cmdLcdDisable(Harness::ITransport& t);
    void cmdMosiTest(Harness::ITransport& t);
    void cmdSpiLoopback(Harness::ITransport& t);

    // SCPI debug queries
    void cmdMotorDebug(Harness::ITransport& t);
    void cmdEncDebug(Harness::ITransport& t);
};
