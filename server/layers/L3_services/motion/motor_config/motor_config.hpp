/**
 * @file motor_config.hpp
 * @brief Motor configuration stored in internal flash
 *
 * Persists across reboots:
 *   - the motor driver's settings, in physical units (Harness::DriverSettings: microsteps,
 *     drive mode, currents in amps / duty, ramps in full steps/s^2, speed ceiling, thresholds,
 *     fault alarms, and the driver's own opaque tuning block)
 *   - the non-driver configuration: encoder filter, encoder PPR, full steps per revolution,
 *     speed-trim / PID gains, following-error thresholds, idle release, fault action
 *
 * Storage: a 128-byte record in the flash journal (Drivers::IConfigStore), magic + CRC32.
 * V5 (driver boundary step 3) stores DriverSettings; V1-V4 blobs held powerSTEP01 register
 * values and are migrated once at load by asking the driver to translate them
 * (IMotorDriver::fromLegacyUnits): the chip's units never appear here.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "harness/defs/harness_l3_l4.hpp"

namespace Services {

constexpr uint32_t MOTOR_CONFIG_MAGIC = 0x4D4F5452;  // "MOTR"
constexpr uint16_t MOTOR_CONFIG_VERSION = 5;   // V5: physical DriverSettings (driver boundary step 3)

/**
 * @brief The V1-V4 fault enable flags (one byte; migration only)
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
 * @brief Motor configuration, V5 (128 bytes, one journal slot)
 */
struct MotorConfig {
    uint32_t magic;               // 0x4D4F5452 - validity marker
    uint16_t version;             // 5
    uint8_t  faultAction;         // 0=HardStop, 1=HardHiZ, 2=SoftStop
    uint8_t  encFilterType;       // bitfield: bit0 EMA, bit1 SMA, bit2 PADE, bit3 BIQUAD

    Harness::DriverSettings driver;   // offset 8, 68 bytes: physical, as the driver takes it

    uint8_t  encFilterParam;      // EMA alpha (0-255, higher = smoother)
    uint8_t  encSmaWindow;        // SMA filter window (2-32, 0 = adaptive)
    uint8_t  encMeasWindowMs;     // encoder measurement window ms (0 = default 40)
    uint8_t  encSampleRateDiv100; // DMA sample rate / 100 (0 = default 1 kHz)
    uint16_t fullStepsPerRev;     // mechanical full steps per revolution (0 = 200)
    uint16_t encoderPPR;          // encoder pulses per revolution (0 = 4000)

    // Speed-trim PI gains (reinterpreted PID fields), x100; all zero = trim disabled
    int16_t  pidKp100;
    int16_t  pidKi100;
    int16_t  pidKd100;            // trim max percent (1-50), 0 = default 8 %
    uint16_t pidOutputLimit;      // max trim output (ticks/s), 0 = default 500
    uint16_t pidIntegralLimit;    // anti-windup clamp (ticks/s), 0 = output limit

    // Following-error supervisor (closed-loop modes); 0 = compiled default
    int16_t  followMoveError;
    uint16_t followMoveTimeMs;
    int16_t  followHoldError;
    uint16_t followHoldTimeMs;
    int16_t  followHardLimit;
    uint8_t  followMaxRetries;
    uint8_t  reserved0;
    uint16_t idleReleaseMs;       // coils to Hi-Z after this long stopped and energised; 0 = never
    uint8_t  reserved5[16];       // zero

    uint32_t crc32;               // CRC32 of everything above
};

static_assert(sizeof(MotorConfig) == 128, "MotorConfig must be 128 bytes (one journal slot)");
static_assert(offsetof(MotorConfig, driver) == 8, "DriverSettings at a fixed offset (storage format)");
static_assert(offsetof(MotorConfig, crc32) == 124, "the CRC closes the 128-byte record");

