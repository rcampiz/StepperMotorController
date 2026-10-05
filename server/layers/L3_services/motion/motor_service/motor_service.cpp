/**
 * @file motor_service.cpp
 * @brief L3 MotorService implementation (Motor task body moved from F_platform/tasks/motor_task.cpp)
 */

#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/motion/motor_controller/motor_controller.hpp"

namespace Services {

MotorService g_motorService;

void MotorService::init(IMotorControlService& controller,
                        MotorController& rawController,
                        CmdQueue& queue,
                        Platform::IClock& clock,
                        Platform::IScheduler& scheduler,
                        Platform::ITaskStats* taskStats,
                        TelemetryStore& telemetry)
{
    m_controller = &controller;
    m_rawController = &rawController;
    m_queue = &queue;
    m_clock = &clock;
    m_scheduler = &scheduler;
    m_taskStats = taskStats;
    m_telemetry = &telemetry;
    m_controller->applyConfig();
}

// ---------------------------------------------------------------------------
// IMotorService (Motor task context)
// ---------------------------------------------------------------------------

void MotorService::onCommand(const MotorCommand& cmd)
{
    if (m_controller != nullptr) {
        m_controller->processCommand(cmd);
    }
}

void MotorService::periodicUpdate()
{
    if (m_controller == nullptr) {
        return;
    }
    m_controller->periodicUpdate();

    Protocol::SystemTelemetry sys = {};
    sys.uptimeTicks = (m_clock != nullptr) ? m_clock->getTickMs() : 0;
    sys.freeHeap = (m_taskStats != nullptr) ? m_taskStats->getFreeHeapBytes() : 0;
    sys.cpuLoad = 0;
    if (m_telemetry != nullptr) {
        m_telemetry->writeSystem(sys);
    }
}

// ---------------------------------------------------------------------------
// IMotorCommandSink (Comms task context)
// ---------------------------------------------------------------------------

bool MotorService::sendCommand(const MotorCommand& cmd, uint32_t timeoutMs)
{
    if (m_queue == nullptr) {
        return false;
    }
    if (timeoutMs > SEND_TIMEOUT_MS) {
        timeoutMs = SEND_TIMEOUT_MS;   // never block the Comms task (and its watchdog) for long
    }
    return m_queue->send(cmd, timeoutMs);
}

// ---------------------------------------------------------------------------
// IMotorTaskControl (Comms task context)
// ---------------------------------------------------------------------------

void MotorService::reinit()
{
    if (m_controller != nullptr) {
        m_controller->reinit();
    }
}

bool MotorService::applyConfig()
{
    if (m_controller == nullptr) {
        return false;
    }
    m_controller->applyConfig();
    return true;
}

bool MotorService::getDebugInfo(Harness::MotorDebugParams& out)
{
    if (m_controller == nullptr) {
        return false;
    }
    m_controller->getDebugInfo(out);
    return true;
}

bool MotorService::setStepModeSafe(uint8_t mode, uint8_t& readback)
{
    if (m_controller == nullptr || mode > 7) {
        readback = 0xFF;
        return false;
    }
    // Keep the Motor task away from the driver while STEP_MODE is rewritten
    const Harness::TaskHandles& h = Harness::taskHandles();
    const bool suspend = (m_scheduler != nullptr && h.motor != nullptr);
    if (suspend) m_scheduler->suspendTask(h.motor);
    bool ok = m_controller->setStepModeSafe(mode, readback);
    if (suspend) m_scheduler->resumeTask(h.motor);
    return ok;
}

// ---------------------------------------------------------------------------
// IImmediateStop (ESTOP path, Comms task context)
// ---------------------------------------------------------------------------

void MotorService::immediateHardHiZ()
{
    if (m_rawController != nullptr) {
        m_rawController->emergencyHiZ();
    }
}

} // namespace Services
