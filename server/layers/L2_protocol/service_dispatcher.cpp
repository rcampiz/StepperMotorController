/**
 * @file service_dispatcher.cpp
 * @brief L2 forwarding dispatcher — all methods delegate to sub-interface pointers
 */

#include "service_dispatcher.hpp"

namespace Protocol {

// ── IMotionDispatcher ───────────────────────────────────────────────

Harness::ServiceStatus ServiceDispatcher::motionRun(uint32_t stepsPerSec, bool forward) {
    return m_motion->motionRun(stepsPerSec, forward);
}

Harness::ServiceStatus ServiceDispatcher::motionMove(int32_t steps, uint32_t maxSpeedSps) {
    return m_motion->motionMove(steps, maxSpeedSps);
}

Harness::ServiceStatus ServiceDispatcher::motionGoTo(int32_t position, uint32_t maxSpeedSps) {
    return m_motion->motionGoTo(position, maxSpeedSps);
}

Harness::ServiceStatus ServiceDispatcher::motionStop(bool hard) {
    return m_motion->motionStop(hard);
}

Harness::ServiceStatus ServiceDispatcher::motionEnable() {
    return m_motion->motionEnable();
}

Harness::ServiceStatus ServiceDispatcher::motionDisable() {
    return m_motion->motionDisable();
}

Harness::ServiceStatus ServiceDispatcher::motionHome(uint32_t maxSpeedSps) {
    return m_motion->motionHome(maxSpeedSps);
}

Harness::ServiceStatus ServiceDispatcher::motionZero() {
    return m_motion->motionZero();
}

// ── IMotorConfigDispatcher ──────────────────────────────────────────

Harness::ServiceStatus ServiceDispatcher::configSetAccelPhysical(uint32_t stepsPerS2) {
    return m_config->configSetAccelPhysical(stepsPerS2);
}

Harness::ServiceStatus ServiceDispatcher::configSetDecelPhysical(uint32_t stepsPerS2) {
    return m_config->configSetDecelPhysical(stepsPerS2);
}

Harness::ServiceStatus ServiceDispatcher::configSetMaxSpeedPhysical(uint32_t stepsPerS) {
    return m_config->configSetMaxSpeedPhysical(stepsPerS);
}

uint32_t ServiceDispatcher::getMicrostepsPerRev() {
    return m_config->getMicrostepsPerRev();
}

uint16_t ServiceDispatcher::getFullStepsPerRev() {
    return m_config->getFullStepsPerRev();
}

bool ServiceDispatcher::configSaveToFlash() {
    return m_config->configSaveToFlash();
}

bool ServiceDispatcher::configLoadFromFlash() {
    return m_config->configLoadFromFlash();
}

bool ServiceDispatcher::configFactoryReset() {
    return m_config->configFactoryReset();
}

bool ServiceDispatcher::configIsValid() {
    return m_config->configIsValid();
}

void ServiceDispatcher::configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    m_config->configSetKval(hold, run, acc, dec);
}

void ServiceDispatcher::configSetOcdThreshold(uint8_t thresh) {
    m_config->configSetOcdThreshold(thresh);
}

void ServiceDispatcher::configSetStallThreshold(uint8_t thresh) {
    m_config->configSetStallThreshold(thresh);
}

void ServiceDispatcher::configSetFaultAction(uint8_t action) {
    m_config->configSetFaultAction(action);
}

void ServiceDispatcher::configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                                   bool uvlo, bool stallA, bool stallB, bool cmdErr) {
    m_config->configSetFaultEnableFlags(ocd, thermalSD, thermalWarn, uvlo, stallA, stallB, cmdErr);
}

void ServiceDispatcher::configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
    m_config->configSetMotionParams(acc, dec, maxSpd);
}

void ServiceDispatcher::configSetStepMode(uint8_t mode) {
    m_config->configSetStepMode(mode);
}

void ServiceDispatcher::configSetFullStepsPerRev(uint16_t steps) {
    m_config->configSetFullStepsPerRev(steps);
}

void ServiceDispatcher::configSetEncoderPPR(uint16_t ppr) {
    m_config->configSetEncoderPPR(ppr);
}

uint16_t ServiceDispatcher::configGetEncoderPPR() {
    return m_config->configGetEncoderPPR();
}

Harness::IMotorConfigDispatcher::MotorConfigSnapshot ServiceDispatcher::getMotorConfig() {
    return m_config->getMotorConfig();
}