/// The V4 128-byte layout: powerSTEP01 register values. Read only, to migrate (the driver
/// translates the chip fields: IMotorDriver::fromLegacyUnits).
struct MotorConfigV4 {
    uint32_t magic;
    uint16_t version;
    uint8_t  driveMode;           // 0 voltage, 1 current
    uint8_t  tvalHold;            // current-mode reference codes, 0 = default
    uint8_t  kvalHold;            // voltage-mode duty codes
    uint8_t  kvalRun;
    uint8_t  kvalAcc;
    uint8_t  kvalDec;
    uint8_t  ocdThreshold;
    uint8_t  stallThreshold;
    uint8_t  encMeasWindowMs;
    uint8_t  encSampleRateDiv100;
    uint16_t acceleration;        // ACC code
    uint16_t deceleration;        // DEC code
    uint16_t maxSpeed;            // MAX_SPEED code
    uint16_t minSpeed;            // MIN_SPEED code
    uint16_t fsSpeed;             // full-step switch code
    uint8_t  tvalRun;
    uint8_t  tvalAcc;
    FaultEnableFlags faultEnable;
    uint8_t  faultAction;
    uint8_t  stepMode;            // log2 of the microsteps asked for (0..7)
    uint8_t  encSmaWindow;
    uint8_t  encFilterType;
    uint8_t  encFilterParam;
    uint16_t fullStepsPerRev;
    uint16_t encoderPPR;
    int16_t  pidKp100;
    int16_t  pidKi100;
    int16_t  pidKd100;
    uint16_t pidOutputLimit;
    uint16_t pidIntegralLimit;
    int16_t  followMoveError;
    uint16_t followMoveTimeMs;
    int16_t  followHoldError;
    uint16_t followHoldTimeMs;
    int16_t  followHardLimit;
    uint8_t  followMaxRetries;
    uint8_t  tvalDec;
    // V4 (2026-10-18): current-mode regulation codes (0 = chip default) and idle release
    uint8_t  cmPredEn;            // 0 default (on), 1 off, 2 on
    uint8_t  cmTswPlus1;          // TSW + 1, 0 = default
    uint8_t  cmTFast;
    uint8_t  cmTonMin;
    uint8_t  cmToffMin;
    uint16_t idleReleaseMs;
    uint8_t  reserved4[56];
    uint32_t crc32;
};

static_assert(sizeof(MotorConfigV4) == 128, "MotorConfigV4 is the 128-byte V4 blob");

/// The V1-V3 64-byte layout: the prefix of MotorConfigV4 up to tvalDec, then the CRC.
/// Read only, to migrate a blob written before V4.
struct MotorConfigV3 {
    uint8_t  prefix[offsetof(MotorConfigV4, cmPredEn)];
    uint32_t crc32;
};

static_assert(sizeof(MotorConfigV3) == 64, "MotorConfigV3 is the pre-V4 64-byte blob");

/**
 * @brief Motor configuration manager
 *
 * Reads from flash at init, writes on explicit save operations. Driver settings are held
 * in physical units; the driver bound at init supplies the capabilities (what the drive
 * mode's resolution is capped at, ranges) and the protocol-2 chip-unit translation.
 */
class MotorConfigManager {
public:
    /**
     * @brief Bind the persistent store and the motor driver (H_L3_L4), load the configuration
     * @param driver capabilities and the V1-V4 / protocol 2 chip-unit translation; nullptr
     *        (host tests) = no caps (nothing capped) and no migration of driver fields
     * @return true if a valid config was loaded, false if defaults were applied
     */
    bool init(Drivers::IConfigStore& store, Drivers::IMotorDriver* driver = nullptr);

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

    const MotorConfig& getConfig() const { return m_config; }
    MotorConfig& getConfigMutable() { return m_config; }

    // --- The driver's settings (physical) ---
    /// The bound driver's capabilities (a permissive placeholder when none is bound)
    const Harness::DriverCaps& caps() const;
    const Harness::DriverSettings& getDriverSettings() const { return m_config.driver; }
    void setDriverSettings(const Harness::DriverSettings& s) { m_config.driver = s; }
    /// The settings as the driver will put them in force (its resolution cap and grid)
    Harness::DriverSettings getEffectiveSettings() const;
    /// Protocol 2 chip units of the settings (the driver translates); false: no such units
    bool toLegacyUnits(Drivers::LegacyDriverUnits& out) const;
    /// Settings from protocol 2 chip units (the driver translates); false: not translated.
    /// A zero current code means "the default current" for that phase, as V3/V4 did.
    bool setFromLegacyUnits(const Drivers::LegacyDriverUnits& in);

