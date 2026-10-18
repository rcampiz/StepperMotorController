/**
 * @file motor_config.hpp
 * @brief Motor safety configuration stored in internal flash
 *
 * Stores powerSTEP01 configuration parameters that persist across reboots:
 *   - KVAL values (PWM duty cycle for voltage mode)
 *   - Overcurrent detection threshold
 *   - Stall detection threshold
 *   - Motion parameters (acceleration, deceleration, max speed)
 *   - Fault enable flags
 *
 * Storage: Last page of STM32F401RE internal flash (Sector 7)
 * Format: 64-byte structure with magic number and CRC32 validation
 *
 * Safety: When faults are enabled, FLAG interrupt triggers immediate motor stop.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "harness/defs/harness_l3_l4.hpp"

namespace Services {

// Flash storage constants - use last 4KB of Sector 7 (128KB sector at 0x08060000)
// We'll use address 0x0807F000 (last 4KB before end of 512KB flash)
constexpr uint32_t MOTOR_CONFIG_MAGIC = 0x4D4F5452;  // "MOTR"
constexpr uint16_t MOTOR_CONFIG_VERSION = 4;   // V4: 128-byte blob, current-mode regulation

/**
 * @brief Fault enable flags
 */
struct FaultEnableFlags {
    uint8_t ocd : 1;        // Overcurrent detection
    uint8_t thermalSD : 1;  // Thermal shutdown
    uint8_t thermalWarn : 1;// Thermal warning
    uint8_t uvlo : 1;       // Under-voltage lockout
    uint8_t stallA : 1;     // Stall detection bridge A
    uint8_t stallB : 1;     // Stall detection bridge B
    uint8_t cmdErr : 1;     // Command error
    uint8_t reserved : 1;
};

/**
 * @brief Motor configuration structure
 *
 * Stored in internal flash. Total size: 64 bytes
 */
struct MotorConfig {
    uint32_t magic;         // 0x4D4F5452 - validity marker
    uint16_t version;       // Config format version
    uint8_t  driveMode;     // V3: 0 = voltage mode (KVAL), 1 = current mode (TVAL, the default); STEP_MODE bit CM_VM
    uint8_t  tvalHold;      // V3: TVAL_HOLD (0-127, 7.8 mV/LSB on the sense: 78 mA/LSB with 0.1 ohm), 0 = default

    // KVAL values (0-255, PWM duty cycle percentage * 2.56)
    uint8_t kvalHold;       // Holding current
    uint8_t kvalRun;        // Running current
    uint8_t kvalAcc;        // Acceleration current
    uint8_t kvalDec;        // Deceleration current

    // Protection thresholds
    uint8_t ocdThreshold;   // Overcurrent threshold (0-31, ~375mA per step)
    uint8_t stallThreshold; // Stall threshold (0-127)
    uint8_t encMeasWindowMs;    // Encoder measurement window in ms (1-255, 0=default 10)
    uint8_t encSampleRateDiv100;// DMA sample rate / 100 (1-100, 0=default 10 → 1kHz)

    // Motion parameters
    uint16_t acceleration;  // ACC register value (14-bit, steps/tick^2)
    uint16_t deceleration;  // DEC register value
    uint16_t maxSpeed;      // MAX_SPEED register value (10-bit)
    uint16_t minSpeed;      // MIN_SPEED register value (for stall detection)

    // Full-step speed threshold (for stall detection cutoff)
    uint16_t fsSpeed;       // FS_SPD register (0x3FF = disabled)
    uint8_t  tvalRun;       // V3: TVAL_RUN, 0 = default
    uint8_t  tvalAcc;       // V3: TVAL_ACC, 0 = default

    // Fault enables
    FaultEnableFlags faultEnable;
    uint8_t faultAction;    // 0=HardStop, 1=HardHiZ, 2=SoftStop
    uint8_t stepMode;       // STEP_MODE[2:0]: 0=full, 1=half, ... 7=1/128
    uint8_t encSmaWindow;   // SMA filter window (2-32, 0=adaptive)

