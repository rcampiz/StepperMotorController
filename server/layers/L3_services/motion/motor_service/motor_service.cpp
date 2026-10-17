/**
 * @file motor_service.cpp
 * @brief L3 MotorService implementation (Motor task body, ESTOP latch, step-mode message, debug pause)
 *
 * ESTOP contract (Comms task -> Motor task):
 *   immediateHardHiZ()   latches m_stopLatched, empties the motor queue, drives Hi-Z now.
 *   onCommand()          drops motion commands while latched (config/stop/status still pass).
 *   applyPendingTransitions()  runs the Motor-task side once per edge: onEmergencyStop()
 *                        idles sysid/trim/supervisor, inhibits the control loops and
 *                        re-asserts Hi-Z; clearEmergencyStop() re-enables them after CLEAR.
 *
 * Step mode (Comms task -> Motor task -> Comms task): setStepModeSafe() queues a SetStepMode
 * command and waits (bounded) for the readback the Motor task publishes in an atomic slot.
 * Debug pause (SPI_STOP/SPI_START): a flag; while set the Motor task does not touch the driver.
 *
 * All MotorController mutation stays on the Motor task. The Comms task only touches the
 * atomics, the queue and the driver's hardHiZ() (one SPI transaction under the SPI1 mutex).
 * Residual window: a command the Motor task dequeued just before the latch can still reach
 * the driver; the pending onEmergencyStop() re-asserts Hi-Z on the next pass (<= 50 ms).
 */

#include "L3_services/motion/motor_service/motor_service.hpp"
#include "L3_services/motion/motor_controller/motor_controller.hpp"
#include "L3_services/motion/motion.hpp"
#include "L3_services/infra/tick_timer/tick_timer.hpp"
#include "harness/pins/imicrosecond_clock.hpp"

namespace Services {

MotorService g_motorService;

void MotorService::init(IMotorControlService& controller,
                        MotorController& rawController,
                        CmdQueue& queue,
                        Platform::IClock& clock,
                        Platform::ITaskStats* taskStats,
                        TelemetryStore& telemetry)
{
    m_controller = &controller;
    m_rawController = &rawController;
    m_queue = &queue;
    m_clock = &clock;
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
        noteHiZDone();
        if (m_standby.load()) {
            m_rawController->publishStandbySnapshot();
        }
    }
    if (m_clearPending.exchange(false)) {
        if (m_standby.load()) {
            // Out-of-band stop: the chip sat in standby with its registers reset. Release the
            // pin, wait, re-initialise and re-apply the config before motion is allowed again.
            m_rawController->wakeFromStandby();
            m_standby.store(false);
        }
        m_oobSeen.store(false);
        m_rawController->clearEmergencyStop();
    }
}

void MotorService::oobEstopFromIsr()
{
    Platform::IMicrosecondClock* us = Harness::microsecondClock();
    const uint32_t t0 = (us != nullptr) ? us->getTickUs() : 0;
    // Same order as immediateHardHiZ: latch first, then the stop. The pin is the stop here;
    // the SPI HardHiZ follows on the Motor task (and on the Comms task when the SYST:ESTOP
    // line behind the byte is parsed), both harmless while the chip is in standby.
    m_stopLatched.store(true);
    m_clearPending.store(false);
    m_stopPending.store(true);
    m_standby.store(true);
    if (m_rawController != nullptr) {
        m_rawController->standbyFromIsr();
    }
    m_oobRxUs.store(t0);
    m_oobPinUs.store((us != nullptr) ? us->getTickUs() : 0);
    m_oobHizUs.store(0);
    m_oobSeen.store(true);
}

void MotorService::noteHiZDone()
{
    if (m_oobSeen.load() && m_oobHizUs.load() == 0) {
        Platform::IMicrosecondClock* us = Harness::microsecondClock();
        m_oobHizUs.store((us != nullptr) ? us->getTickUs() : 0);
    }
}

bool MotorService::estopTiming(uint32_t& pinUs, uint32_t& hizUs) const
{
    if (!m_oobSeen.load()) {
        return false;
    }
    const uint32_t rx = m_oobRxUs.load();
    pinUs = m_oobPinUs.load() - rx;
    const uint32_t hiz = m_oobHizUs.load();
    hizUs = (hiz != 0) ? hiz - rx : 0;
    return true;
}

void MotorService::handleSetStepMode(uint8_t mode)
{
    uint8_t readback = 0xFF;
    bool ok = false;
    if (m_controller != nullptr && mode <= 7) {
        ok = m_controller->setStepModeSafe(mode, readback);
    }
    m_stepModeResult.store(STEP_MODE_ANSWERED | (ok ? STEP_MODE_OK : 0u) | readback);
}

void MotorService::handleStartAt(uint32_t targetTick)
{
    // SYNC:START:AT: sleep in 1 ms steps until just before the tick, spin the rest for
    // microsecond precision, then flush the armed queue. The ESTOP path does not come
    // through here (immediateHardHiZ writes the driver from the Comms task and sets the
    // latch), so the wait is abandoned as soon as the latch is set or the start is cancelled.
    if (m_scheduledStart == nullptr) {
        return;
    }
    while (static_cast<int32_t>(targetTick - TickTimer_GetTick()) > 1500) {
        if (m_stopLatched.load() || !m_scheduledStart->scheduledStartPending()) {
            return;
        }
        if (m_clock != nullptr) {
            m_clock->delayMs(1);
        }
    }
    while (static_cast<int32_t>(targetTick - TickTimer_GetTick()) > 0) {
        // spin: the last 1.5 ms
    }
    if (!m_stopLatched.load()) {
        m_scheduledStart->executeScheduledStart();
    }
}

