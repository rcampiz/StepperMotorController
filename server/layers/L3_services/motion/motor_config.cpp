/**
 * @file motor_config.cpp
 * @brief Motor configuration manager implementation
 *
 * Uses STM32F401RE internal flash for persistent storage.
 * Flash operations require interrupts disabled during erase/program.
 */

#include "L3_services/motion/motor_config.hpp"
#include "stm32f401xe.h"
#include <string.h>

// FLASH register bit definitions (STM32F401)
// FLASH_CR bits
#define FLASH_CR_PG        (1UL << 0)   // Programming
#define FLASH_CR_SER       (1UL << 1)   // Sector erase
#define FLASH_CR_MER       (1UL << 2)   // Mass erase
#define FLASH_CR_SNB_Pos   3            // Sector number position
#define FLASH_CR_SNB_Msk   (0xFUL << FLASH_CR_SNB_Pos)
#define FLASH_CR_PSIZE_0   (1UL << 8)   // Program size bit 0
#define FLASH_CR_PSIZE_1   (1UL << 9)   // Program size bit 1
#define FLASH_CR_STRT      (1UL << 16)  // Start
#define FLASH_CR_LOCK      (1UL << 31)  // Lock

// FLASH_SR bits
#define FLASH_SR_EOP       (1UL << 0)   // End of operation
#define FLASH_SR_SOP       (1UL << 1)   // Operation error (reserved, use OPERR)
#define FLASH_SR_WRPERR    (1UL << 4)   // Write protection error
#define FLASH_SR_PGAERR    (1UL << 5)   // Programming alignment error
#define FLASH_SR_PGPERR    (1UL << 6)   // Programming parallelism error
#define FLASH_SR_PGSERR    (1UL << 7)   // Programming sequence error
#define FLASH_SR_BSY       (1UL << 16)  // Busy

namespace Services {

// Global instance
MotorConfigManager g_motorConfig;

bool MotorConfigManager::init() {
    m_valid = false;

    // Try to load from flash
    if (loadFromFlash()) {
        m_valid = true;
        return true;
    }

    // Flash empty or corrupted - apply safe defaults
    applyDefaults();
    return false;
}

void MotorConfigManager::applyDefaults() {
    memset(&m_config, 0, sizeof(m_config));
    m_config.magic = MOTOR_CONFIG_MAGIC;
    m_config.version = MOTOR_CONFIG_VERSION;

    // Safe KVAL defaults for NEMA 23 at 21V
    // Conservative values - won't damage motor but may not move heavy loads
    m_config.kvalHold = 0x20;  // ~12% duty
    m_config.kvalRun = 0x30;   // ~19% duty
    m_config.kvalAcc = 0x40;   // ~25% duty
    m_config.kvalDec = 0x40;   // ~25% duty

    // Protection thresholds - high values = less sensitive
    m_config.ocdThreshold = 0x1F;   // Max (~10A), adjust down for protection
    m_config.stallThreshold = 0x40; // Mid-range stall detection

    // Motion parameters - conservative
    m_config.acceleration = 0x08A;  // Default ACC
    m_config.deceleration = 0x08A;  // Default DEC
    m_config.maxSpeed = 0x41;       // Default MAX_SPEED
    m_config.minSpeed = 0x00;       // No minimum speed
    m_config.fsSpeed = 0x3FF;       // Full-step speed max (disables stall at high speed)

    // Fault enables - start with OCD enabled, others disabled
    // User should enable after tuning thresholds
    m_config.faultEnable.ocd = 1;       // Overcurrent - enabled
    m_config.faultEnable.thermalSD = 1; // Thermal shutdown - enabled
    m_config.faultEnable.thermalWarn = 0;
    m_config.faultEnable.uvlo = 1;      // Under-voltage - enabled
    m_config.faultEnable.stallA = 0;    // Stall - disabled (requires tuning)
    m_config.faultEnable.stallB = 0;
    m_config.faultEnable.cmdErr = 0;

    m_config.faultAction = 1;  // HardHiZ - safest option
    m_config.stepMode = 7;     // 1/128 microstep (powerSTEP01 power-on default)

    // Encoder velocity filter - EMA+SMA+Padé+Butterworth enabled by default
    m_config.encFilterType = 0x0F;  // EMA|SMA|PADE|BIQUAD
    m_config.encFilterParam = 200;  // EMA alpha=200
    m_config.encSmaWindow = 8;      // SMA window=8
    m_config.encMeasWindowMs = 40;  // 40ms measurement window
    m_config.encSampleRateDiv100 = 0; // 0 = default 1000Hz
}

void MotorConfigManager::setKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    m_config.kvalHold = hold;
    m_config.kvalRun = run;
    m_config.kvalAcc = acc;
    m_config.kvalDec = dec;
}