    // Encoder velocity filter (persisted across reboots)
    // V2: encFilterType is a bitfield (bit0=EMA, bit1=SMA), encFilterParam = EMA alpha
    uint8_t encFilterType;    // Bitfield: bit0=EMA enabled, bit1=SMA enabled
    uint8_t encFilterParam;   // EMA alpha (0-255, higher=smoother)

    // Motor/encoder physical parameters (0 = use defaults: 200/4000)
    uint16_t fullStepsPerRev; // Mechanical full steps per revolution (typically 200)
    uint16_t encoderPPR;      // Encoder pulses per revolution (e.g., 4000 for 1000-line × 4)

    // Speed-trim PI gains (reinterpreted from PID fields, backward compatible)
    // Stored as int16_t × 100 for 0.01 resolution (range ±327.67)
    // All zeros = trim disabled (backward compatible with old configs)
    int16_t pidKp100;         // Trim Kp × 100
    int16_t pidKi100;         // Trim Ki × 100
    int16_t pidKd100;         // Trim max percent (1-50), 0 = default 8%
    uint16_t pidOutputLimit;  // Max trim output (ticks/sec), 0 = default 500
    uint16_t pidIntegralLimit;// Anti-windup clamp (ticks/sec), 0 = use outputLimit

    // Following Error Supervisor thresholds (closed-loop modes)
    // All zeros = use compiled defaults (backward compatible with old flash images)
    int16_t  followMoveError;   // E_MOVE: position error threshold during motion (ticks), 0=50
    uint16_t followMoveTimeMs;  // T_MOVE: debounce before Tier 2 escalation (ms), 0=500
    int16_t  followHoldError;   // E_HOLD: position error threshold during hold (ticks), 0=20
    uint16_t followHoldTimeMs;  // T_HOLD: debounce for hold error (ms), 0=200
    int16_t  followHardLimit;   // E_HARD: immediate fault threshold (ticks), 0=500
    uint8_t  followMaxRetries;  // N_RETRY: max Tier 2 recovery attempts, 0=3
    uint8_t  tvalDec;           // V3: TVAL_DEC, 0 = default

    // V4 (2026-10-18): the blob grew from 64 to 128 bytes. powerSTEP01 current-mode
    // regulation (0 = chip default everywhere) and room for later fields.
    uint8_t  cmPredEn;        // CONFIG bit 15 PRED_EN (predictive current control): 0 = default (on), 1 off, 2 on
    uint8_t  cmTswPlus1;      // CONFIG[14:10] target switching period + 1 ((TSW+1) x 4 us); 0 = default (48 us)
    uint8_t  cmTFast;         // T_FAST: [7:4] FAST_STEP, [3:0] TOFF_FAST, (v+1) x 2 us each; 0 = default 0x19
    uint8_t  cmTonMin;        // TON_MIN (7 bits, (v+1) x 0.5 us); 0 = default 0x29 (21 us)
    uint8_t  cmToffMin;       // TOFF_MIN (7 bits, (v+1) x 0.5 us); 0 = default 0x29 (21 us)
    uint16_t idleReleaseMs;   // V4: coils to Hi-Z after this long stopped and energised; 0 = never
    uint8_t  reserved4[56];   // zero (the uint16 above is padded to an even offset)

    uint32_t crc32;         // Data integrity check
};

static_assert(sizeof(MotorConfig) == 128, "MotorConfig must be 128 bytes (one journal slot)");

/// The V1-V3 64-byte layout: the prefix of MotorConfig up to tvalDec, then the CRC. Read
/// only, to migrate a blob written before V4.
struct MotorConfigV3 {
    uint32_t magic;         // 0x4D4F5452 - validity marker
    uint16_t version;       // Config format version
    uint8_t  driveMode;     // V3: 0 = voltage mode (KVAL), 1 = current mode (TVAL, the default); STEP_MODE bit CM_VM
    uint8_t  tvalHold;      // V3: TVAL_HOLD (0-127, 7.8 mV/LSB on the sense: 78 mA/LSB with 0.1 ohm), 0 = default

    // KVAL values (0-255, PWM duty cycle percentage * 2.56)
    uint8_t kvalHold;       // Holding current
    uint8_t kvalRun;        // Running current
    uint8_t kvalAcc;        // Acceleration current
    uint8_t kvalDec;        // Deceleration current