// ── ISafetyDispatcher ───────────────────────────────────────────────

void ServiceDispatcher::safetyEstop() {
    m_safety->safetyEstop();
}

Harness::ServiceStatus ServiceDispatcher::safetyClearFault(char* activeFaults, uint32_t bufSize) {
    return m_safety->safetyClearFault(activeFaults, bufSize);
}

Harness::ServiceStatus ServiceDispatcher::safetyForceClearFault() {
    return m_safety->safetyForceClearFault();
}

uint32_t ServiceDispatcher::safetySetHeartbeatTimeout(uint32_t ms) {
    return m_safety->safetySetHeartbeatTimeout(ms);
}

void ServiceDispatcher::safetyHeartbeatReceived(uint32_t seq) {
    m_safety->safetyHeartbeatReceived(seq);
}

Harness::ISafetyDispatcher::HeartbeatStatus ServiceDispatcher::safetyGetHeartbeatStatus() {
    return m_safety->safetyGetHeartbeatStatus();
}

// ── IQueueDispatcher ────────────────────────────────────────────────

Harness::ServiceStatus ServiceDispatcher::queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) {
    return m_queue->queueCommand(cmdType, p1, p2);
}

Harness::ServiceStatus ServiceDispatcher::queueMove(int32_t signedSteps) {
    return m_queue->queueMove(signedSteps);
}

Harness::ServiceStatus ServiceDispatcher::queueGoTo(int32_t position) {
    return m_queue->queueGoTo(position);
}

Harness::ServiceStatus ServiceDispatcher::queueRun(uint32_t speedRaw, int32_t direction) {
    return m_queue->queueRun(speedRaw, direction);
}

Harness::ServiceStatus ServiceDispatcher::queueStop(bool hard) {
    return m_queue->queueStop(hard);
}

Harness::ServiceStatus ServiceDispatcher::queueHome() {
    return m_queue->queueHome();
}

Harness::ServiceStatus ServiceDispatcher::queueZero() {
    return m_queue->queueZero();
}

Harness::ServiceStatus ServiceDispatcher::queueArm() {
    return m_queue->queueArm();
}

Harness::ServiceStatus ServiceDispatcher::queueStart() {
    return m_queue->queueStart();
}

Harness::ServiceStatus ServiceDispatcher::queueStartAt(uint32_t targetTick) {
    return m_queue->queueStartAt(targetTick);
}

Harness::ServiceStatus ServiceDispatcher::queueClear() {
    return m_queue->queueClear();
}

uint8_t ServiceDispatcher::getControllerState() {
    return m_queue->getControllerState();
}

uint32_t ServiceDispatcher::getQueueDepth() {
    return m_queue->getQueueDepth();
}

// ── IControlModeDispatcher ──────────────────────────────────────────

uint8_t ServiceDispatcher::getControlMode() {
    return m_ctrlMode->getControlMode();
}

uint8_t ServiceDispatcher::getEncoderStatus() {
    return m_ctrlMode->getEncoderStatus();
}

Harness::ServiceStatus ServiceDispatcher::setControlMode(uint8_t mode) {
    return m_ctrlMode->setControlMode(mode);
}

// ── ISupervisorTrimDispatcher ───────────────────────────────────────

Harness::ISupervisorTrimDispatcher::FollowThresholds ServiceDispatcher::getFollowThresholds() {
    return m_supTrim->getFollowThresholds();
}

void ServiceDispatcher::setFollowThresholds(const FollowThresholds& t) {
    m_supTrim->setFollowThresholds(t);
}

void ServiceDispatcher::applySupervisorConfig() {
    m_supTrim->applySupervisorConfig();
}

bool ServiceDispatcher::supervisorClearFault() {
    return m_supTrim->supervisorClearFault();
}

Harness::ISupervisorTrimDispatcher::SupervisorTelemetry ServiceDispatcher::getSupervisorTelemetry() {
    return m_supTrim->getSupervisorTelemetry();
}

Harness::ISupervisorTrimDispatcher::TrimConfig ServiceDispatcher::getTrimConfig() {
    return m_supTrim->getTrimConfig();
}

void ServiceDispatcher::setTrimGains(int16_t kp100, int16_t ki100) {
    m_supTrim->setTrimGains(kp100, ki100);
}

void ServiceDispatcher::setTrimLimits(uint16_t outLim, uint16_t iLim) {
    m_supTrim->setTrimLimits(outLim, iLim);
}

