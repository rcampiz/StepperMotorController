/**
 * @file device_config.hpp
 * @brief Persistent device configuration stored in SPI NOR flash
 *
 * Provides unique device identification for multi-controller systems:
 *   - Device ID for host identification
 *   - Wheel role assignment (FL/FR/RL/RR for mecanum robot)
 *   - Default control mode (OPEN_LOOP/CLOSED_LOOP)
 *
 * Storage: First 4KB sector of SPI NOR flash on X-NUCLEO-GFX01M2
 * Format: 36-byte structure with magic number and CRC32 validation
 */

#pragma once

#include "drivers/flash_nor.hpp"
#include "FreeRTOS.h"
#include "semphr.h"
#include <cstdint>

namespace Services {

// Forward declaration
enum class ControlMode : uint8_t;

/**
 * @brief Wheel position on mecanum robot chassis
 */
enum class WheelRole : uint8_t {
    UNASSIGNED  = 0,
    FRONT_LEFT  = 1,
    FRONT_RIGHT = 2,
    REAR_LEFT   = 3,
    REAR_RIGHT  = 4
};

/**
 * @brief Convert WheelRole to string
 */
const char* roleToString(WheelRole role);

/**
 * @brief Parse WheelRole from string (FL, FR, RL, RR, NONE)
 * @return UNASSIGNED if invalid
 */
WheelRole parseRole(const char* str);

// Flash storage constants
constexpr uint32_t CONFIG_MAGIC = 0xDEADBEEF;
constexpr uint16_t CONFIG_VERSION = 1;
constexpr uint32_t CONFIG_FLASH_ADDR = 0x000000;  // First sector

/**
 * @brief Persistent device configuration structure
 *
 * Stored in SPI NOR flash. Total size: 36 bytes
 * Layout chosen for alignment and future expansion.
 */
struct DeviceConfig {
    uint32_t magic;           // 0xDEADBEEF - validity marker
    uint16_t version;         // Config format version
    uint16_t deviceId;        // Unique device ID (0-65535)
    uint8_t role;             // WheelRole enum value
    uint8_t defaultMode;      // ControlMode enum value
    uint8_t reserved[22];     // Future expansion (zero-filled)
    uint32_t crc32;           // Data integrity check
};

static_assert(sizeof(DeviceConfig) == 36, "DeviceConfig must be 36 bytes");

/**
 * @brief Device configuration manager
 *
 * Thread-safe singleton managing persistent device configuration.
 * Reads from flash at init, writes on explicit save operations.
 */
class DeviceConfigManager {
public:
    /**
     * @brief Initialize with flash driver
     *
     * Loads configuration from flash. If flash is empty or corrupted,
     * applies default values (deviceId=0, UNASSIGNED, OPEN_LOOP).
     *
     * @param flash Reference to initialized SPIFlash driver
     * @return true if valid config loaded, false if defaults applied
     */
    bool init(SPIFlash& flash);

    /**
     * @brief Check if configuration is valid
     */
    bool isValid() const { return m_valid; }

    // Getters (thread-safe, read from RAM)
    uint16_t getDeviceId() const;
    WheelRole getRole() const;
    uint8_t getDefaultMode() const;

    /**
     * @brief Set device ID and save to flash
     * @return true if flash write succeeded
     */
    bool setDeviceId(uint16_t id);

    /**
     * @brief Set wheel role and save to flash
     * @return true if flash write succeeded
     */
    bool setRole(WheelRole role);

    /**
     * @brief Set default control mode and save to flash
     * @return true if flash write succeeded
     */
    bool setDefaultMode(uint8_t mode);

    /**
     * @brief Reload configuration from flash
     * @return true if valid config loaded
     */
    bool loadFromFlash();

    /**
     * @brief Save current configuration to flash
     * @return true if write succeeded
     */
    bool saveToFlash();

    /**
     * @brief Reset to factory defaults and save
     * @return true if write succeeded
     */
    bool factoryReset();

private:
    DeviceConfig m_config;
    SPIFlash* m_flash;
    SemaphoreHandle_t m_mutex;
    bool m_valid;

    // CRC32 calculation (polynomial 0xEDB88320)
    uint32_t calculateCRC(const uint8_t* data, size_t len);

    // Apply default configuration
    void applyDefaults();

    // Validate magic and CRC
    bool validateConfig(const DeviceConfig& cfg);

    // Lock/unlock helpers
    bool lock(TickType_t timeout = portMAX_DELAY);
    void unlock();
};

// Global instance
extern DeviceConfigManager g_deviceConfig;

} // namespace Services
