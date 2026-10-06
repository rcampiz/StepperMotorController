/**
 * @file debug_service.hpp
 * @brief L3 DebugService — DBG:* bring-up commands behind DBG:UNLOCK (Comms task)
 *
 * Task-aware commands no longer suspend tasks: SPI_STOP/SPI_START set the Motor task's
 * debug-pause flag (IMotorTaskControl), LCD_DISABLE clears the Display task's enable flag
 * (IDisplayControl). Register-level commands are forwarded to Drivers::IDebugAccess (H_L3_L4).
 */
#pragma once

#include "L3_services/boundary/idebug_dispatcher.hpp"
#include "L3_services/boundary/imotor_task_control.hpp"
#include "L3_services/boundary/idisplay_control.hpp"
#include "harness/defs/harness_l3_l4.hpp"

namespace Services {

class DebugService : public IDebugDispatcher {
public:
    void init(Drivers::IDebugAccess* access,
              IMotorTaskControl* motorCtrl,
              IDisplayControl* display);

    // IDebugDispatcher
    Harness::ServiceStatus debugUnlock() override;
    Harness::ServiceStatus debugLock() override;
    bool debugIsUnlocked() const override { return m_unlocked; }
    bool debugDispatch(const char* cmd, const char* const* args, uint8_t argCount,
                       Harness::IResponseSink& out) override;
    int debugFormatSpi2Diag(char* buf, size_t bufSize) override;

private:
    bool isTaskAwareCommand(const char* cmd) const;
    bool isReadOnlyCommand(const char* cmd) const;   ///< dumps that bypass the lock (F3)
    void cmdSpiStop(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdSpiStart(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdLcdDisable(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdMotorDebug(Harness::IResponseSink& out);

    Drivers::IDebugAccess* m_access = nullptr;
    IMotorTaskControl*     m_motorCtrl = nullptr;
    IDisplayControl*       m_display = nullptr;
    bool m_unlocked = false;
};

extern DebugService g_debugService;

} // namespace Services