void ServiceDispatcher::setTrimMaxPct(int16_t val) {
    m_supTrim->setTrimMaxPct(val);
}

void ServiceDispatcher::resetTrim() {
    m_supTrim->resetTrim();
}

void ServiceDispatcher::applyTrimConfig() {
    m_supTrim->applyTrimConfig();
}

bool ServiceDispatcher::sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                                    bool forward, uint16_t durationMs, uint16_t settleMs,
                                    uint16_t freqMHz) {
    return m_supTrim->sysIdStart(type, targetSpeed, startSpeed, forward, durationMs, settleMs, freqMHz);
}

void ServiceDispatcher::sysIdAbort() {
    m_supTrim->sysIdAbort();
}

uint8_t ServiceDispatcher::sysIdGetPhase() {
    return m_supTrim->sysIdGetPhase();
}

uint16_t ServiceDispatcher::sysIdGetSampleCount() {
    return m_supTrim->sysIdGetSampleCount();
}

const Harness::SysIdSampleData* ServiceDispatcher::sysIdGetSamples(uint16_t& count) {
    return m_supTrim->sysIdGetSamples(count);
}

// ── IEncoderDispatcher ──────────────────────────────────────────────

bool ServiceDispatcher::isEncoderAvailable() {
    return m_encoder->isEncoderAvailable();
}

Harness::IEncoderDispatcher::EncoderSnapshot ServiceDispatcher::getEncoderSnapshot() {
    return m_encoder->getEncoderSnapshot();
}

void ServiceDispatcher::encoderResetCount() {
    m_encoder->encoderResetCount();
}

void ServiceDispatcher::encFilterGetConfig(EncFilterParams& out) {
    m_encoder->encFilterGetConfig(out);
}

void ServiceDispatcher::encFilterSetConfig(const EncFilterParams& cfg) {
    m_encoder->encFilterSetConfig(cfg);
}

void ServiceDispatcher::encFilterSetLegacy(uint8_t type, uint8_t param) {
    m_encoder->encFilterSetLegacy(type, param);
}

void ServiceDispatcher::configSetEncFilter(uint8_t type, uint8_t alpha) {
    m_encoder->configSetEncFilter(type, alpha);
}

void ServiceDispatcher::configSetEncSmaWindow(uint8_t window) {
    m_encoder->configSetEncSmaWindow(window);
}

void ServiceDispatcher::configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                                uint8_t smaWindow, uint8_t measWindowMs,
                                                uint8_t rateDiv) {
    m_encoder->configSetEncFilterFull(flags, emaAlpha, smaWindow, measWindowMs, rateDiv);
}

// ── IMotorDriverDispatcher ──────────────────────────────────────────

Harness::IMotorDriverDispatcher::FaultEnableFlags ServiceDispatcher::getFaultEnable() {
    return m_motorDrv->getFaultEnable();
}

void ServiceDispatcher::motorReinit() {
    m_motorDrv->motorReinit();
}

bool ServiceDispatcher::motorApplyConfig() {
    return m_motorDrv->motorApplyConfig();
}

bool ServiceDispatcher::motorGetDebugInfo(Harness::MotorDebugParams& out) {
    return m_motorDrv->motorGetDebugInfo(out);
}

bool ServiceDispatcher::motorSetStepModeSafe(uint8_t mode, uint8_t& readback) {
    return m_motorDrv->motorSetStepModeSafe(mode, readback);
}

// ── IDisplayDispatcher ──────────────────────────────────────────────

bool ServiceDispatcher::displayIsRemoteMode() {
    return m_display->displayIsRemoteMode();
}

uint8_t ServiceDispatcher::displayGetMode() {
    return m_display->displayGetMode();
}

Harness::ServiceStatus ServiceDispatcher::displaySetMode(uint8_t mode) {
    return m_display->displaySetMode(mode);
}

void ServiceDispatcher::displayClear(uint16_t color) {
    m_display->displayClear(color);
}

void ServiceDispatcher::displayText(uint16_t x, uint16_t y, const char* text,
                                     uint16_t fg, uint16_t bg) {
    m_display->displayText(x, y, text, fg, bg);
}

void ServiceDispatcher::displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                     uint16_t color, bool filled) {
    m_display->displayRect(x, y, w, h, color, filled);
}

void ServiceDispatcher::displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                                     uint16_t color) {
    m_display->displayLine(x0, y0, x1, y1, color);
}

