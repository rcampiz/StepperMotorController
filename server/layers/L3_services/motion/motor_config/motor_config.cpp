/**
 * @file motor_config.cpp
 * @brief Motor configuration manager implementation
 *
 * Persists a 128-byte record through Drivers::IConfigStore (the internal-flash journal).
 * V5 holds the driver settings in physical units; V1-V4 records (powerSTEP01 register
 * values) are migrated at load through the driver's own translation.
 */

#include "L3_services/motion/motor_config/motor_config.hpp"
#include "harness/pins/encoder_filter_config.hpp"
#include <stddef.h>
#include <string.h>
#include <limits.h>

namespace Services {

// Global instance
MotorConfigManager g_motorConfig;

namespace {
/// No driver bound (host tests of the store alone): nothing is capped or offered less
const Harness::DriverCaps NO_DRIVER_CAPS = {
    true, true, true, 256, 256,
    false, false, true, 0, 0x3F, 0x3F,
    100.0f, 0.001f, 1.0f / 256.0f,
    0.0f, 1000.0f, 0.001f, 0.0f, 0.001f,
    100000.0f, 100000.0f, 0.001f,
    0.001f, 1000000.0f, 0.001f,
    INT32_MIN, INT32_MAX,
};
} // namespace

bool MotorConfigManager::init(Drivers::IConfigStore& store, Drivers::IMotorDriver* driver) {
    m_store = &store;
    m_driver = driver;
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
            // The record read above goes back as journal entry 0 (a migrated one as V5)
            m_config.magic = MOTOR_CONFIG_MAGIC;
            m_config.version = MOTOR_CONFIG_VERSION;
            m_config.crc32 = calculateCRC(reinterpret_cast<const uint8_t*>(&m_config),
                                          sizeof(m_config) - sizeof(m_config.crc32));
            writeRecord();
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

const Harness::DriverCaps& MotorConfigManager::caps() const {
    return (m_driver != nullptr) ? m_driver->caps() : NO_DRIVER_CAPS;
}

Harness::DriverSettings MotorConfigManager::getAppliedSettings() const {
    Harness::DriverSettings s = m_config.driver;
    const float k = static_cast<float>(getCurrentScalePct()) / 100.0f;
    if (k != 1.0f) {
        Harness::DriveLevels* lv[2] = {&s.currentA, &s.duty};
        for (Harness::DriveLevels* l : lv) {
            l->hold *= k; l->run *= k; l->acc *= k; l->dec *= k;
        }
        float* d[4] = {&s.duty.hold, &s.duty.run, &s.duty.acc, &s.duty.dec};
        for (float* v : d) {
            if (*v > 1.0f) *v = 1.0f;                // a duty never above the supply
        }
    }
    return s;
}

Harness::DriverSettings MotorConfigManager::getEffectiveSettings() const {
    return (m_driver != nullptr) ? m_driver->quantize(m_config.driver) : m_config.driver;
}

uint16_t MotorConfigManager::getEffectiveMicrosteps() const {
    return Harness::effectiveMicrosteps(caps(), m_config.driver.microsteps, m_config.driver.mode);
}

bool MotorConfigManager::toLegacyUnits(Drivers::LegacyDriverUnits& out) const {
    return m_driver != nullptr && m_driver->toLegacyUnits(m_config.driver, out);
}

bool MotorConfigManager::setFromLegacyUnits(const Drivers::LegacyDriverUnits& in) {
    Harness::DriverSettings s = m_config.driver;
    if (m_driver == nullptr || !m_driver->fromLegacyUnits(in, s)) {
        return false;
    }
    // A zero reference code was "the default" since V3 (DRV:CFG:TVAL 00, a V2 blob)
    if (in.tval[0] == 0) s.currentA.hold = DEFAULT_HOLD_A;
    if (in.tval[1] == 0) s.currentA.run = DEFAULT_RUN_A;
    if (in.tval[2] == 0) s.currentA.acc = DEFAULT_ACC_A;
    if (in.tval[3] == 0) s.currentA.dec = DEFAULT_ACC_A;
    m_config.driver = s;
    return true;
}

void MotorConfigManager::applyDefaults() {
    memset(&m_config, 0, sizeof(m_config));
    m_config.magic = MOTOR_CONFIG_MAGIC;
    m_config.version = MOTOR_CONFIG_VERSION;

    Harness::DriverSettings& d = m_config.driver;
    d.microsteps = 128;                         // the finest step; the driver caps it per mode
    d.mode = Harness::DriveMode::CURRENT;       // bench 2026-10-17: smoother than voltage drive to 400 steps/s
    d.currentA = { DEFAULT_HOLD_A, DEFAULT_RUN_A, DEFAULT_ACC_A, DEFAULT_ACC_A };
    d.duty = { 0.125f, 0.1875f, 0.25f, 0.25f }; // voltage mode, NEMA 23 at 21 V: conservative
    d.accelFsps2 = 2008.2f;                     // the ramps and ceiling the firmware always booted with
    d.decelFsps2 = 2008.2f;
    d.maxSpeedFsps = 992.0f;
    // Short-circuit guard, not a current limit (the current reference limits the motor). Bench
    // 2026-10-19, IHM03A1: powerSTEP01 OCD_TH 0..1 trip on switching transients while running,
    // 2 and up stay clear. 2026-10-20, with the lower motion currents (1.01 / 1.17 A): OCD_TH 0
    // (26 A) still flags ~24 times a minute running; 1 and 2 soak clean (1 flagged once at the
    // old currents). 78 A = OCD_TH 2 (94 mV), the lowest clean in every soak.
    d.overcurrentA = 78.0f;
    d.stallThreshold01 = 0.0f;
    // Fault alarms: overcurrent, thermal shutdown, undervoltage; stall needs tuning first
    d.faultAlarms = Harness::DriverFault::OVERCURRENT | Harness::DriverFault::THERMAL_SHUTDOWN
                  | Harness::DriverFault::UNDERVOLTAGE;
    // d.tuning stays tag 0: the driver's own defaults

    m_config.faultAction = 1;  // HardHiZ - safest option

    // Encoder velocity filter: the tracking observer alone (stage parameters 0: their defaults)
    m_config.encFilterType = Harness::ENC_FILT_DEFAULT_FLAGS;
    m_config.encFilterParam = 200;  // EMA alpha=200
    m_config.encSmaWindow = 8;      // SMA window=8
    m_config.encMeasWindowMs = 40;  // 40ms measurement window
    m_config.encSampleRateDiv100 = 0; // 0 = default 1000Hz
}

void MotorConfigManager::setFaultAction(uint8_t action) {
    m_config.faultAction = action;
}

void MotorConfigManager::setEncFilter(uint8_t type, uint8_t param) {
    m_config.encFilterType = type;
    m_config.encFilterParam = param;
}

void MotorConfigManager::setEncFilterFull(const Harness::EncoderFilterConfig& cfg) {
    m_config.encFilterType = cfg.filterFlags;
    m_config.encFilterParam = cfg.emaAlpha;
    m_config.encSmaWindow = cfg.smaWindow;
    m_config.encMeasWindowMs = cfg.measWindowMs;
    // stored /100, 0 = the 1 kHz default
    m_config.encSampleRateDiv100 = (cfg.sampleRateHz != 0 && cfg.sampleRateHz != 1000)
                                       ? static_cast<uint8_t>(cfg.sampleRateHz / 100) : 0;
    m_config.encPadeGainPct = cfg.padeGainPct;
    m_config.encPadeMaxCorr = cfg.padeMaxCorr;
    m_config.encBiquadCutoffHz = cfg.biquadCutoffHz;
    m_config.encNotchCenterHz = cfg.notchCenterHz;
    m_config.encNotchQ10 = cfg.notchQ10;
    m_config.encHoltAlpha = cfg.holtAlpha;
    m_config.encHoltBeta = cfg.holtBeta;
    m_config.encObserverBwHz = cfg.observerBwHz;
}

Harness::ResonanceConfig MotorConfigManager::getResonanceConfig() const {
    Harness::ResonanceConfig c = {};
    c.enabled = (m_config.resFlags & 0x01u) != 0u;
    c.harmonicMask = static_cast<uint8_t>((m_config.resFlags >> 1) & 0x07u);
    c.f1AHz = static_cast<float>(m_config.resF1AHz10) / 10.0f;
    c.exponent = static_cast<float>(m_config.resExponent100) / 100.0f;
    c.bandPct = m_config.resBandPct;
    c.boostPct = m_config.resBoostPct;
    c.cutPct = m_config.resCutPct;
    return c;
}

void MotorConfigManager::setResonanceConfig(const Harness::ResonanceConfig& c) {
    const float f = c.f1AHz * 10.0f + 0.5f;
    const float e = c.exponent * 100.0f + 0.5f;
    m_config.resF1AHz10 = (f <= 0.0f) ? 0u : (f >= 65535.0f) ? 65535u : static_cast<uint16_t>(f);
    m_config.resExponent100 = (e <= 0.0f) ? 0u : (e >= 255.0f) ? 255u : static_cast<uint8_t>(e);
    m_config.resFlags = static_cast<uint8_t>((c.enabled ? 0x01u : 0u) | ((c.harmonicMask & 0x07u) << 1));
    m_config.resBandPct = c.bandPct;
    m_config.resBoostPct = c.boostPct;
    m_config.resCutPct = c.cutPct;
}

Harness::EncoderFilterConfig MotorConfigManager::getEncFilterConfig() const {
    Harness::EncoderFilterConfig cfg = {};
    cfg.filterFlags = m_config.encFilterType;
    cfg.emaAlpha = m_config.encFilterParam;
    cfg.smaWindow = m_config.encSmaWindow;
    cfg.measWindowMs = getEncMeasWindowMs();
    cfg.sampleRateHz = getEncSampleRateHz();
    cfg.padeGainPct = m_config.encPadeGainPct;
    cfg.padeMaxCorr = m_config.encPadeMaxCorr;
    cfg.biquadCutoffHz = m_config.encBiquadCutoffHz;
    cfg.notchCenterHz = m_config.encNotchCenterHz;
    cfg.notchQ10 = m_config.encNotchQ10;
    cfg.holtAlpha = m_config.encHoltAlpha;
    cfg.holtBeta = m_config.encHoltBeta;
    cfg.observerBwHz = m_config.encObserverBwHz;
    return cfg;
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

void MotorConfigManager::migrateV4(const MotorConfigV4& old) {
    // Non-driver fields carry over; driver fields start at the V5 defaults
    applyDefaults();
    m_config.faultAction = old.faultAction;
    m_config.encFilterType = old.encFilterType;
    m_config.encFilterParam = old.encFilterParam;
    m_config.encSmaWindow = old.encSmaWindow;
    m_config.encMeasWindowMs = old.encMeasWindowMs;
    m_config.encSampleRateDiv100 = old.encSampleRateDiv100;
    m_config.fullStepsPerRev = old.fullStepsPerRev;
    m_config.encoderPPR = old.encoderPPR;
    m_config.pidKp100 = old.pidKp100;
    m_config.pidKi100 = old.pidKi100;
    m_config.pidKd100 = old.pidKd100;
    m_config.pidOutputLimit = old.pidOutputLimit;
    m_config.pidIntegralLimit = old.pidIntegralLimit;
    m_config.followMoveError = old.followMoveError;
    m_config.followMoveTimeMs = old.followMoveTimeMs;
    m_config.followHoldError = old.followHoldError;
    m_config.followHoldTimeMs = old.followHoldTimeMs;
    m_config.followHardLimit = old.followHardLimit;
    m_config.followMaxRetries = old.followMaxRetries;
    m_config.idleReleaseMs = old.idleReleaseMs;

    // Driver fields: the old blob holds the powerSTEP01's own units; only the driver knows
    // them (a driver for another chip declines and the driver defaults stay)
    Drivers::LegacyDriverUnits u = {};
    u.kval[0] = old.kvalHold; u.kval[1] = old.kvalRun; u.kval[2] = old.kvalAcc; u.kval[3] = old.kvalDec;
    u.tval[0] = old.tvalHold; u.tval[1] = old.tvalRun; u.tval[2] = old.tvalAcc; u.tval[3] = old.tvalDec;
    u.driveMode = old.driveMode ? 1 : 0;
    u.stepMode = old.stepMode > 7 ? 7 : old.stepMode;
    u.ocdTh = old.ocdThreshold;
    u.stallTh = old.stallThreshold;
    u.acc = old.acceleration;
    u.dec = old.deceleration;
    u.maxSpeed = old.maxSpeed;
    u.minSpeed = old.minSpeed;
    u.fsSpd = old.fsSpeed;
    u.predEn = (old.cmPredEn != 1) ? 1 : 0;                       // 0 = default = on
    u.tsw = old.cmTswPlus1 ? static_cast<uint8_t>(old.cmTswPlus1 - 1u) : 0xFF;
    u.tFast = old.cmTFast;                                          // 0 = chip default, kept
    u.tonMin = old.cmTonMin;
    u.toffMin = old.cmToffMin;
    u.alarms = static_cast<uint8_t>((old.faultEnable.ocd ? 1u : 0u) | (old.faultEnable.thermalSD ? 2u : 0u)
             | (old.faultEnable.thermalWarn ? 4u : 0u) | (old.faultEnable.uvlo ? 8u : 0u)
             | (old.faultEnable.stallA ? 16u : 0u) | (old.faultEnable.stallB ? 32u : 0u)
             | (old.faultEnable.cmdErr ? 64u : 0u));
    (void)setFromLegacyUnits(u);
    m_migrated = true;
}

bool MotorConfigManager::validateRecord(const uint8_t* rec, uint16_t maxVersion) {
    // magic, version and the CRC of a 128-byte record (V4 and V5 share the frame)
    uint32_t magic;
    uint16_t version;
    uint32_t crc;
    memcpy(&magic, rec, sizeof(magic));
    memcpy(&version, rec + 4, sizeof(version));
    memcpy(&crc, rec + offsetof(MotorConfig, crc32), sizeof(crc));
    if (magic != MOTOR_CONFIG_MAGIC || version == 0 || version > maxVersion) {
        return false;
    }
    return calculateCRC(rec, offsetof(MotorConfig, crc32)) == crc;
}

bool MotorConfigManager::loadFromFlash() {
    if (m_store == nullptr) {
        return false;
    }
    uint8_t rec[sizeof(MotorConfig)];
    if (m_store->read(rec, sizeof(rec)) != sizeof(rec)) {
        return false;
    }
    m_migrated = false;

    if (validateRecord(rec, MOTOR_CONFIG_VERSION)) {
        uint16_t version;
        memcpy(&version, rec + 4, sizeof(version));
        if (version == MOTOR_CONFIG_VERSION) {
            memcpy(&m_config, rec, sizeof(m_config));
            return true;
        }
        if (version == 4) {
            MotorConfigV4 v4;
            memcpy(&v4, rec, sizeof(v4));
            migrateV4(v4);
            return true;
        }
        // 128-byte records were only ever V4 or V5
    }

    // A 64-byte V1-V3 blob (a pre-V4 journal record or the legacy single blob): its fields
    // are the prefix of the V4 layout; bring it to V4, then migrate
    MotorConfigV3 old;
    memcpy(&old, rec, sizeof(old));
    if (!validateV3(old)) {
        return false;
    }
    MotorConfigV4 v4;
    memset(&v4, 0, sizeof(v4));
    memcpy(&v4, old.prefix, sizeof(old.prefix));
    // V1 -> V2: filter field reinterpretation (V1 SMA mode kept its window in encFilterParam)
    if (v4.version < 2 && v4.encFilterType == 2) {
        v4.encSmaWindow = v4.encFilterParam;
        v4.encFilterParam = 0;
    }
    // V2 -> V3: the bytes were reserved (zero): current mode with the default currents (zero
    // reference codes are "the default"); the saved voltage-mode duties stay
    if (v4.version < 3) {
        v4.driveMode = 1;
        v4.tvalHold = v4.tvalRun = v4.tvalAcc = v4.tvalDec = 0;
    }
    // V3 -> V4: regulation at the chip's defaults (zero), no idle release (zero)
    v4.version = 4;
    migrateV4(v4);
    return true;
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

bool MotorConfigManager::validateV3(const MotorConfigV3& cfg) {
    uint32_t magic;
    uint16_t version;
    memcpy(&magic, cfg.prefix, sizeof(magic));
    memcpy(&version, cfg.prefix + 4, sizeof(version));
    if (magic != MOTOR_CONFIG_MAGIC || version == 0 || version > 3) {
        return false;
    }
    return calculateCRC(cfg.prefix, sizeof(cfg.prefix)) == cfg.crc32;
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