void MotorConfigManager::setOcdThreshold(uint8_t threshold) {
    m_config.ocdThreshold = threshold & 0x1F;  // 5-bit max
}

void MotorConfigManager::setStallThreshold(uint8_t threshold) {
    m_config.stallThreshold = threshold & 0x7F;  // 7-bit max
}

void MotorConfigManager::setMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
    m_config.acceleration = acc & 0x0FFF;  // 12-bit
    m_config.deceleration = dec & 0x0FFF;
    m_config.maxSpeed = maxSpd & 0x03FF;   // 10-bit
}

void MotorConfigManager::setFaultEnable(FaultEnableFlags flags) {
    m_config.faultEnable = flags;
}

void MotorConfigManager::setFaultAction(uint8_t action) {
    m_config.faultAction = action;
}

void MotorConfigManager::setStepMode(uint8_t mode) {
    m_config.stepMode = mode & 0x07;  // 3-bit, 0-7
}

void MotorConfigManager::setEncFilter(uint8_t type, uint8_t param) {
    m_config.encFilterType = type;
    m_config.encFilterParam = param;
}

void MotorConfigManager::setEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                           uint8_t smaWindow, uint8_t measWindowMs,
                                           uint8_t sampleRateDiv100) {
    m_config.encFilterType = flags;
    m_config.encFilterParam = emaAlpha;
    m_config.encSmaWindow = smaWindow;
    m_config.encMeasWindowMs = measWindowMs;
    m_config.encSampleRateDiv100 = sampleRateDiv100;
}

void MotorConfigManager::setFullStepsPerRev(uint16_t steps) {
    if (steps == 0) steps = 200;
    m_config.fullStepsPerRev = steps;
}

void MotorConfigManager::setEncoderPPR(uint16_t ppr) {
    if (ppr == 0) ppr = 4000;
    m_config.encoderPPR = ppr;
}

void MotorConfigManager::setPidGains(int16_t kp100, int16_t ki100, int16_t kd100) {
    m_config.pidKp100 = kp100;
    m_config.pidKi100 = ki100;
    m_config.pidKd100 = kd100;
}

void MotorConfigManager::setPidLimits(uint16_t outputLimit, uint16_t integralLimit) {
    m_config.pidOutputLimit = outputLimit;
    m_config.pidIntegralLimit = integralLimit;
}

void MotorConfigManager::setFollowThresholds(int16_t moveErr, uint16_t moveTimeMs,
                                              int16_t holdErr, uint16_t holdTimeMs,
                                              int16_t hardLimit, uint8_t maxRetries) {
    m_config.followMoveError  = moveErr;
    m_config.followMoveTimeMs = moveTimeMs;
    m_config.followHoldError  = holdErr;
    m_config.followHoldTimeMs = holdTimeMs;
    m_config.followHardLimit  = hardLimit;
    m_config.followMaxRetries = maxRetries;
}

bool MotorConfigManager::loadFromFlash() {
    // Read config from flash
    const MotorConfig* flashConfig = reinterpret_cast<const MotorConfig*>(MOTOR_CONFIG_FLASH_ADDR);

    if (validateConfig(*flashConfig)) {
        m_config = *flashConfig;
        // Clamp stepMode to valid range (0-7)
        if (m_config.stepMode > 7) {
            m_config.stepMode = 7;
        }
        // Migrate V1 → V2: filter field reinterpretation
        if (m_config.version < 2) {
            // V1 encFilterType: 0=NONE, 1=EMA, 2=SMA (mutually exclusive)
            // V2 encFilterType: bitfield (bit0=EMA, bit1=SMA)
            // Values 0, 1, 2 map cleanly: 0→0, 1→bit0, 2→bit1
            // But V1 encFilterParam was SMA window when type==2
            if (m_config.encFilterType == 2) {
                // Old SMA mode: param was SMA window, move to dedicated field
                m_config.encSmaWindow = m_config.encFilterParam;
                m_config.encFilterParam = 0; // No EMA alpha
            }
            m_config.version = 2;
        }
        return true;
    }

    return false;
}

