/**
 * @file service_dispatcher.hpp
 * @brief L2 forwarding dispatcher — ICommandDispatcher aggregator
 *
 * Thin forwarding class holding 12 sub-interface pointers.
 * All methods delegate to the appropriate L3 dispatch implementation
 * through harness sub-interfaces. No L3 includes, no service logic.
 */

#pragma once

#include "harness/pins/icommand_dispatcher.hpp"

namespace Protocol {

class ServiceDispatcher : public Harness::ICommandDispatcher {
public:
    // Dependency injection — 12 sub-interface setters
    void setMotion(Harness::IMotionDispatcher* d)                { m_motion = d; }
    void setConfig(Harness::IMotorConfigDispatcher* d)           { m_config = d; }
    void setSafety(Harness::ISafetyDispatcher* d)                { m_safety = d; }
    void setQueue(Harness::IQueueDispatcher* d)                  { m_queue = d; }
    void setControlMode(Harness::IControlModeDispatcher* d)      { m_ctrlMode = d; }
    void setSupervisorTrim(Harness::ISupervisorTrimDispatcher* d){ m_supTrim = d; }
    void setEncoder(Harness::IEncoderDispatcher* d)              { m_encoder = d; }
    void setMotorDriver(Harness::IMotorDriverDispatcher* d)      { m_motorDrv = d; }
    void setDisplay(Harness::IDisplayDispatcher* d)              { m_display = d; }
    void setFlash(Harness::IFlashImageDispatcher* d)             { m_flash = d; }
    void setSystem(Harness::ISystemDispatcher* d)                { m_system = d; }
    void setTrace(Harness::ITraceDispatcher* d)                  { m_trace = d; }

    // IMotionDispatcher
    Harness::ServiceStatus motionRun(uint32_t stepsPerSec, bool forward) override;
    Harness::ServiceStatus motionMove(int32_t steps, uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionGoTo(int32_t position, uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionStop(bool hard) override;
    Harness::ServiceStatus motionEnable() override;
    Harness::ServiceStatus motionDisable() override;
    Harness::ServiceStatus motionHome(uint32_t maxSpeedSps) override;
    Harness::ServiceStatus motionZero() override;

    // IMotorConfigDispatcher
    Harness::ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) override;
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

    // ISafetyDispatcher
    void safetyEstop() override;
    Harness::ServiceStatus safetyClearFault(char* activeFaults, uint32_t bufSize) override;
    Harness::ServiceStatus safetyForceClearFault() override;
    uint32_t safetySetHeartbeatTimeout(uint32_t ms) override;
    void safetyHeartbeatReceived(uint32_t seq) override;
    HeartbeatStatus safetyGetHeartbeatStatus() override;

    // IQueueDispatcher
    Harness::ServiceStatus queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) override;
    Harness::ServiceStatus queueMove(int32_t signedSteps) override;
    Harness::ServiceStatus queueGoTo(int32_t position) override;
    Harness::ServiceStatus queueRun(uint32_t speedRaw, int32_t direction) override;
    Harness::ServiceStatus queueStop(bool hard) override;
    Harness::ServiceStatus queueHome() override;
    Harness::ServiceStatus queueZero() override;
    Harness::ServiceStatus queueArm() override;
    Harness::ServiceStatus queueStart() override;
    Harness::ServiceStatus queueStartAt(uint32_t targetTick) override;
    Harness::ServiceStatus queueClear() override;
    uint8_t getControllerState() override;
    uint32_t getQueueDepth() override;

    // IControlModeDispatcher
    uint8_t getControlMode() override;
    uint8_t getEncoderStatus() override;
    Harness::ServiceStatus setControlMode(uint8_t mode) override;

    // ISupervisorTrimDispatcher
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
    const Harness::SysIdSampleData* sysIdGetSamples(uint16_t& count) override;

    // IEncoderDispatcher
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

    // IMotorDriverDispatcher
    FaultEnableFlags getFaultEnable() override;
    void motorReinit() override;
    bool motorApplyConfig() override;
    bool motorGetDebugInfo(Harness::MotorDebugParams& out) override;
    bool motorSetStepModeSafe(uint8_t mode, uint8_t& readback) override;

    // IDisplayDispatcher
    bool displayIsRemoteMode() override;
    uint8_t displayGetMode() override;
    Harness::ServiceStatus displaySetMode(uint8_t mode) override;
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

    // IFlashImageDispatcher
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

    // ISystemDispatcher
    uint32_t getTickUs() override;
    uint32_t getTickMs() override;
    void setTransportDelay(uint32_t ms) override;
    uint32_t getTransportDelay() override;
    void getDeviceInfo(uint16_t& deviceId, uint8_t& role) override;
    bool setDeviceId(uint16_t id) override;
    Harness::ServiceStatus setRole(uint8_t role) override;
    void enableEvents(uint8_t mask, uint16_t currentStatus) override;
    void disableEvents() override;
    EventStats getEventStats() override;
    uint32_t getLastEventSeq() override;

    // ITraceDispatcher
    uint32_t traceGetCount() override;
    bool traceGetEntry(uint32_t index, TraceEntryData& out) override;
    void traceReset() override;
    void traceRecordEntry(const char* tag, uint32_t arg0 = 0) override;
    void traceRecordExit(const char* tag, uint32_t arg0 = 0) override;

private:
    Harness::IMotionDispatcher*         m_motion = nullptr;
    Harness::IMotorConfigDispatcher*    m_config = nullptr;
    Harness::ISafetyDispatcher*         m_safety = nullptr;
    Harness::IQueueDispatcher*          m_queue = nullptr;
    Harness::IControlModeDispatcher*    m_ctrlMode = nullptr;
    Harness::ISupervisorTrimDispatcher* m_supTrim = nullptr;
    Harness::IEncoderDispatcher*        m_encoder = nullptr;
    Harness::IMotorDriverDispatcher*    m_motorDrv = nullptr;
    Harness::IDisplayDispatcher*        m_display = nullptr;
    Harness::IFlashImageDispatcher*     m_flash = nullptr;
    Harness::ISystemDispatcher*         m_system = nullptr;
    Harness::ITraceDispatcher*          m_trace = nullptr;
};

} // namespace Protocol
