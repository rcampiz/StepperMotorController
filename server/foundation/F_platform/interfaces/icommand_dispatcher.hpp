/**
 * @file icommand_dispatcher.hpp
 * @brief Interface for dispatching parsed commands to services
 *
 * Defined in L2 (consumer owns interface). Implemented by
 * ServiceDispatcher in Foundation. Eliminates L3 and Foundation
 * task includes from command_parser.cpp.
 *
 * Return convention:
 *   - Actions return DispatchResult (ok + error message)
 *   - Queries return data directly via return value or out params
 *   - State enums are passed as uint8_t to avoid L3 enum dependencies
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace Comms {

/**
 * @brief Result of a dispatched command
 */
struct DispatchResult {
    bool ok;
    const char* message;  // error string on failure, nullptr on success
};

/**
 * @brief Sysid sample data (mirrors L3 SysIdSample layout)
 */
struct SysIdSampleData {
    uint16_t time_ms;
    int16_t  setpoint_tps;
    int16_t  actual_tps;
    int16_t  pos_error;
};

/**
 * @brief Abstract command dispatcher interface
 *
 * Groups service operations by domain. Methods added incrementally
 * as SCPI namespaces are migrated from direct L3 calls.
 */
class ICommandDispatcher {
public:
    virtual ~ICommandDispatcher() = default;

    // =================================================================
    // Motion commands (MOT namespace)
    // =================================================================

