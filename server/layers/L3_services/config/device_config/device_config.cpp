/**
 * @file device_config.cpp
 * @brief Device configuration manager implementation
 */

#include "L3_services/config/device_config/device_config.hpp"
#include "harness/trace/interface_trace.hpp"
#include <string.h>

namespace Services {

// Global instance
DeviceConfigManager g_deviceConfig;

const char *roleToString(WheelRole role) {
  switch (role) {
  case WheelRole::FRONT_LEFT:
    return "FL";
  case WheelRole::FRONT_RIGHT:
    return "FR";
  case WheelRole::REAR_LEFT:
    return "RL";
  case WheelRole::REAR_RIGHT:
    return "RR";
  case WheelRole::UNASSIGNED:
  default:
    return "NONE";
  }
}

WheelRole parseRole(const char *str) {
  if (strcmp(str, "FL") == 0 || strcmp(str, "fl") == 0 ||
      strcmp(str, "FRONT_LEFT") == 0) {
    return WheelRole::FRONT_LEFT;
  }
  if (strcmp(str, "FR") == 0 || strcmp(str, "fr") == 0 ||
      strcmp(str, "FRONT_RIGHT") == 0) {
    return WheelRole::FRONT_RIGHT;
  }
  if (strcmp(str, "RL") == 0 || strcmp(str, "rl") == 0 ||
      strcmp(str, "REAR_LEFT") == 0) {
    return WheelRole::REAR_LEFT;
  }
  if (strcmp(str, "RR") == 0 || strcmp(str, "rr") == 0 ||
      strcmp(str, "REAR_RIGHT") == 0) {
    return WheelRole::REAR_RIGHT;
  }
  return WheelRole::UNASSIGNED;
}

bool DeviceConfigManager::init(Harness::IFlash &flash, Harness::ILock& lock) {
  m_flash = &flash;
  m_lock = &lock;
  m_valid = false;

  // Try to load from flash
  if (loadFromFlash()) {
    m_valid = true;
    return true;
  }

  // Flash empty or corrupted - apply defaults
  applyDefaults();
  m_valid = false;
  return false;
}

uint16_t DeviceConfigManager::getDeviceId() const { return m_config.deviceId; }

WheelRole DeviceConfigManager::getRole() const {
  return static_cast<WheelRole>(m_config.role);
}

uint8_t DeviceConfigManager::getDefaultMode() const {
  return m_config.defaultMode;
}

bool DeviceConfigManager::setDeviceId(uint16_t id) {
  m_lock->acquire();

  m_config.deviceId = id;
  bool result = saveToFlash();

  m_lock->release();
  return result;
}

bool DeviceConfigManager::setRole(WheelRole role) {
  m_lock->acquire();

  m_config.role = static_cast<uint8_t>(role);
  bool result = saveToFlash();

  m_lock->release();
  return result;
}

bool DeviceConfigManager::setDefaultMode(uint8_t mode) {
  m_lock->acquire();

  m_config.defaultMode = mode;
  bool result = saveToFlash();

  m_lock->release();
  return result;
}

bool DeviceConfigManager::loadFromFlash() {
  if (m_flash == nullptr)
    return false;

  ITRACE(Harness::ITrace::L4_FLASH, "[L3>L4]", "flash.load", "config");
  DeviceConfig cfg;
  m_flash->read(CONFIG_FLASH_ADDR, reinterpret_cast<uint8_t *>(&cfg),
                sizeof(cfg));

  if (validateConfig(cfg)) {
    m_config = cfg;
    return true;
  }

  return false;
}

bool DeviceConfigManager::saveToFlash() {
  if (m_flash == nullptr)
    return false;

  ITRACE(Harness::ITrace::L4_FLASH, "[L3>L4]", "flash.save", "config");
  // Update CRC before saving
  m_config.magic = CONFIG_MAGIC;
  m_config.version = CONFIG_VERSION;
  m_config.crc32 = calculateCRC(reinterpret_cast<const uint8_t *>(&m_config),
                                sizeof(m_config) - sizeof(m_config.crc32));

  // Erase sector first (4KB)
  m_flash->sectorErase(CONFIG_FLASH_ADDR);

  // Write config (fits in single page)
  m_flash->pageProgram(CONFIG_FLASH_ADDR,
                       reinterpret_cast<const uint8_t *>(&m_config),
                       sizeof(m_config));

  // Verify write
  DeviceConfig verify;
  m_flash->read(CONFIG_FLASH_ADDR, reinterpret_cast<uint8_t *>(&verify),
                sizeof(verify));

  m_valid = (memcmp(&m_config, &verify, sizeof(m_config)) == 0);
  return m_valid;
}

bool DeviceConfigManager::factoryReset() {
  m_lock->acquire();

  applyDefaults();
  bool result = saveToFlash();

  m_lock->release();
  return result;
}

void DeviceConfigManager::applyDefaults() {
  memset(&m_config, 0, sizeof(m_config));
  m_config.magic = CONFIG_MAGIC;
  m_config.version = CONFIG_VERSION;
  m_config.deviceId = 0;
  m_config.role = static_cast<uint8_t>(WheelRole::UNASSIGNED);
  m_config.defaultMode = 0; // OPEN_LOOP
}

bool DeviceConfigManager::validateConfig(const DeviceConfig &cfg) {
  // Check magic
  if (cfg.magic != CONFIG_MAGIC) {
    return false;
  }

  // Check version (allow current or older)
  if (cfg.version > CONFIG_VERSION) {
    return false;
  }

  // Verify CRC
  uint32_t crc = calculateCRC(reinterpret_cast<const uint8_t *>(&cfg),
                              sizeof(cfg) - sizeof(cfg.crc32));
  if (crc != cfg.crc32) {
    return false;
  }

  return true;
}

uint32_t DeviceConfigManager::calculateCRC(const uint8_t *data, size_t len) {
  // CRC32 (polynomial 0xEDB88320, standard Ethernet/ZIP CRC)
  uint32_t crc = 0xFFFFFFFF;

  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 1) {
        crc = (crc >> 1) ^ 0xEDB88320;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc ^ 0xFFFFFFFF;
}

} // namespace Services