    // Project defaults for the motor, StepperOnline 23HS22-2804-ME1K (NEMA 23, 2.8 A/phase,
    // 0.9 ohm, 2.5 mH, 1000-line encoder): hold 0.31 A, run 1.33 A, acc/dec 1.56 A. Chosen for
    // a battery robot (bench 2026-10-18, 12.5 V); 2026-10-19: 25 % under 0.39 / 1.72 / 2.03 A.
    static constexpr float DEFAULT_HOLD_A = 0.312f;
    static constexpr float DEFAULT_RUN_A  = 1.326f;
    static constexpr float DEFAULT_ACC_A  = 1.56f;    // acc and dec

    bool    isCurrentMode() const { return m_config.driver.mode == Harness::DriveMode::CURRENT; }
    uint8_t getDriveMode() const { return isCurrentMode() ? 1 : 0; }
    void    setDriveMode(Harness::DriveMode mode) { m_config.driver.mode = mode; }
    /// Resolution asked for (kept when the drive mode caps it, so going back restores it)
    uint16_t getMicrosteps() const { return m_config.driver.microsteps; }
    void     setMicrosteps(uint16_t microsteps) { m_config.driver.microsteps = microsteps != 0 ? microsteps : 1; }
    /// The resolution in force: the request capped by the driver for the drive mode (bench
    /// 2026-10-18: 1/128 in powerSTEP01 current mode ran 1/16, a 1-rev jog turned 8 revs).
    /// Every position conversion uses this one.
    uint16_t getEffectiveMicrosteps() const;
    uint8_t  getFaultAlarms() const { return m_config.driver.faultAlarms; }

    /// Idle release: 0 = hold for ever, else release the coils (SoftHiZ) after this many ms
    /// stopped; the next motion command re-energises them.
    uint16_t getIdleReleaseMs() const { return m_config.idleReleaseMs; }
    void     setIdleReleaseMs(uint16_t ms) { m_config.idleReleaseMs = ms; }
    uint8_t  getFaultAction() const { return m_config.faultAction; }
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
    uint32_t getMicrostepsPerRev() const { return static_cast<uint32_t>(getFullStepsPerRev()) * getEffectiveMicrosteps(); }
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
    void setFaultAction(uint8_t action);
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

    bool saveToFlash();
    bool loadFromFlash();
    bool factoryReset();
    /// Apply defaults without saving (for first boot)
    void applyDefaults();
    /// The blob's CRC (exposed so a host test can forge an old blob)
    uint32_t blobCrc(const uint8_t* data, size_t len) { return calculateCRC(data, len); }
    /// True when the last load migrated a V1-V4 blob (the driver translated its chip fields)
    bool migratedLegacy() const { return m_migrated; }

private:
    MotorConfig m_config;
    bool m_valid = false;
    bool m_migrated = false;

    uint32_t calculateCRC(const uint8_t* data, size_t len);
    bool validateV3(const MotorConfigV3& cfg);
    bool validateRecord(const uint8_t* rec, uint16_t maxVersion);
    void migrateV4(const MotorConfigV4& old);

    Drivers::IConfigStore* m_store = nullptr;
    Drivers::IMotorDriver* m_driver = nullptr;   ///< caps and chip-unit translation (pure calls)
    EraseGate  m_eraseGate = nullptr;
    SaveStatus m_lastSave = SaveStatus::Ok;

    /// Compact at boot when fewer than this many saves are left, so a run-time erase is rare.
    static constexpr size_t COMPACT_MARGIN_SAVES = 16;

    bool writeRecord();   // one journal append (or overwrite) + readback verify
};

// Global instance
extern MotorConfigManager g_motorConfig;

} // namespace Services
