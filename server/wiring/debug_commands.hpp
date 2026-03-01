/**
 * @file debug_commands.hpp
 * @brief Hardware debug/bringup command handler
 *
 * Implements IDebugCommands — contains all register-touching debug commands
 * extracted from command_parser.cpp. Lives in wiring/ so it can include
 * CMSIS, board headers, and task APIs.
 */

#pragma once

#include "F_platform/interfaces/idebug_commands.hpp"

class DebugCommandHandler : public Comms::IDebugCommands {
public:
    bool dispatch(const char* cmd, const char* const* args,
                  uint8_t argCount, Comms::ITransport& transport) override;

    int formatSpi2Diag(char* buf, size_t bufSize) override;

private:
    // SPI/GPIO debug commands
    void cmdSpiDebug(Comms::ITransport& t);
    void cmdSpiModeTest(Comms::ITransport& t);
    void cmdCsLow(Comms::ITransport& t);
    void cmdCsHigh(Comms::ITransport& t);
    void cmdMisoPulldown(Comms::ITransport& t);
    void cmdMisoNopull(Comms::ITransport& t);
    void cmdStbyRelease(Comms::ITransport& t);
    void cmdStbyHold(Comms::ITransport& t);
    void cmdGpioState(Comms::ITransport& t);
    void cmdPa6Hiz(Comms::ITransport& t);
    void cmdPa6Restore(Comms::ITransport& t);
    void cmdSpiStop(Comms::ITransport& t);
    void cmdSpiStart(Comms::ITransport& t);
    void cmdGpioDump(Comms::ITransport& t);
    void cmdSpiFloatTest(Comms::ITransport& t);
    void cmdSpiBitbang(Comms::ITransport& t);
    void cmdSpiBitbangM3(Comms::ITransport& t);
    void cmdSpiEchoTest(Comms::ITransport& t);
    void cmdSpiCsToggle(Comms::ITransport& t);

    // powerSTEP01 bringup commands
    void cmdPs01Diag(Comms::ITransport& t);
    void cmdPs01FixClk(Comms::ITransport& t);
    void cmdPs01Safe(Comms::ITransport& t);
    void cmdPs01Reset(Comms::ITransport& t);
    void cmdPs01Run(const char* const* args, uint8_t argCount, Comms::ITransport& t);
    void cmdPs01Stop(Comms::ITransport& t);

    // LCD/motor isolation
    void cmdLcdDisable(Comms::ITransport& t);
    void cmdMosiTest(Comms::ITransport& t);
    void cmdSpiLoopback(Comms::ITransport& t);

    // SCPI debug queries
    void cmdMotorDebug(Comms::ITransport& t);
    void cmdEncDebug(Comms::ITransport& t);
};
