/**
 * @file motor_config.cpp
 * @brief Motor configuration manager implementation
 *
 * Uses STM32F401RE internal flash for persistent storage.
 * Flash operations require interrupts disabled during erase/program.
 */

#include "L3_services/motion/motor_config/motor_config.hpp"
#include <stddef.h>
#include <string.h>

namespace Services {

// Global instance
MotorConfigManager g_motorConfig;

bool MotorConfigManager::init(Drivers::IConfigStore& store) {
    m_store = &store;
    m_valid = false;

    // Try to load from flash
    const bool loaded = loadFromFlash();
    if (!loaded) {
        applyDefaults();          // flash empty or corrupted: safe defaults
    }
    m_valid = loaded;

    // Boot-time compaction: nothing moves yet, so the sector erase costs only boot time.
    // Keeps a run-time erase (which needs the motor stopped) from ever being needed in
    // normal use: one erase per ~2000 saves, taken here.
    if (savesRemaining() < COMPACT_MARGIN_SAVES) {
        if (m_store->erase() && loaded) {
            writeRecord();        // the record read above goes back as journal entry 0
        }
    }
    return loaded;
}

size_t MotorConfigManager::savesRemaining() const {
    if (m_store == nullptr) {
        return 0;
    }
    return m_store->remaining() / sizeof(MotorConfig);
}

bool MotorConfigManager::writeRecord() {
    if (!m_store->write(reinterpret_cast<const uint8_t*>(&m_config), sizeof(m_config))) {
        return false;
    }
    MotorConfig verify;
    return (m_store->read(reinterpret_cast<uint8_t*>(&verify), sizeof(verify)) == sizeof(verify))
        && (memcmp(&m_config, &verify, sizeof(m_config)) == 0);
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
    m_config.driveMode = 1;    // current mode (TVAL): bench 2026-10-17, smoother than KVAL to 400 steps/s
    m_config.tvalHold = TVAL_DEFAULT_HOLD;
    m_config.tvalRun = TVAL_DEFAULT_RUN;
    m_config.tvalAcc = TVAL_DEFAULT_ACC;
    m_config.tvalDec = TVAL_DEFAULT_ACC;

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

void MotorConfigManager::setDriveMode(uint8_t mode) {
    m_config.driveMode = mode ? 1 : 0;
}

void MotorConfigManager::setTval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    m_config.tvalHold = hold & 0x7F;
    m_config.tvalRun = run & 0x7F;
    m_config.tvalAcc = acc & 0x7F;
    m_config.tvalDec = dec & 0x7F;
}

void MotorConfigManager::setCmRegulation(bool predEn, uint8_t tsw, uint8_t tFast,
                                         uint8_t tonMin, uint8_t toffMin) {
    m_config.cmPredEn = predEn ? 2 : 1;
    m_config.cmTswPlus1 = (tsw == 0xFF) ? 0 : static_cast<uint8_t>((tsw & 0x1F) + 1);
    m_config.cmTFast = tFast;
    m_config.cmTonMin = tonMin & 0x7F;
    m_config.cmToffMin = toffMin & 0x7F;
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
    // Read the stored blob through the L4 config store
    if (m_store == nullptr) {
        return false;
    }
    MotorConfig flashConfig;
    if (m_store->read(reinterpret_cast<uint8_t*>(&flashConfig), sizeof(flashConfig)) != sizeof(flashConfig)) {
        return false;
    }

    bool found = validateConfig(flashConfig);
    if (!found) {
        // A 64-byte V1-V3 blob (a pre-V4 journal record or the legacy single blob): its
        // fields are the prefix of the V4 layout, so copy them and zero the V4 tail
        MotorConfigV3 old;
        memcpy(&old, &flashConfig, sizeof(old));
        if (validateLegacyV3(old)) {
            memset(&flashConfig, 0, sizeof(flashConfig));
            memcpy(&flashConfig, &old, offsetof(MotorConfigV3, crc32));
            found = true;
        }
    }
    if (found) {
        m_config = flashConfig;
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
        // Migrate V2 -> V3: the bytes were reserved (zero): current mode (the V3 default)
        // with the default TVALs; the saved KVALs stay for anyone switching back
        if (m_config.version < 3) {
            m_config.driveMode = 1;
            m_config.tvalHold = TVAL_DEFAULT_HOLD;
            m_config.tvalRun = TVAL_DEFAULT_RUN;
            m_config.tvalAcc = m_config.tvalDec = TVAL_DEFAULT_ACC;
            m_config.version = 3;
        }
        // Migrate V3 -> V4: regulation at chip defaults (0), reserved tail zero
        if (m_config.version < 4) {
            m_config.cmPredEn = m_config.cmTswPlus1 = m_config.cmTFast = 0;
            m_config.cmTonMin = m_config.cmToffMin = 0;
            memset(m_config.reserved4, 0, sizeof(m_config.reserved4));
            m_config.version = 4;
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

    if (m_store == nullptr) {
        m_lastSave = SaveStatus::WriteFailed;
        return false;
    }

    // Append first: on the journal store this is a few word programs, no erase, no stall.
    if (m_store->remaining() >= sizeof(m_config)) {
        m_valid = writeRecord();
        m_lastSave = m_valid ? SaveStatus::Ok : SaveStatus::WriteFailed;
        return m_valid;
    }

    // Journal full: the sector erase freezes the MCU for 1-2 s (ESTOP included), so it is
    // only taken when the gate says the motor is stopped; otherwise the caller reports
    // STORE_FULL and the user saves again once stopped.
    if (m_eraseGate != nullptr && !m_eraseGate()) {
        m_lastSave = SaveStatus::StoreFull;
        return false;
    }
    if (!m_store->erase()) {
        m_lastSave = SaveStatus::WriteFailed;
        return false;
    }
    m_valid = writeRecord();
    m_lastSave = m_valid ? SaveStatus::Ok : SaveStatus::WriteFailed;
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

bool MotorConfigManager::validateLegacyV3(const MotorConfigV3& cfg) {
    if (cfg.magic != MOTOR_CONFIG_MAGIC || cfg.version == 0 || cfg.version > 3) {
        return false;
    }
    return calculateCRC(reinterpret_cast<const uint8_t*>(&cfg), sizeof(cfg) - sizeof(cfg.crc32)) == cfg.crc32;
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


} // namespace Services