bool MotorConfigManager::saveToFlash() {
    // Update header
    m_config.magic = MOTOR_CONFIG_MAGIC;
    m_config.version = MOTOR_CONFIG_VERSION;

    // Calculate CRC (exclude CRC field itself)
    m_config.crc32 = calculateCRC(
        reinterpret_cast<const uint8_t*>(&m_config),
        sizeof(m_config) - sizeof(m_config.crc32)
    );

    // Erase and write
    if (!eraseFlashPage()) {
        return false;
    }

    if (!writeFlash(reinterpret_cast<const uint8_t*>(&m_config), sizeof(m_config))) {
        return false;
    }

    // Verify
    const MotorConfig* flashConfig = reinterpret_cast<const MotorConfig*>(MOTOR_CONFIG_FLASH_ADDR);
    m_valid = (memcmp(&m_config, flashConfig, sizeof(m_config)) == 0);

    return m_valid;
}

bool MotorConfigManager::factoryReset() {
    applyDefaults();
    return saveToFlash();
}

bool MotorConfigManager::validateConfig(const MotorConfig& cfg) {
    // Check magic
    if (cfg.magic != MOTOR_CONFIG_MAGIC) {
        return false;
    }

    // Check version
    if (cfg.version > MOTOR_CONFIG_VERSION) {
        return false;
    }

    // Verify CRC
    uint32_t crc = calculateCRC(
        reinterpret_cast<const uint8_t*>(&cfg),
        sizeof(cfg) - sizeof(cfg.crc32)
    );

    return (crc == cfg.crc32);
}

uint32_t MotorConfigManager::calculateCRC(const uint8_t* data, size_t len) {
    // CRC32 (polynomial 0xEDB88320)
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

bool MotorConfigManager::eraseFlashPage() {
    // Unlock flash
    if ((FLASH->CR & FLASH_CR_LOCK) != 0) {
        FLASH->KEYR = 0x45670123;
        FLASH->KEYR = 0xCDEF89AB;
    }

    // Wait for any ongoing operation
    while (FLASH->SR & FLASH_SR_BSY);

    // Clear error flags
    FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;

    // Set up sector erase for Sector 7 (our config is in sector 7)
    // PSIZE = 2 (32-bit), SNB = 7, SER = 1
    FLASH->CR = FLASH_CR_PSIZE_1 | (7 << FLASH_CR_SNB_Pos) | FLASH_CR_SER;

    // Start erase
    FLASH->CR |= FLASH_CR_STRT;

    // Wait for completion
    while (FLASH->SR & FLASH_SR_BSY);

    // Check for errors
    if (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
        FLASH->CR |= FLASH_CR_LOCK;
        return false;
    }

    // Clear SER
    FLASH->CR &= ~FLASH_CR_SER;

    return true;
}

bool MotorConfigManager::writeFlash(const uint8_t* data, size_t len) {
    // Flash should already be unlocked from erase

    // Set programming mode (32-bit)
    FLASH->CR = FLASH_CR_PSIZE_1 | FLASH_CR_PG;

    // Write 32 bits at a time
    uint32_t addr = MOTOR_CONFIG_FLASH_ADDR;
    const uint32_t* src = reinterpret_cast<const uint32_t*>(data);
    size_t words = (len + 3) / 4;

    for (size_t i = 0; i < words; i++) {
        *reinterpret_cast<volatile uint32_t*>(addr) = src[i];

        // Wait for completion
        while (FLASH->SR & FLASH_SR_BSY);

        // Check for errors
        if (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) {
            FLASH->CR &= ~FLASH_CR_PG;
            FLASH->CR |= FLASH_CR_LOCK;
            return false;
        }

        addr += 4;
    }

    // Disable programming mode and lock flash
    FLASH->CR &= ~FLASH_CR_PG;
    FLASH->CR |= FLASH_CR_LOCK;

    return true;
}

} // namespace Services
