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
                        Platform::IScheduler& scheduler,
                        IMotorControlService& motorCtrl)
{
    m_access = access;
    m_scheduler = &scheduler;
    m_motorCtrl = &motorCtrl;
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

bool DebugService::isTaskAwareCommand(const char* cmd) const
{
    return strcmp(cmd, "SPI_STOP") == 0 || strcmp(cmd, "SPI_START") == 0 ||
           strcmp(cmd, "LCD_DISABLE") == 0 || strcmp(cmd, "MOTOR_DEBUG") == 0;
}

bool DebugService::debugDispatch(const char* cmd, const char* const* args, uint8_t argCount,
                                 Harness::IResponseSink& out)
{
    const bool known = isTaskAwareCommand(cmd) ||
                       (m_access != nullptr && m_access->knowsCommand(cmd));
    if (!known) {
        return false;
    }
    if (!m_unlocked) {
        out.writeLine("ERROR DBG locked (send DBG:UNLOCK)");
        return true;
    }

    if (strcmp(cmd, "SPI_STOP") == 0)    { cmdSpiStop(args, argCount, out);    return true; }
    if (strcmp(cmd, "SPI_START") == 0)   { cmdSpiStart(args, argCount, out);   return true; }
    if (strcmp(cmd, "LCD_DISABLE") == 0) { cmdLcdDisable(args, argCount, out); return true; }
    if (strcmp(cmd, "MOTOR_DEBUG") == 0) { cmdMotorDebug(out);                 return true; }

    return (m_access != nullptr) && m_access->rawCommand(cmd, args, argCount, out);
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
    const Harness::TaskHandles& h = Harness::taskHandles();
    if (m_scheduler != nullptr && h.motor != nullptr) {
        m_scheduler->suspendTask(h.motor);
        out.writeLine("Motor task suspended.");
    }
    if (m_access != nullptr) m_access->rawCommand("SPI_STOP", args, argc, out);
}

void DebugService::cmdSpiStart(const char* const* args, uint8_t argc, Harness::IResponseSink& out)
{
    if (m_access != nullptr) m_access->rawCommand("SPI_START", args, argc, out);
    const Harness::TaskHandles& h = Harness::taskHandles();
    if (m_scheduler != nullptr && h.motor != nullptr) {
        m_scheduler->resumeTask(h.motor);
        out.writeLine("Motor task resumed.");
    }
}

void DebugService::cmdLcdDisable(const char* const* args, uint8_t argc, Harness::IResponseSink& out)
{
    out.writeLine("Disabling LCD and display task...");
    const Harness::TaskHandles& h = Harness::taskHandles();
    if (m_scheduler != nullptr && h.display != nullptr) {
        m_scheduler->suspendTask(h.display);
        out.writeLine("Display task suspended.");
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
             "ACC=%u DEC=%u MAXSPD=%u POS=%ld",
             (unsigned)info.status, hiZ, busy, cmdErr ? " CMDERR" : "",
             uvlo ? " UVLO" : "",
             thStatus >= 2 ? " TH_SD" : (thStatus >= 1 ? " TH_WARN" : ""),
             ocd ? " OCD" : "", stepLossA ? " STALL_A" : "",
             stepLossB ? " STALL_B" : "", (unsigned)info.kvalHold,
             (unsigned)info.kvalRun, (unsigned)info.kvalAcc,
             (unsigned)info.kvalDec, (unsigned)info.accel, (unsigned)info.decel,
             (unsigned)info.maxSpeed, (long)info.absPos);
    out.writeLine(buf);
}

} // namespace Services
