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
constexpr uint16_t MOTOR_CONFIG_VERSION = 1;

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
    uint8_t reserved2[2];

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
    uint8_t reserved4[2];

    // Padding to 64 bytes
    uint8_t reserved5[28];

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

    // Setters (modify RAM, call saveToFlash() to persist)
    void setKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec);
    void setOcdThreshold(uint8_t threshold);
    void setStallThreshold(uint8_t threshold);
    void setMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd);
    void setFaultEnable(FaultEnableFlags flags);
    void setFaultAction(uint8_t action);

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
