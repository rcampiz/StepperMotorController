/**
 * @file service_dispatcher.hpp
 * @brief Wiring-layer implementation of ICommandDispatcher
 *
 * Delegates ICommandDispatcher methods to L3 service APIs.
 * Lives in wiring/ (composition root — can include anything).
 * Method implementations split across per-domain .cpp files.
 */

#pragma once

#include "F_platform/dispatch/icommand_dispatcher.hpp"
#include "F_platform/hal/iencoder.hpp"

class ServiceDispatcher : public Comms::ICommandDispatcher {
public:
    explicit ServiceDispatcher(Services::IEncoder* encoder = nullptr)
        : m_encoder(encoder) {}

    void setEncoder(Services::IEncoder* encoder) { m_encoder = encoder; }

    // Motion (MOT namespace)
    Comms::ServiceStatus motionRun(uint32_t stepsPerSec, bool forward) override;
    Comms::ServiceStatus motionMove(int32_t steps, uint32_t maxSpeedSps) override;
    Comms::ServiceStatus motionGoTo(int32_t position, uint32_t maxSpeedSps) override;
    Comms::ServiceStatus motionStop(bool hard) override;
    Comms::ServiceStatus motionEnable() override;
    Comms::ServiceStatus motionDisable() override;
    Comms::ServiceStatus motionHome(uint32_t maxSpeedSps) override;
    Comms::ServiceStatus motionZero() override;