void ServiceDispatcher::displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                       const uint8_t* data, uint32_t len) {
    m_display->displayBitmap(x, y, w, h, data, len);
}

void ServiceDispatcher::displayIndicator(uint16_t angle, int8_t rotation, bool translation) {
    m_display->displayIndicator(angle, rotation, translation);
}

bool ServiceDispatcher::displayStreamStart(uint16_t x, uint16_t y,
                                            uint16_t w, uint16_t h) {
    return m_display->displayStreamStart(x, y, w, h);
}

void ServiceDispatcher::displayStreamData(const uint8_t* data, uint32_t len) {
    m_display->displayStreamData(data, len);
}

void ServiceDispatcher::displayStreamEnd() {
    m_display->displayStreamEnd();
}

// ── IFlashImageDispatcher ───────────────────────────────────────────

bool ServiceDispatcher::flashIsAvailable() {
    return m_flash->flashIsAvailable();
}

Harness::IFlashImageDispatcher::FlashInfo ServiceDispatcher::flashGetInfo() {
    return m_flash->flashGetInfo();
}

uint32_t ServiceDispatcher::flashMaxSlots() {
    return m_flash->flashMaxSlots();
}

bool ServiceDispatcher::flashEraseSlot(uint32_t slot) {
    return m_flash->flashEraseSlot(slot);
}

bool ServiceDispatcher::flashWriteSlotData(uint32_t slot, uint32_t offset,
                                            const uint8_t* data, size_t len) {
    return m_flash->flashWriteSlotData(slot, offset, data, len);
}

bool ServiceDispatcher::flashReadSlotChunk(uint32_t slot, uint32_t offset,
                                            uint8_t* buf, size_t len) {
    return m_flash->flashReadSlotChunk(slot, offset, buf, len);
}

bool ServiceDispatcher::flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                                 uint8_t* buf, size_t len) {
    return m_flash->flashReadSlotChunkStart(slot, offset, buf, len);
}

void ServiceDispatcher::flashReadSlotChunkFinish() {
    m_flash->flashReadSlotChunkFinish();
}

bool ServiceDispatcher::flashEraseAll() {
    return m_flash->flashEraseAll();
}

uint32_t ServiceDispatcher::flashSlotAddress(uint32_t slot) {
    return m_flash->flashSlotAddress(slot);
}

// ── ISystemDispatcher ───────────────────────────────────────────────

uint32_t ServiceDispatcher::getTickUs() {
    return m_system->getTickUs();
}

uint32_t ServiceDispatcher::getTickMs() {
    return m_system->getTickMs();
}

void ServiceDispatcher::setTransportDelay(uint32_t ms) {
    m_system->setTransportDelay(ms);
}

uint32_t ServiceDispatcher::getTransportDelay() {
    return m_system->getTransportDelay();
}

void ServiceDispatcher::getDeviceInfo(uint16_t& deviceId, uint8_t& role) {
    m_system->getDeviceInfo(deviceId, role);
}

bool ServiceDispatcher::setDeviceId(uint16_t id) {
    return m_system->setDeviceId(id);
}

Harness::ServiceStatus ServiceDispatcher::setRole(uint8_t role) {
    return m_system->setRole(role);
}

void ServiceDispatcher::enableEvents(uint8_t mask, uint16_t currentStatus) {
    m_system->enableEvents(mask, currentStatus);
}

void ServiceDispatcher::disableEvents() {
    m_system->disableEvents();
}

Harness::ISystemDispatcher::EventStats ServiceDispatcher::getEventStats() {
    return m_system->getEventStats();
}

uint32_t ServiceDispatcher::getLastEventSeq() {
    return m_system->getLastEventSeq();
}

// ── ITraceDispatcher ────────────────────────────────────────────────

uint32_t ServiceDispatcher::traceGetCount() {
    return m_trace->traceGetCount();
}

bool ServiceDispatcher::traceGetEntry(uint32_t index, TraceEntryData& out) {
    return m_trace->traceGetEntry(index, out);
}

void ServiceDispatcher::traceReset() {
    m_trace->traceReset();
}

void ServiceDispatcher::traceRecordEntry(const char* tag, uint32_t arg0) {
    m_trace->traceRecordEntry(tag, arg0);
}

void ServiceDispatcher::traceRecordExit(const char* tag, uint32_t arg0) {
    m_trace->traceRecordExit(tag, arg0);
}

} // namespace Protocol
