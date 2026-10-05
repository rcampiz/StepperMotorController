/**
 * @file debug_service.hpp
 * @brief L3 DebugService — bring-up commands behind DBG:UNLOCK (replaces harness DebugCommandHandler)
 *
 * Locked by default. Task-aware commands (SPI_STOP/SPI_START suspend the Motor task,
 * LCD_DISABLE suspends the Display task, MOTOR_DEBUG reads the controller) are handled
 * here with Platform primitives and the task handles from H_TASKS_L3; every register-level
 * command is forwarded to Drivers::IDebugAccess (H_L3_L4), whose implementation lives in L5.
 */
#pragma once

#include "L3_services/boundary/idebug_dispatcher.hpp"
#include "L3_services/boundary/imotor_control_service.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/defs/harness_tasks_l3.hpp"
#include "F_platform/boundary/ischeduler.hpp"

namespace Services {

class DebugService : public IDebugDispatcher {
public:
    void init(Drivers::IDebugAccess* access,
              Platform::IScheduler& scheduler,
              IMotorControlService& motorCtrl);

    // IDebugDispatcher
    Harness::ServiceStatus debugUnlock() override;
    Harness::ServiceStatus debugLock() override;
    bool debugIsUnlocked() const override { return m_unlocked; }
    bool debugDispatch(const char* cmd, const char* const* args, uint8_t argCount,
                       Harness::IResponseSink& out) override;
    int debugFormatSpi2Diag(char* buf, size_t bufSize) override;

private:
    bool isTaskAwareCommand(const char* cmd) const;
    void cmdSpiStop(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdSpiStart(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdLcdDisable(const char* const* args, uint8_t argc, Harness::IResponseSink& out);
    void cmdMotorDebug(Harness::IResponseSink& out);

    Drivers::IDebugAccess* m_access = nullptr;
    Platform::IScheduler*  m_scheduler = nullptr;
    IMotorControlService*  m_motorCtrl = nullptr;
    bool m_unlocked = false;
};

extern DebugService g_debugService;

} // namespace Services
