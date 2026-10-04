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

class DebugCommandHandler : public Scheduler::IDebugCommands {
public:
    bool dispatch(const char* cmd, const char* const* args,
                  uint8_t argCount, Transport::ITransport& transport) override;

    int formatSpi2Diag(char* buf, size_t bufSize) override;

private:
    // SPI/GPIO debug commands
    void cmdSpiDebug(Transport::ITransport& t);
    void cmdSpiModeTest(Transport::ITransport& t);
    void cmdCsLow(Transport::ITransport& t);
    void cmdCsHigh(Transport::ITransport& t);
    void cmdMisoPulldown(Transport::ITransport& t);
    void cmdMisoNopull(Transport::ITransport& t);
    void cmdStbyRelease(Transport::ITransport& t);
    void cmdStbyHold(Transport::ITransport& t);
    void cmdGpioState(Transport::ITransport& t);
    void cmdPa6Hiz(Transport::ITransport& t);
    void cmdPa6Restore(Transport::ITransport& t);
    void cmdSpiStop(Transport::ITransport& t);
    void cmdSpiStart(Transport::ITransport& t);
    void cmdGpioDump(Transport::ITransport& t);
    void cmdSpiFloatTest(Transport::ITransport& t);
    void cmdSpiBitbang(Transport::ITransport& t);
    void cmdSpiBitbangM3(Transport::ITransport& t);
    void cmdSpiEchoTest(Transport::ITransport& t);
    void cmdSpiCsToggle(Transport::ITransport& t);

    // powerSTEP01 bringup commands
    void cmdPs01Diag(Transport::ITransport& t);
    void cmdPs01FixClk(Transport::ITransport& t);
    void cmdPs01Safe(Transport::ITransport& t);
    void cmdPs01Reset(Transport::ITransport& t);
    void cmdPs01Run(const char* const* args, uint8_t argCount, Transport::ITransport& t);
    void cmdPs01Stop(Transport::ITransport& t);

    // LCD/motor isolation
    void cmdLcdDisable(Transport::ITransport& t);
    void cmdMosiTest(Transport::ITransport& t);
    void cmdSpiLoopback(Transport::ITransport& t);

    // SCPI debug queries
    void cmdMotorDebug(Transport::ITransport& t);
    void cmdEncDebug(Transport::ITransport& t);
};
