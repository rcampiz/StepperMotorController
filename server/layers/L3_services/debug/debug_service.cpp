/**
 * @file debug_service.cpp
 * @brief L3 DebugService implementation
 */

#include "L3_services/debug/debug_service.hpp"
#include <stdio.h>
#include <string.h>

namespace Services {

DebugService g_debugService;

void DebugService::init(Drivers::IDebugAccess* access,
                        IMotorTaskControl* motorCtrl,
                        IDisplayControl* display)
{
    m_access = access;
    m_motorCtrl = motorCtrl;
    m_display = display;
    m_unlocked = false;
}

Harness::ServiceStatus DebugService::debugUnlock()
{
    m_unlocked = true;
    return Harness::ServiceStatus::Ok;
}

Harness::ServiceStatus DebugService::debugLock()
{
    m_unlocked = false;
    return Harness::ServiceStatus::Ok;
}

/// Read-only register/pin dumps: harmless, so they bypass DBG:UNLOCK (F3). The GUI
/// refreshes MOTOR_DEBUG on every connect; the encoder/PS01 dumps are diagnostics.
bool DebugService::isReadOnlyCommand(const char* cmd) const
{
    return strcmp(cmd, "MOTOR_DEBUG") == 0 || strcmp(cmd, "ENC_DEBUG") == 0 ||
           strcmp(cmd, "PS01_DIAG") == 0 || strcmp(cmd, "GPIO_STATE") == 0 ||
           strcmp(cmd, "GPIO_DUMP") == 0;
}

bool DebugService::isTaskAwareCommand(const char* cmd) const
{
    return strcmp(cmd, "SPI_STOP") == 0 || strcmp(cmd, "SPI_START") == 0 ||
           strcmp(cmd, "LCD_DISABLE") == 0 || strcmp(cmd, "MOTOR_DEBUG") == 0;
}

DebugDispatchResult DebugService::debugDispatch(const char* cmd, const char* const* args, uint8_t argCount,
                                 Harness::IResponseSink& out)
{
    const bool known = isTaskAwareCommand(cmd) ||
                       (m_access != nullptr && m_access->knowsCommand(cmd));
    if (!known) {
        return DebugDispatchResult::NotClaimed;
    }
    if (!m_unlocked && !isReadOnlyCommand(cmd)) {
        return DebugDispatchResult::Locked;   // the parser replies ERROR DBG_LOCKED
    }

    if (strcmp(cmd, "SPI_STOP") == 0)    { cmdSpiStop(args, argCount, out); return DebugDispatchResult::Handled; }
    if (strcmp(cmd, "SPI_START") == 0)   { cmdSpiStart(args, argCount, out); return DebugDispatchResult::Handled; }
    if (strcmp(cmd, "LCD_DISABLE") == 0) { cmdLcdDisable(args, argCount, out); return DebugDispatchResult::Handled; }
    if (strcmp(cmd, "MOTOR_DEBUG") == 0) { cmdMotorDebug(out); return DebugDispatchResult::Handled; }

    return (m_access != nullptr && m_access->rawCommand(cmd, args, argCount, out))
               ? DebugDispatchResult::Handled : DebugDispatchResult::NotClaimed;
}

int DebugService::debugFormatSpi2Diag(char* buf, size_t bufSize)
{
    return (m_access != nullptr) ? m_access->formatSpi2Diag(buf, bufSize) : 0;
}

// ---------------------------------------------------------------------------
// Task-aware commands
// ---------------------------------------------------------------------------

void DebugService::cmdSpiStop(const char* const* args, uint8_t argc, Harness::IResponseSink& out)
{
    if (m_motorCtrl != nullptr) {
        m_motorCtrl->setDebugPaused(true);     // Motor task stops touching the driver (flag, no suspend)
        out.writeLine("Motor task paused.");
    }
    if (m_access != nullptr) m_access->rawCommand("SPI_STOP", args, argc, out);
}

void DebugService::cmdSpiStart(const char* const* args, uint8_t argc, Harness::IResponseSink& out)
{
    if (m_access != nullptr) m_access->rawCommand("SPI_START", args, argc, out);
    if (m_motorCtrl != nullptr) {
        m_motorCtrl->setDebugPaused(false);
        out.writeLine("Motor task resumed.");
    }
}

void DebugService::cmdLcdDisable(const char* const* args, uint8_t argc, Harness::IResponseSink& out)
{
    out.writeLine("Disabling LCD and display rendering...");
    if (m_display != nullptr) {
        m_display->setDisplayEnabled(false);    // Display task keeps looping; tick() returns at once
        out.writeLine("Display rendering disabled.");
    }
    if (m_access != nullptr) m_access->rawCommand("LCD_DISABLE", args, argc, out);
    out.writeLine("Reinitializing motor driver...");
    if (m_motorCtrl != nullptr) {
        m_motorCtrl->reinit();
    }
    out.writeLine("Motor driver reinitialized.");
}

void DebugService::cmdMotorDebug(Harness::IResponseSink& out)
{
    if (m_motorCtrl == nullptr) {
        out.writeLine("ERROR Motor not initialized");
        return;
    }
    Harness::MotorDebugParams info;
    m_motorCtrl->getDebugInfo(info);

    const char *hiZ = ((info.status & 0x0001) != 0) ? "HiZ" : "Active";
    const char *busy = ((info.status & 0x0002) != 0) ? "Idle" : "BUSY";
    bool cmdErr = (info.status & 0x0080) != 0;
    bool uvlo = (info.status & 0x0200) == 0;
    uint8_t thStatus = (info.status >> 11) & 0x3;
    bool ocd = (info.status & 0x2000) == 0;
    bool stepLossA = (info.status & 0x4000) == 0;
    bool stepLossB = (info.status & 0x8000) == 0;

    char buf[256];
    snprintf(buf, sizeof(buf),
             "STATUS=%04X (%s %s%s%s%s%s%s%s)\n"
             "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
             "ACC=%u DEC=%u MAXSPD=%u POS=%ld\n"
             "ADC_OUT=%u",
             (unsigned)info.status, hiZ, busy, cmdErr ? " CMDERR" : "",
             uvlo ? " UVLO" : "",
             thStatus >= 2 ? " TH_SD" : (thStatus >= 1 ? " TH_WARN" : ""),
             ocd ? " OCD" : "", stepLossA ? " STALL_A" : "",
             stepLossB ? " STALL_B" : "", (unsigned)info.kvalHold,
             (unsigned)info.kvalRun, (unsigned)info.kvalAcc,
             (unsigned)info.kvalDec, (unsigned)info.accel, (unsigned)info.decel,
             (unsigned)info.maxSpeed, (long)info.absPos, (unsigned)info.adcOut);
    out.writeLine(buf);
}

} // namespace Services