    // Protection thresholds
    uint8_t ocdThreshold;   // Overcurrent threshold (0-31, ~375mA per step)
    uint8_t stallThreshold; // Stall threshold (0-127)
    uint8_t encMeasWindowMs;    // Encoder measurement window in ms (1-255, 0=default 10)
    uint8_t encSampleRateDiv100;// DMA sample rate / 100 (1-100, 0=default 10 → 1kHz)

    // Motion parameters
    uint16_t acceleration;  // ACC register value (14-bit, steps/tick^2)
    uint16_t deceleration;  // DEC register value
    uint16_t maxSpeed;      // MAX_SPEED register value (10-bit)
    uint16_t minSpeed;      // MIN_SPEED register value (for stall detection)

    // Full-step speed threshold (for stall detection cutoff)
    uint16_t fsSpeed;       // FS_SPD register (0x3FF = disabled)
    uint8_t  tvalRun;       // V3: TVAL_RUN, 0 = default
    uint8_t  tvalAcc;       // V3: TVAL_ACC, 0 = default

    // Fault enables
    FaultEnableFlags faultEnable;
    uint8_t faultAction;    // 0=HardStop, 1=HardHiZ, 2=SoftStop
    uint8_t stepMode;       // STEP_MODE[2:0]: 0=full, 1=half, ... 7=1/128
    uint8_t encSmaWindow;   // SMA filter window (2-32, 0=adaptive)

    // Encoder velocity filter (persisted across reboots)
    // V2: encFilterType is a bitfield (bit0=EMA, bit1=SMA), encFilterParam = EMA alpha
    uint8_t encFilterType;    // Bitfield: bit0=EMA enabled, bit1=SMA enabled
    uint8_t encFilterParam;   // EMA alpha (0-255, higher=smoother)

    // Motor/encoder physical parameters (0 = use defaults: 200/4000)
    uint16_t fullStepsPerRev; // Mechanical full steps per revolution (typically 200)
    uint16_t encoderPPR;      // Encoder pulses per revolution (e.g., 4000 for 1000-line × 4)

    // Speed-trim PI gains (reinterpreted from PID fields, backward compatible)
    // Stored as int16_t × 100 for 0.01 resolution (range ±327.67)
    // All zeros = trim disabled (backward compatible with old configs)
    int16_t pidKp100;         // Trim Kp × 100
    int16_t pidKi100;         // Trim Ki × 100
    int16_t pidKd100;         // Trim max percent (1-50), 0 = default 8%
    uint16_t pidOutputLimit;  // Max trim output (ticks/sec), 0 = default 500
    uint16_t pidIntegralLimit;// Anti-windup clamp (ticks/sec), 0 = use outputLimit

    // Following Error Supervisor thresholds (closed-loop modes)
    // All zeros = use compiled defaults (backward compatible with old flash images)
    int16_t  followMoveError;   // E_MOVE: position error threshold during motion (ticks), 0=50
    uint16_t followMoveTimeMs;  // T_MOVE: debounce before Tier 2 escalation (ms), 0=500
    int16_t  followHoldError;   // E_HOLD: position error threshold during hold (ticks), 0=20
    uint16_t followHoldTimeMs;  // T_HOLD: debounce for hold error (ms), 0=200
    int16_t  followHardLimit;   // E_HARD: immediate fault threshold (ticks), 0=500
    uint8_t  followMaxRetries;  // N_RETRY: max Tier 2 recovery attempts, 0=3
    uint8_t  tvalDec;           // V3: TVAL_DEC, 0 = default

    uint32_t crc32;
};

static_assert(sizeof(MotorConfigV3) == 64, "MotorConfigV3 is the pre-V4 64-byte blob");

/**
 * @brief Motor configuration manager
 *
 * Thread-safe singleton managing persistent motor configuration.
 * Reads from flash at init, writes on explicit save operations.
 */
class MotorConfigManager {
public:
    /**
     * @brief Bind the persistent store (H_L3_L4) and load the configuration
     * @return true if a valid config was loaded, false if defaults were applied
     */
    bool init(Drivers::IConfigStore& store);