void MotorService::onCommand(const MotorCommand& cmd)
{
    if (m_controller == nullptr) {
        return;
    }
    applyPendingTransitions();
    if (m_debugPaused.load() || m_standby.load()) {
        ++m_refused;                                   // SPI_STOP / standby: no driver traffic at all
        if (isJob(cmd.type)) {
            m_jobResult.store(JOB_ANSWERED);           // answer "not done" so the Comms task does not wait
        }
        return;
    }
    if (isJob(cmd.type)) {
        handleJob(cmd.type);
        return;
    }
    if (cmd.type == MotorCmdType::SetStepMode) {
        handleSetStepMode(static_cast<uint8_t>(cmd.param1));
        return;
    }
    if (cmd.type == MotorCmdType::StartAt) {
        handleStartAt(static_cast<uint32_t>(cmd.param1));
        return;
    }
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
    if (m_standby.load()) {
        m_rawController->publishStandbySnapshot();     // the chip cannot be read: report Hi-Z, stopped
        return;
    }
    if (m_debugPaused.load()) {
        return;                                        // SPI_STOP: no status polls
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

// ---------------------------------------------------------------------------
// Motor-task jobs (Phase 4 step 5). The Comms task never touches the controller itself:
// every driver sequence runs on the Motor task, between its own operations.
// ---------------------------------------------------------------------------

bool MotorService::isJob(MotorCmdType t)
{
    return t == MotorCmdType::Reinit || t == MotorCmdType::ApplyConfig || t == MotorCmdType::GetDebugInfo;
}

bool MotorService::runJob(MotorCmdType type)
{
    if (m_controller == nullptr) {
        return false;
    }
    m_jobResult.store(JOB_PENDING);
    MotorCommand cmd = {};
    cmd.type = type;
    if (!sendCommand(cmd, SEND_TIMEOUT_MS)) {
        return false;
    }
    uint32_t waited = 0;
    uint32_t result = m_jobResult.load();
    while ((result & JOB_ANSWERED) == 0u && m_clock != nullptr && waited < JOB_WAIT_MS) {
        m_clock->delayMs(STEP_MODE_POLL_MS);
        waited += STEP_MODE_POLL_MS;
        result = m_jobResult.load();
    }
    return (result & JOB_ANSWERED) != 0u && (result & JOB_OK) != 0u;
}

void MotorService::handleJob(MotorCmdType type)
{
    bool ok = false;
    if (m_controller != nullptr) {
        switch (type) {
            case MotorCmdType::Reinit:
                m_controller->reinit();
                ok = true;
                break;
            case MotorCmdType::ApplyConfig: {
                ok = m_controller->applyConfig();
                if (!ok) {
                    // Bridges on: STEP_MODE needs the Hi-Z path, which is ours to run here
                    uint8_t readback = 0xFF;
                    ok = m_controller->setStepModeSafe(motion.stepper.config.getStepMode(), readback);
                }
                break;
            }
            case MotorCmdType::GetDebugInfo:
                m_controller->getDebugInfo(m_jobDebugInfo);
                ok = true;
                break;
            default:
                break;
        }
    }
    m_jobResult.store(JOB_ANSWERED | (ok ? JOB_OK : 0u));
}

bool MotorService::lastJobResult(bool& ok) const
{
    const uint32_t result = m_jobResult.load();
    ok = (result & JOB_OK) != 0u;
    return (result & JOB_ANSWERED) != 0u;
}

void MotorService::reinit()
{
    (void)runJob(MotorCmdType::Reinit);
}

bool MotorService::applyConfig()
{
    return runJob(MotorCmdType::ApplyConfig);
}

bool MotorService::getDebugInfo(Harness::MotorDebugParams& out)
{
    if (!runJob(MotorCmdType::GetDebugInfo)) {
        return false;
    }
    out = m_jobDebugInfo;
    return true;
}

bool MotorService::setStepModeSafe(uint8_t mode, uint8_t& readback)
{
    readback = 0xFF;
    if (m_controller == nullptr || mode > 7) {
        return false;
    }
    // Message to the Motor task: it owns the driver, so it rewrites STEP_MODE between its own
    // operations. No suspend/resume.
    m_stepModeResult.store(STEP_MODE_PENDING);
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::SetStepMode;
    cmd.param1 = mode;
    if (!sendCommand(cmd, SEND_TIMEOUT_MS)) {
        return false;
    }
    uint32_t waited = 0;
    uint32_t result = m_stepModeResult.load();
    while ((result & STEP_MODE_ANSWERED) == 0u && m_clock != nullptr && waited < STEP_MODE_WAIT_MS) {
        m_clock->delayMs(STEP_MODE_POLL_MS);
        waited += STEP_MODE_POLL_MS;
        result = m_stepModeResult.load();
    }
    if ((result & STEP_MODE_ANSWERED) == 0u) {
        return false;                                  // Motor task did not answer in time
    }
    readback = static_cast<uint8_t>(result & 0xFFu);
    return (result & STEP_MODE_OK) != 0u;
}

bool MotorService::lastStepModeResult(uint8_t& readback) const
{
    const uint32_t result = m_stepModeResult.load();
    readback = static_cast<uint8_t>(result & 0xFFu);
    return (result & STEP_MODE_ANSWERED) != 0u && (result & STEP_MODE_OK) != 0u;
}

void MotorService::setDebugPaused(bool paused)
{
    m_debugPaused.store(paused);
}

bool MotorService::isDebugPaused() const
{
    return m_debugPaused.load();
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
    noteHiZDone();
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