    virtual DispatchResult motionRun(uint32_t stepsPerSec, bool forward) = 0;
    virtual DispatchResult motionMove(int32_t steps, uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionGoTo(int32_t position, uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionStop(bool hard) = 0;
    virtual DispatchResult motionEnable() = 0;
    virtual DispatchResult motionDisable() = 0;
    virtual DispatchResult motionHome(uint32_t maxSpeedSps) = 0;
    virtual DispatchResult motionZero() = 0;

    // =================================================================
    // Config commands (MOT:CFG namespace + legacy ACCEL/DECEL/MAXSPD)
    // =================================================================

    virtual DispatchResult configSetAccelRaw(uint16_t val) = 0;
    virtual DispatchResult configSetDecelRaw(uint16_t val) = 0;
    virtual DispatchResult configSetMaxSpeedRaw(uint16_t val) = 0;
    virtual DispatchResult configSetAccelPhysical(uint32_t stepsPerS2) = 0;
    virtual DispatchResult configSetDecelPhysical(uint32_t stepsPerS2) = 0;
    virtual DispatchResult configSetMaxSpeedPhysical(uint32_t stepsPerS) = 0;

    // =================================================================
    // Motor config queries (needed by REV/RPM unit conversion)
    // =================================================================

    virtual uint32_t getMicrostepsPerRev() = 0;
    virtual uint16_t getFullStepsPerRev() = 0;

    // =================================================================
    // Safety commands (SYST namespace)
    // =================================================================

    virtual void safetyEstop() = 0;
    virtual DispatchResult safetyClearFault(char* activeFaults, uint32_t bufSize) = 0;
    virtual DispatchResult safetyForceClearFault() = 0;
    virtual uint32_t safetySetHeartbeatTimeout(uint32_t ms) = 0;
    virtual void safetyHeartbeatReceived(uint32_t seq) = 0;
    virtual void safetyGetHeartbeatStatus(bool& enabled, uint32_t& timeoutMs,
                                           uint32_t& lastSeq, uint32_t& remainingMs,
                                           bool& timedOut) = 0;

    // =================================================================
    // Sync / Queue commands (SYNC namespace)
    // =================================================================

    /** @brief Queue a motor command by type */
    virtual DispatchResult queueCommand(uint8_t cmdType, int32_t p1, int32_t p2) = 0;
    virtual DispatchResult queueMove(int32_t signedSteps) = 0;
    virtual DispatchResult queueGoTo(int32_t position) = 0;
    virtual DispatchResult queueRun(uint32_t speedRaw, int32_t direction) = 0;
    virtual DispatchResult queueStop(bool hard) = 0;
    virtual DispatchResult queueHome() = 0;
    virtual DispatchResult queueZero() = 0;
    virtual DispatchResult queueArm() = 0;
    virtual DispatchResult queueStart() = 0;
    virtual DispatchResult queueStartAt(uint32_t targetTick) = 0;
    virtual DispatchResult queueClear() = 0;

    /** @brief Get controller state (returns ControllerState as uint8_t) */
    virtual uint8_t getControllerState() = 0;
    virtual uint32_t getQueueDepth() = 0;
    virtual const char* controllerStateString() = 0;

    // =================================================================
    // Control mode / status queries (CTRL namespace, GET_STATUS)
    // =================================================================

    virtual uint8_t getControlMode() = 0;
    virtual const char* controlModeString() = 0;
    virtual uint8_t getEncoderStatus() = 0;
    virtual const char* encoderStatusString() = 0;

    /** @brief Get fault enable mask for GET_STATUS fault filtering */
    virtual void getFaultEnable(bool& ocd, bool& thermalSD, bool& thermalWarn,
                                bool& uvlo, bool& stallA, bool& stallB,
                                bool& cmdErr) = 0;

    // =================================================================
    // Device config (DEV namespace)
    // =================================================================

    virtual void getDeviceInfo(uint16_t& deviceId, const char*& roleStr) = 0;
    virtual bool setDeviceId(uint16_t id) = 0;
    /** @brief Parse role name and set; returns role string in message on success */
    virtual DispatchResult setRole(const char* roleName) = 0;

    // =================================================================
    // Control mode set (CTRL namespace)
    // =================================================================

    /** @brief Parse mode name and set; message = mode name on success, error on failure */
    virtual DispatchResult setControlMode(const char* modeName) = 0;

    // =================================================================
    // Encoder data queries (CTRL:ENC namespace)
    // =================================================================

    virtual bool isEncoderAvailable() = 0;
    virtual void getEncoderState(int32_t& count, int32_t& velocity, bool& indexSeen) = 0;

    // =================================================================
    // Motor config — flash operations (DRV namespace)
    // =================================================================

    virtual bool configSaveToFlash() = 0;
    virtual bool configLoadFromFlash() = 0;
    virtual bool configFactoryReset() = 0;
    virtual bool configIsValid() = 0;

    // =================================================================
    // Motor config — DRV setters
    // =================================================================

    virtual void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) = 0;
    virtual void configSetOcdThreshold(uint8_t thresh) = 0;
    virtual void configSetStallThreshold(uint8_t thresh) = 0;
    virtual void configSetFaultAction(uint8_t action) = 0;
    virtual void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                            bool uvlo, bool stallA, bool stallB, bool cmdErr) = 0;
    virtual void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) = 0;
    virtual void configSetStepMode(uint8_t mode) = 0;
    virtual void configSetFullStepsPerRev(uint16_t steps) = 0;
    virtual void configSetEncoderPPR(uint16_t ppr) = 0;
    virtual uint16_t configGetEncoderPPR() = 0;

    /** @brief Format full motor config into buffer for display (DRV:CFG?) */
    virtual void formatMotorConfig(char* buf, uint32_t bufSize) = 0;

    // =================================================================
    // Motor config — encoder filter (UI:ENC namespace)
    // =================================================================

    virtual void configSetEncFilter(uint8_t type, uint8_t alpha) = 0;
    virtual void configSetEncSmaWindow(uint8_t window) = 0;
    virtual void configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                         uint8_t smaWindow, uint8_t measWindowMs,
                                         uint8_t rateDiv) = 0;

    // =================================================================
    // Following supervisor (CTRL:FOLLOW namespace)
    // =================================================================

    virtual void getFollowThresholds(int16_t& moveErr, uint16_t& moveTime,
                                      int16_t& holdErr, uint16_t& holdTime,
                                      int16_t& hardLimit, uint8_t& maxRetries) = 0;
    virtual void setFollowThresholds(int16_t moveErr, uint16_t moveTime,
                                      int16_t holdErr, uint16_t holdTime,
                                      int16_t hardLimit, uint8_t maxRetries) = 0;
    virtual void applySupervisorConfig() = 0;
    virtual bool supervisorClearFault() = 0;

    /** @brief Get supervisor telemetry for CTRL:FOLLOW? display */
    virtual void getSupervisorTelemetry(uint8_t& state, uint8_t& tier,
                                         int32_t& posError, int32_t& velError,
                                         int32_t& setpoint, int16_t& pidOutput,
                                         uint8_t& retryCount, uint32_t& errorDurationMs) = 0;

    // =================================================================
    // Speed trim (CTRL:TRIM namespace)
    // =================================================================

    virtual void getTrimGains(int16_t& kp100, int16_t& ki100, int16_t& kd100,
                               uint16_t& outLim, uint16_t& iLim) = 0;
    virtual void setTrimGains(int16_t kp100, int16_t ki100) = 0;
    virtual void setTrimLimits(uint16_t outLim, uint16_t iLim) = 0;
    virtual void setTrimMaxPct(int16_t val) = 0;
    virtual void resetTrim() = 0;
    virtual void applyTrimConfig() = 0;

    // =================================================================
    // System ID (CTRL:SYSID namespace)
    // =================================================================

    virtual bool sysIdStart(uint8_t type, uint16_t targetSpeed, uint16_t startSpeed,
                             bool forward, uint16_t durationMs, uint16_t settleMs,
                             uint16_t freqMHz) = 0;
    virtual void sysIdAbort() = 0;
    virtual uint8_t sysIdGetPhase() = 0;
    virtual uint16_t sysIdGetSampleCount() = 0;
    /** @brief Get sysid sample data pointer (null if not DONE); count set to sample count */
    virtual const SysIdSampleData* sysIdGetSamples(uint16_t& count) = 0;

    // =================================================================
    // Events (EVT namespace)
    // =================================================================

    virtual void enableEvents(uint8_t mask, uint16_t currentStatus) = 0;
    virtual void disableEvents() = 0;
    virtual void getEventStats(uint32_t& sent, uint32_t& lostCritical,
                                uint32_t& lostInfo, uint8_t& mask,
                                uint8_t& queueDepth) = 0;

    // =================================================================
    // Timing (SYST:TICK, SYST:DELAY)
    // =================================================================

    virtual uint32_t getTickUs() = 0;
    virtual void setTransportDelay(uint32_t ms) = 0;
    virtual uint32_t getTransportDelay() = 0;

    // =================================================================
    // Encoder (CTRL:ENC, SYST:ZERO)
    // =================================================================

    virtual void encoderResetCount() = 0;

    // =================================================================
    // Trace operations (DBG:TRACE namespace)
    // =================================================================

    struct TraceEntryData {
        uint32_t tick;
        const char* tag;
        uint32_t arg0;
        uint8_t dir;       // 0=ENTRY, 1=EXIT
        const char* method;
    };

    virtual uint32_t traceGetCount() = 0;
    virtual bool traceGetEntry(uint32_t index, TraceEntryData& out) = 0;
    virtual void traceReset() = 0;
    virtual void traceRecordEntry(const char* tag, uint32_t arg0 = 0) = 0;
    virtual void traceRecordExit(const char* tag, uint32_t arg0 = 0) = 0;

    // =================================================================
    // Flash image operations (FLASH_* commands)
    // =================================================================

    static constexpr uint32_t FLASH_IMAGE_SIZE  = 153600;  // 240*320*2
    static constexpr uint32_t FLASH_PAGE_SIZE   = 256;

    struct FlashInfo {
        uint8_t  manufacturer;
        uint8_t  memoryType;
        uint8_t  capacityCode;
        uint32_t capacityBytes;
        uint32_t maxSlots;
    };

    virtual bool flashIsAvailable() = 0;
    virtual FlashInfo flashGetInfo() = 0;
    virtual uint32_t flashMaxSlots() = 0;
    virtual bool flashEraseSlot(uint32_t slot) = 0;
    virtual bool flashWriteSlotData(uint32_t slot, uint32_t offset,
                                    const uint8_t* data, size_t len) = 0;
    virtual bool flashReadSlotChunk(uint32_t slot, uint32_t offset,
                                    uint8_t* buf, size_t len) = 0;
    virtual bool flashReadSlotChunkStart(uint32_t slot, uint32_t offset,
                                         uint8_t* buf, size_t len) = 0;
    virtual void flashReadSlotChunkFinish() = 0;
    virtual bool flashEraseAll() = 0;
    virtual uint32_t flashSlotAddress(uint32_t slot) = 0;

};

} // namespace Comms
