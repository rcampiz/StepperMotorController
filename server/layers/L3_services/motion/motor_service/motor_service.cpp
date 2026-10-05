/**
 * @file motor_service.cpp
 * @brief L3 MotorService implementation (Motor task body + ESTOP latch)
 *
 * ESTOP contract (Comms task -> Motor task):
 *   immediateHardHiZ()   latches m_stopLatched, empties the motor queue, drives Hi-Z now.
 *   onCommand()          drops motion commands while latched (config/stop/status still pass).
 *   applyPendingTransitions()  runs the Motor-task side once per edge: onEmergencyStop()
 *                        idles sysid/trim/supervisor, inhibits the control loops and
 *                        re-asserts Hi-Z; clearEmergencyStop() re-enables them after CLEAR.
 *
 * All MotorController mutation stays on the Motor task. The Comms task only touches the
 * atomics, the queue and the driver's hardHiZ() (one SPI transaction under the SPI1 mutex).
 * Residual window: a command the Motor task dequeued just before the latch can still reach
 * the driver; the pending onEmergencyStop() re-asserts Hi-Z on the next pass (<= 50 ms).
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

bool MotorService::isMotionCommand(MotorCmdType t)
{
    switch (t) {
        case MotorCmdType::Move:
        case MotorCmdType::GoTo:
        case MotorCmdType::Run:
        case MotorCmdType::GoHome:
        case MotorCmdType::GoMark:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// IMotorService (Motor task context)
// ---------------------------------------------------------------------------

void MotorService::applyPendingTransitions()
{
    if (m_rawController == nullptr) {
        return;
    }
    if (m_stopPending.exchange(false)) {
        m_rawController->onEmergencyStop();
    }
    if (m_clearPending.exchange(false)) {
        m_rawController->clearEmergencyStop();
    }
}

void MotorService::onCommand(const MotorCommand& cmd)
{
    if (m_controller == nullptr) {
        return;
    }
    applyPendingTransitions();
    if (m_stopLatched.load() && isMotionCommand(cmd.type)) {
        ++m_refused;
        return;
    }
    m_controller->processCommand(cmd);
}

void MotorService::periodicUpdate()
{
    if (m_controller == nullptr) {
        return;
    }
    applyPendingTransitions();
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
    // Order matters: latch first so a command dequeued from now on is refused, then empty
    // the queue so nothing already queued runs after the stop, then stop the outputs.
    m_stopLatched.store(true);
    m_clearPending.store(false);
    m_stopPending.store(true);
    if (m_queue != nullptr) {
        m_queue->reset();
    }
    if (m_rawController != nullptr) {
        m_rawController->emergencyHiZ();
    }
}

void MotorService::clearImmediateStop()
{
    m_stopLatched.store(false);
    m_clearPending.store(true);
}

bool MotorService::isStopLatched() const
{
    return m_stopLatched.load();
}

} // namespace Services