    /**
     * @brief Check if configuration is valid (loaded from flash)
     */
    bool isValid() const { return m_valid; }

    /// Why the last saveToFlash() returned false.
    enum class SaveStatus : uint8_t {
        Ok,
        StoreFull,      ///< journal full and the erase gate refused (motor moving): save again stopped
        WriteFailed     ///< the store rejected the write or the readback differed
    };
    SaveStatus lastSaveStatus() const { return m_lastSave; }

    /// Gate for the run-time sector erase (null = always allowed, as at boot). The erase
    /// freezes the MCU for 1-2 s, so the composition root installs "the motor is stopped".
    using EraseGate = bool (*)();
    void setEraseGate(EraseGate gate) { m_eraseGate = gate; }

    /// Saves possible before the journal must be erased (store-dependent).
    size_t savesRemaining() const;

    /**
     * @brief Get current configuration (read-only reference)
     */
    const MotorConfig& getConfig() const { return m_config; }

    /**
     * @brief Get mutable configuration for modification
     * Call saveToFlash() after making changes.
     */
    MotorConfig& getConfigMutable() { return m_config; }

    // Convenience getters
    uint8_t getKvalHold() const { return m_config.kvalHold; }
    uint8_t getKvalRun() const { return m_config.kvalRun; }
    uint8_t getKvalAcc() const { return m_config.kvalAcc; }
    uint8_t getKvalDec() const { return m_config.kvalDec; }

    // Current mode (powerSTEP01 CM_VM): the KVAL registers become TVAL current references.
    // Defaults for the project's motor, StepperOnline 23HS22-2804-ME1K (NEMA 23, 2.8 A/phase,
    // 0.9 ohm, 2.5 mH, 1000-line encoder): hold 0.39 A, run 1.72 A, acc/dec 2.03 A at 78 mA/LSB.
    // Chosen for a battery robot (bench 2026-10-18, 12.5 V, gate drive fixed): hold 64 mA from
    // the supply, run about 25 % below the 2.0 A setting's draw; well inside the 2.8 A rating.
    static constexpr uint8_t TVAL_DEFAULT_HOLD = 0x05;
    static constexpr uint8_t TVAL_DEFAULT_RUN  = 0x16;
    static constexpr uint8_t TVAL_DEFAULT_ACC  = 0x1A;
    bool    isCurrentMode() const { return m_config.driveMode != 0; }
    uint8_t getDriveMode() const { return m_config.driveMode ? 1 : 0; }
    uint8_t getTvalHold() const { return m_config.tvalHold ? m_config.tvalHold : TVAL_DEFAULT_HOLD; }
    uint8_t getTvalRun()  const { return m_config.tvalRun  ? m_config.tvalRun  : TVAL_DEFAULT_RUN; }
    uint8_t getTvalAcc()  const { return m_config.tvalAcc  ? m_config.tvalAcc  : TVAL_DEFAULT_ACC; }
    uint8_t getTvalDec()  const { return m_config.tvalDec  ? m_config.tvalDec  : TVAL_DEFAULT_ACC; }

    // Current-mode regulation (V4). Chip defaults: PRED_EN off, TSW 11 (48 us), T_FAST 0x19
    // (fast step 4 us, fast decay 20 us), TON_MIN/TOFF_MIN 0x29 (21 us).
    static constexpr uint8_t CM_DEFAULT_TSW   = 11;
    static constexpr uint8_t CM_DEFAULT_TFAST = 0x19;
    static constexpr uint8_t CM_DEFAULT_TMIN  = 0x29;
    bool    getCmPredEn()  const { return m_config.cmPredEn != 1; }   // on by default (bench 2026-10-18: less ripple at every speed)
    uint8_t getCmTsw()     const { return m_config.cmTswPlus1 ? static_cast<uint8_t>((m_config.cmTswPlus1 - 1) & 0x1F) : CM_DEFAULT_TSW; }
    uint8_t getCmTFast()   const { return m_config.cmTFast   ? m_config.cmTFast   : CM_DEFAULT_TFAST; }
    uint8_t getCmTonMin()  const { return m_config.cmTonMin  ? m_config.cmTonMin  : CM_DEFAULT_TMIN; }
    uint8_t getCmToffMin() const { return m_config.cmToffMin ? m_config.cmToffMin : CM_DEFAULT_TMIN; }

