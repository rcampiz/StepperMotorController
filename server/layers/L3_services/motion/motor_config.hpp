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

namespace Services {

// Flash storage constants - use last 4KB of Sector 7 (128KB sector at 0x08060000)
// We'll use address 0x0807F000 (last 4KB before end of 512KB flash)
constexpr uint32_t MOTOR_CONFIG_FLASH_ADDR = 0x0807F000;
constexpr uint32_t MOTOR_CONFIG_MAGIC = 0x4D4F5452;  // "MOTR"
constexpr uint16_t MOTOR_CONFIG_VERSION = 2;

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
    uint16_t reserved1;

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
    uint16_t reserved3;

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
    uint8_t  followReserved;    // padding

    uint32_t crc32;         // Data integrity check
};

static_assert(sizeof(MotorConfig) == 64, "MotorConfig must be 64 bytes");

/**
 * @brief Motor configuration manager
 *
 * Thread-safe singleton managing persistent motor configuration.
 * Reads from flash at init, writes on explicit save operations.
 */
class MotorConfigManager {
public:
    /**
     * @brief Initialize and load configuration from flash
     * @return true if valid config loaded, false if defaults applied
     */
    bool init();

    /**
     * @brief Check if configuration is valid (loaded from flash)
     */
    bool isValid() const { return m_valid; }

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
    uint8_t getOcdThreshold() const { return m_config.ocdThreshold; }
    uint8_t getStallThreshold() const { return m_config.stallThreshold; }
    uint16_t getAcceleration() const { return m_config.acceleration; }
    uint16_t getDeceleration() const { return m_config.deceleration; }
    uint16_t getMaxSpeed() const { return m_config.maxSpeed; }
    FaultEnableFlags getFaultEnable() const { return m_config.faultEnable; }
    uint8_t getStepMode() const { return m_config.stepMode > 7 ? 7 : m_config.stepMode; }
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

private:
    MotorConfig m_config;
    bool m_valid;

    // CRC32 calculation
    uint32_t calculateCRC(const uint8_t* data, size_t len);

    // Validate magic and CRC
    bool validateConfig(const MotorConfig& cfg);

    // Internal flash operations
    bool eraseFlashPage();
    bool writeFlash(const uint8_t* data, size_t len);
};

// Global instance
extern MotorConfigManager g_motorConfig;

} // namespace Services