    // Config (MOT:CFG)
    Comms::ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) override;
    Comms::ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) override;
    Comms::ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) override;
    uint32_t getMicrostepsPerRev() override;
    uint16_t getFullStepsPerRev() override;
    bool configSaveToFlash() override;
    bool configLoadFromFlash() override;
    bool configFactoryReset() override;
    bool configIsValid() override;
    void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) override;
    void configSetOcdThreshold(uint8_t thresh) override;
    void configSetStallThreshold(uint8_t thresh) override;
    void configSetFaultAction(uint8_t action) override;
    void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                    bool uvlo, bool stallA, bool stallB, bool cmdErr) override;
    void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) override;
    void configSetStepMode(uint8_t mode) override;
    void configSetFullStepsPerRev(uint16_t steps) override;
    void configSetEncoderPPR(uint16_t ppr) override;
    uint16_t configGetEncoderPPR() override;
    MotorConfigSnapshot getMotorConfig() override;

    // Safety (SYST namespace)
    void safetyEstop() override;
    Comms::ServiceStatus safetyClearFault(char* activeFaults, uint32_t bufSize) override;
    Comms::ServiceStatus safetyForceClearFault() override;
    uint32_t safetySetHeartbeatTimeout(uint32_t ms) override;
    void safetyHeartbeatReceived(uint32_t seq) override;
    HeartbeatStatus safetyGetHeartbeatStatus() override;

    // Queue / Sync (SYNC namespace)
    Comms::ServiceStatus queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) override;
    Comms::ServiceStatus queueMove(int32_t signedSteps) override;
    Comms::ServiceStatus queueGoTo(int32_t position) override;
    Comms::ServiceStatus queueRun(uint32_t speedRaw, int32_t direction) override;
    Comms::ServiceStatus queueStop(bool hard) override;
    Comms::ServiceStatus queueHome() override;
    Comms::ServiceStatus queueZero() override;
    Comms::ServiceStatus queueArm() override;
    Comms::ServiceStatus queueStart() override;
    Comms::ServiceStatus queueStartAt(uint32_t targetTick) override;
    Comms::ServiceStatus queueClear() override;
    uint8_t getControllerState() override;
    uint32_t getQueueDepth() override;

    // Control mode (CTRL namespace)
    uint8_t getControlMode() override;
    uint8_t getEncoderStatus() override;
    Comms::ServiceStatus setControlMode(uint8_t mode) override;

    // Motor driver (MOTOR_REINIT, DRV namespace)
    FaultEnableFlags getFaultEnable() override;
    void motorReinit() override;
    bool motorApplyConfig() override;
    bool motorGetDebugInfo(MotorDebugParams& out) override;
    bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) override;

    // Encoder (CTRL:ENC namespace)
    bool isEncoderAvailable() override;
    EncoderSnapshot getEncoderSnapshot() override;
    void encoderResetCount() override;
    void encFilterGetConfig(EncFilterParams& out) override;
    void encFilterSetConfig(const EncFilterParams& cfg) override;
    void encFilterSetLegacy(uint8_t type, uint8_t param) override;
    void configSetEncFilter(uint8_t type, uint8_t alpha) override;
    void configSetEncSmaWindow(uint8_t window) override;
    void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                 uint8_t smaWindow, uint8_t measWindowMs,
                                 uint8_t rateDiv) override;

    // Display (UI:DISP namespace)
    bool displayIsRemoteMode() override;
    uint8_t displayGetMode() override;
    Comms::ServiceStatus displaySetMode(uint8_t mode) override;
    void displayClear(uint16_t color) override;
    void displayText(uint16_t x, uint16_t y, const char* text,
                      uint16_t fg, uint16_t bg) override;
    void displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color, bool filled) override;
    void displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      uint16_t color) override;
    void displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t* data, uint32_t len) override;
    void displayIndicator(uint16_t angle, int8_t rotation, bool translation) override;
    bool displayStreamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
    void displayStreamData(const uint8_t* data, uint32_t len) override;
    void displayStreamEnd() override;

    // Flash image (FLASH_* commands)
    bool flashIsAvailable() override;
    FlashInfo flashGetInfo() override;
    uint32_t flashMaxSlots() override;
    bool flashEraseSlot(uint32_t slot) override;
    bool flashWriteSlotData(uint32_t slot, uint32_t offset,
                            const uint8_t* data, size_t len) override;
    bool flashReadSlotChunk(uint32_t slot, uint32_t offset,
                            uint8_t* buf, size_t len) override;
    bool flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                  uint8_t* buf, size_t len) override;
    void flashReadSlotChunkFinish() override;
    bool flashEraseAll() override;
    uint32_t flashSlotAddress(uint32_t slot) override;

    // Supervisor / Trim / SysId (CTRL namespace)
    FollowThresholds getFollowThresholds() override;
    void setFollowThresholds(const FollowThresholds& t) override;
    void applySupervisorConfig() override;
    bool supervisorClearFault() override;
    SupervisorTelemetry getSupervisorTelemetry() override;
    TrimConfig getTrimConfig() override;
    void setTrimGains(int16_t kp100, int16_t ki100) override;
    void setTrimLimits(uint16_t outLim, uint16_t iLim) override;
    void setTrimMaxPct(int16_t val) override;
    void resetTrim() override;
    void applyTrimConfig() override;
    bool sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                     bool forward, uint16_t durationMs, uint16_t settleMs,
                     uint16_t freqMHz) override;
    void sysIdAbort() override;
    uint8_t sysIdGetPhase() override;
    uint16_t sysIdGetSampleCount() override;
    const Comms::SysIdSampleData* sysIdGetSamples(uint16_t& count) override;

    // System (timing, device, events)
    uint32_t getTickUs() override;
    uint32_t getTickMs() override;
    void setTransportDelay(uint32_t ms) override;
    uint32_t getTransportDelay() override;
    void getDeviceInfo(uint16_t& deviceId, uint8_t& role) override;
    bool setDeviceId(uint16_t id) override;
    Comms::ServiceStatus setRole(uint8_t role) override;
    void enableEvents(uint8_t mask, uint16_t currentStatus) override;
    void disableEvents() override;
    EventStats getEventStats() override;
    uint32_t getLastEventSeq() override;

    // Trace (DBG:TRACE namespace)
    uint32_t traceGetCount() override;
    bool traceGetEntry(uint32_t index, TraceEntryData& out) override;
    void traceReset() override;
    void traceRecordEntry(const char* tag, uint32_t arg0 = 0) override;
    void traceRecordExit(const char* tag, uint32_t arg0 = 0) override;

private:
    Services::IEncoder* m_encoder;
};