    /// Idle release: 0 = hold for ever (the chip's own behaviour), else release the coils
    /// (SoftHiZ) after this many ms stopped; the next motion command re-energises them.
    uint16_t getIdleReleaseMs() const { return m_config.idleReleaseMs; }
    void     setIdleReleaseMs(uint16_t ms) { m_config.idleReleaseMs = ms; }
    uint8_t getOcdThreshold() const { return m_config.ocdThreshold; }
    uint8_t getStallThreshold() const { return m_config.stallThreshold; }
    uint16_t getAcceleration() const { return m_config.acceleration; }
    uint16_t getDeceleration() const { return m_config.deceleration; }
    uint16_t getMaxSpeed() const { return m_config.maxSpeed; }
    FaultEnableFlags getFaultEnable() const { return m_config.faultEnable; }
    /// powerSTEP01 in current mode microsteps at 1/16 at most: STEP_SEL 1/32..1/128 run as
    /// 1/16 while the register still reads back what was written. So a configured 1/128 moved
    /// the motor 8x too far (bench 2026-10-18: a 1-rev jog turned 8 revs). The step mode in
    /// force, and every conversion from it, is the effective one.
    static constexpr uint8_t MAX_CURRENT_MODE_STEP_MODE = 4;    // 1/16
    static uint8_t effectiveStepMode(uint8_t mode, bool currentMode) {
        const uint8_t m = mode > 7 ? 7 : mode;
        return (currentMode && m > MAX_CURRENT_MODE_STEP_MODE) ? MAX_CURRENT_MODE_STEP_MODE : m;
    }
    uint8_t getStepMode() const { return effectiveStepMode(m_config.stepMode, isCurrentMode()); }
    /// What was asked for (kept, so going back to voltage mode restores it)
    uint8_t getConfiguredStepMode() const { return m_config.stepMode > 7 ? 7 : m_config.stepMode; }
    uint8_t getEncFilterType() const { return m_config.encFilterType; }
    uint8_t getEncFilterParam() const { return m_config.encFilterParam; }
    uint8_t getEncSmaWindow() const { return m_config.encSmaWindow; }
    uint8_t getEncMeasWindowMs() const { return (m_config.encMeasWindowMs != 0) ? m_config.encMeasWindowMs : 40; }
    uint16_t getEncSampleRateHz() const {
        uint8_t d = m_config.encSampleRateDiv100;
        return (d != 0) ? static_cast<uint16_t>(d) * 100 : 1000;
    }
    uint16_t getFullStepsPerRev() const { return (m_config.fullStepsPerRev != 0) ? m_config.fullStepsPerRev : 200; }
    uint16_t getEncoderPPR() const { return (m_config.encoderPPR != 0) ? m_config.encoderPPR : 4000; }
    uint32_t getMicrostepsPerRev() const { return static_cast<uint32_t>(getFullStepsPerRev()) * (1U << getStepMode()); }
    int16_t getPidKp100() const { return m_config.pidKp100; }
    int16_t getPidKi100() const { return m_config.pidKi100; }
    int16_t getPidKd100() const { return m_config.pidKd100; }
    float getPidKp() const { return static_cast<float>(m_config.pidKp100) / 100.0F; }
    float getPidKi() const { return static_cast<float>(m_config.pidKi100) / 100.0F; }
    float getPidKd() const { return static_cast<float>(m_config.pidKd100) / 100.0F; }
    uint16_t getPidOutputLimit() const { return (m_config.pidOutputLimit != 0) ? m_config.pidOutputLimit : 500; }
    uint16_t getPidIntegralLimit() const { return (m_config.pidIntegralLimit != 0) ? m_config.pidIntegralLimit : getPidOutputLimit(); }

    // Speed-trim convenience getters (reinterpret PID flash fields)
    float getTrimKp() const { return getPidKp(); }
    float getTrimKi() const { return getPidKi(); }
    uint8_t getTrimMaxPercent() const {
        int16_t v = m_config.pidKd100;
        return (v > 0 && v <= 50) ? static_cast<uint8_t>(v) : 8;
    }
    uint16_t getTrimOutputLimit() const { return getPidOutputLimit(); }
    uint16_t getTrimIntegralLimit() const { return getPidIntegralLimit(); }

    // Following Error Supervisor getters (0 = use compiled default)
    int16_t  getFollowMoveError() const { return (m_config.followMoveError != 0) ? m_config.followMoveError : 50; }
    uint16_t getFollowMoveTimeMs() const { return (m_config.followMoveTimeMs != 0) ? m_config.followMoveTimeMs : 500; }
    int16_t  getFollowHoldError() const { return (m_config.followHoldError != 0) ? m_config.followHoldError : 20; }
    uint16_t getFollowHoldTimeMs() const { return (m_config.followHoldTimeMs != 0) ? m_config.followHoldTimeMs : 200; }
    int16_t  getFollowHardLimit() const { return (m_config.followHardLimit != 0) ? m_config.followHardLimit : 500; }
    uint8_t  getFollowMaxRetries() const { return (m_config.followMaxRetries != 0) ? m_config.followMaxRetries : 3; }

    // Setters (modify RAM, call saveToFlash() to persist)
    void setKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec);
    void setDriveMode(uint8_t mode);                                   ///< 0 voltage, 1 current
    void setTval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec); ///< each clamped to 0x7F
    /// tsw 0..31 (0xFF = chip default); tFast/tonMin/toffMin 0 = chip default
    void setCmRegulation(bool predEn, uint8_t tsw, uint8_t tFast, uint8_t tonMin, uint8_t toffMin);
    void setOcdThreshold(uint8_t threshold);
    void setStallThreshold(uint8_t threshold);
    void setMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd);
    void setFaultEnable(FaultEnableFlags flags);
    void setFaultAction(uint8_t action);
    void setStepMode(uint8_t mode);
    void setEncFilter(uint8_t type, uint8_t param);
    void setEncFilterFull(uint8_t flags, uint8_t emaAlpha, uint8_t smaWindow,
                          uint8_t measWindowMs, uint8_t sampleRateDiv100);
    void setFullStepsPerRev(uint16_t steps);
    void setEncoderPPR(uint16_t ppr);
    void setPidGains(int16_t kp100, int16_t ki100, int16_t kd100);
    void setPidLimits(uint16_t outputLimit, uint16_t integralLimit);
    void setFollowThresholds(int16_t moveErr, uint16_t moveTimeMs,
                             int16_t holdErr, uint16_t holdTimeMs,
                             int16_t hardLimit, uint8_t maxRetries);

    /**
     * @brief Save current configuration to flash
     * @return true if write succeeded
     */
    bool saveToFlash();

    /**
     * @brief Reload configuration from flash
     * @return true if valid config loaded
     */
    bool loadFromFlash();

    /**
     * @brief Reset to safe defaults and save
     * @return true if write succeeded
     */
    bool factoryReset();

    /**
     * @brief Apply defaults without saving (for first boot)
     */
    void applyDefaults();
    /// The blob's CRC (exposed so a host test can forge a pre-V4 blob)
    uint32_t blobCrc(const uint8_t* data, size_t len) { return calculateCRC(data, len); }

private:
    MotorConfig m_config;
    bool m_valid;

    // CRC32 calculation
    uint32_t calculateCRC(const uint8_t* data, size_t len);
    bool validateLegacyV3(const MotorConfigV3& cfg);

    // Validate magic and CRC
    bool validateConfig(const MotorConfig& cfg);

    // Persistent blob store (L4, via H_L3_L4)
    Drivers::IConfigStore* m_store = nullptr;
    EraseGate  m_eraseGate = nullptr;
    SaveStatus m_lastSave = SaveStatus::Ok;

    /// Compact at boot when fewer than this many saves are left, so a run-time erase is rare.
    static constexpr size_t COMPACT_MARGIN_SAVES = 16;

    bool writeRecord();   // one journal append (or overwrite) + readback verify
};

// Global instance
extern MotorConfigManager g_motorConfig;

} // namespace Services
