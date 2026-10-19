/**
 * @file powerstep01_settings.hpp
 * @brief powerSTEP01 settings engine: physical DriverSettings <-> registers, capabilities,
 *        the Hi-Z-only writes, the chip-reset watch, diagnostics, protocol 2 chip units.
 *
 * L4 only (docs/design/DRIVER_BOUNDARY.md, step 3). Shared by the driver (powerstep01.hpp),
 * the virtual Nucleo's fake chip and the host tests' register fakes: every function that
 * touches the chip is a template over a register port `Io` offering
 *     uint32_t readReg(uint8_t reg);  void writeReg(uint8_t reg, uint32_t value);
 *     bool outputsOff();              // a GetStatus: HiZ flag (also clears latched faults)
 *     void clearFaults();             // a GetStatus
 *
 * Units and the chip/board facts behind them:
 *   TVAL (current mode)   7.8 mV/LSB on the IHM03A1's 0.1 ohm sense: 0.078 A per code
 *                         (the project's convention since V3: code x 78 mA), 0..0x7F
 *   KVAL (voltage mode)   duty = code / 256, 0..0xFF
 *   ACC / DEC             14.552 full steps/s^2 per code (2^-40 step/tick^2), 12 bits
 *   MAX_SPEED             powerstep01_units.hpp (15.2588 full steps/s per code)
 *   OCD_TH                a MOSFET drain-source VOLTAGE: (code+1) x 31.25 mV (31.25 mV..1 V).
 *                         As a current it depends on the board's RDS(on): the IHM03A1's
 *                         STL220N6F7 is ~1.2 mOhm (ESTIMATE, typical at 10 V gate), so a code
 *                         is ~26 A and even code 0 is ~26 A: OCD only catches hard shorts.
 *   STALL_TH              5 bits; on DriverSettings' 0..1 scale code / 31
 *   STEP_MODE             STEP_SEL 0..7 = 1/2^n; CM_VM (bit 3) current mode, which runs 1/16
 *                         at most; SYNC_EN|SYNC_SEL = 0xC0 (8 pulses per full step on
 *                         BUSY/SYNC for the TIM3 position capture). Writable only in Hi-Z.
 *   CONFIG[15:10]         current mode: PRED_EN, TSW. Writable only in Hi-Z.
 *
 * Conversions to codes round to the nearest code (levels, thresholds) or truncate (speed
 * ceiling, accelerations: whole steps/s^2 give floor(n x 1000 / 14552), what the integer
 * path always did), and code -> physical -> code is exact for every code (host-tested).
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L4_drivers/devices/powerstep01_regs.hpp"
#include "L4_drivers/devices/powerstep01_units.hpp"

namespace Drivers {
namespace PowerStepSettings {

// --- Chip and board constants ----------------------------------------------------------
constexpr float    TVAL_A_PER_CODE   = 0.078f;
constexpr uint8_t  TVAL_CODE_MAX     = 0x7F;
constexpr float    KVAL_DUTY_PER_CODE = 1.0f / 256.0f;
constexpr uint8_t  KVAL_CODE_MAX     = 0xFF;
constexpr uint32_t ACC_MILLI_PER_CODE = 14552u;      ///< 14.552 full steps/s^2 per ACC/DEC code
constexpr float    ACC_FSPS2_PER_CODE = 14.552f;
constexpr uint16_t ACC_CODE_MAX      = 0x0FFF;
constexpr float    OCD_V_PER_STEP    = 0.03125f;     ///< OCD_TH: (code + 1) x 31.25 mV of VDS
constexpr float    BOARD_RDS_ON_OHM  = 0.0012f;      ///< ESTIMATE: STL220N6F7 typ. RDS(on), IHM03A1
constexpr float    OCD_A_PER_STEP    = OCD_V_PER_STEP / BOARD_RDS_ON_OHM;   ///< ~26.04 A (estimate)
constexpr uint8_t  OCD_CODE_MAX      = 0x1F;
constexpr uint8_t  STALL_CODE_MAX    = 0x1F;
constexpr uint8_t  STEP_SEL_MAX_VOLTAGE = 7;         ///< 1/128
constexpr uint8_t  STEP_SEL_MAX_CURRENT = 4;         ///< 1/16: current mode runs 1/16 at most, whatever
                                                     ///< STEP_SEL says (bench 2026-10-18: 1/128 asked,
                                                     ///< a 1-rev jog turned 8 revs)
constexpr uint32_t STEP_MODE_SYNC_8FS = 0xC0u;       ///< SYNC_EN + SYNC_SEL: 8 pulses per full step
constexpr uint32_t STEP_MODE_CM_VM   = 0x08u;
constexpr uint32_t CONFIG_CM_MASK    = 0xFC00u;      ///< PRED_EN (15) + TSW (14:10)
constexpr uint8_t  ALARM_UVLO_ADC    = 1u << 4;      ///< never armed: the IHM03A1's ADCIN floats

// Current-regulation defaults (the chip's own, PRED_EN on: bench 2026-10-18, less ripple)
constexpr uint8_t  CM_DEFAULT_TSW    = 11;           ///< 48 us
constexpr uint8_t  CM_DEFAULT_TFAST  = 0x19;
constexpr uint8_t  CM_DEFAULT_TMIN   = 0x29;         ///< 21 us
constexpr uint16_t MIN_SPEED_DEFAULT = 0;
constexpr uint16_t FS_SPD_DEFAULT    = 0x3FF;        ///< never switch to full step

constexpr uint8_t  TUNING_TAG        = 0x50;         ///< DriverTuning owned by this driver ('P')

// --- The tuning block (DriverTuning::data) ---------------------------------------------
struct Tuning {
    uint8_t  predEn = 0;        ///< 0 default (on), 1 off, 2 on
    uint8_t  tswPlus1 = 0;      ///< TSW + 1; 0 default
    uint8_t  tFast = 0;         ///< 0 default
    uint8_t  tonMin = 0;        ///< 0 default
    uint8_t  toffMin = 0;       ///< 0 default
    uint16_t minSpeed = MIN_SPEED_DEFAULT;
    uint16_t fsSpd = FS_SPD_DEFAULT;
    uint8_t  stallBridges = 0;  ///< which bridges' stall raises the alarm: bit 0 A, bit 1 B; 0 both

    bool    pred() const    { return predEn != 1; }
    uint8_t tsw() const     { return tswPlus1 != 0 ? static_cast<uint8_t>((tswPlus1 - 1u) & 0x1Fu) : CM_DEFAULT_TSW; }
    uint8_t tFastR() const  { return tFast != 0 ? tFast : CM_DEFAULT_TFAST; }
    uint8_t tonMinR() const { return tonMin != 0 ? tonMin : CM_DEFAULT_TMIN; }
    uint8_t toffMinR() const { return toffMin != 0 ? toffMin : CM_DEFAULT_TMIN; }
};

inline Tuning unpack(const Harness::DriverTuning& t)
{
    Tuning u;
    if (t.tag != TUNING_TAG) return u;                      // another chip's block, or defaults
    u.predEn = t.data[0];
    u.tswPlus1 = t.data[1];
    u.tFast = t.data[2];
    u.tonMin = t.data[3];
    u.toffMin = t.data[4];
    u.minSpeed = static_cast<uint16_t>(t.data[5] | (t.data[6] << 8));
    u.fsSpd = static_cast<uint16_t>(t.data[7] | (t.data[8] << 8));
    u.stallBridges = t.data[9];
    return u;
}

inline Harness::DriverTuning pack(const Tuning& u)
{
    Harness::DriverTuning t = {};
    t.tag = TUNING_TAG;
    t.data[0] = u.predEn;
    t.data[1] = u.tswPlus1;
    t.data[2] = u.tFast;
    t.data[3] = u.tonMin;
    t.data[4] = u.toffMin;
    t.data[5] = static_cast<uint8_t>(u.minSpeed & 0xFFu);
    t.data[6] = static_cast<uint8_t>(u.minSpeed >> 8);
    t.data[7] = static_cast<uint8_t>(u.fsSpd & 0xFFu);
    t.data[8] = static_cast<uint8_t>(u.fsSpd >> 8);
    t.data[9] = u.stallBridges;
    return t;
}

// --- Capabilities ----------------------------------------------------------------------
inline const Harness::DriverCaps& caps()
{
    static const Harness::DriverCaps c = {
        /*voltageControl*/ true, /*currentControl*/ true, /*currentSettable*/ true,
        /*maxMicrostepsVoltage*/ 128, /*maxMicrostepsCurrent*/ 16,
        /*stallDetect*/ true, /*standbyPin*/ true, /*hardwarePositioning*/ true,
        /*syncPulsesPerFullStep*/ 8,
        /*faultsReported*/ 0x3F, /*faultsAlarmable*/ 0x3F,
        /*maxCurrentA*/ TVAL_CODE_MAX * TVAL_A_PER_CODE, /*currentStepA*/ TVAL_A_PER_CODE,
        /*dutyStep*/ KVAL_DUTY_PER_CODE,
        /*overcurrentMinA*/ OCD_A_PER_STEP, /*overcurrentMaxA*/ (OCD_CODE_MAX + 1) * OCD_A_PER_STEP,
        /*overcurrentStepA*/ OCD_A_PER_STEP, /*overcurrentOhms*/ BOARD_RDS_ON_OHM,
        /*stallStep01*/ 1.0f / STALL_CODE_MAX,
        /*maxSpeedFsps*/ 15625.0f,
        /*maxSpeedLimitFsps*/ PowerStepUnits::MAX_SPEED_REG_MAX * (15625.0f / 1024.0f),
        /*speedLimitStepFsps*/ 15625.0f / 1024.0f,
        /*accelMinFsps2*/ ACC_FSPS2_PER_CODE,
        /*accelMaxFsps2*/ ACC_CODE_MAX * ACC_FSPS2_PER_CODE,
        /*accelStepFsps2*/ ACC_FSPS2_PER_CODE,
        /*minPositionUsteps*/ -2097152, /*maxPositionUsteps*/ 2097151,   // ABS_POS: 22 bits
    };
    return c;
}

// --- Field conversions -----------------------------------------------------------------
inline uint8_t ampsToTval(float a)
{
    if (!(a > 0.0f)) return 0;
    const float c = a / TVAL_A_PER_CODE + 0.5f;
    return c >= static_cast<float>(TVAL_CODE_MAX) ? TVAL_CODE_MAX : static_cast<uint8_t>(c);
}
inline float tvalToAmps(uint32_t code) { return static_cast<float>(code & TVAL_CODE_MAX) * TVAL_A_PER_CODE; }

inline uint8_t dutyToKval(float d)
{
    if (!(d > 0.0f)) return 0;
    const float c = d * 256.0f + 0.5f;
    return c >= static_cast<float>(KVAL_CODE_MAX) ? KVAL_CODE_MAX : static_cast<uint8_t>(c);
}
inline float kvalToDuty(uint32_t code) { return static_cast<float>(code & 0xFFu) * KVAL_DUTY_PER_CODE; }

/// Full steps/s^2 -> ACC/DEC code: floor(thousandths / 14552), a positive request at least 1.
/// Whole numbers are exact (n x 1000 / 14552, the old integer path: the remainder of n x 1000
/// is a multiple of 8, at most 14544, so the +7 that absorbs a stored float's error never
/// carries); a value read back from a code (code x 14.552) returns that code.
inline uint16_t fsps2ToAcc(float f)
{
    if (!(f > 0.0f)) return 0;
    if (f >= 60000.0f) return ACC_CODE_MAX;
    const uint32_t whole = static_cast<uint32_t>(f);
    const uint32_t milli = whole * 1000u + static_cast<uint32_t>((f - static_cast<float>(whole)) * 1000.0f + 0.5f);
    uint32_t code = (milli + 7u) / ACC_MILLI_PER_CODE;
    if (code < 1u) code = 1u;
    return code > ACC_CODE_MAX ? ACC_CODE_MAX : static_cast<uint16_t>(code);
}
inline float accToFsps2(uint32_t code) { return static_cast<float>(code & ACC_CODE_MAX) * ACC_FSPS2_PER_CODE; }

/// Estimated amps -> OCD_TH code (nearest; the threshold is VDS, see the header)
inline uint8_t ampsToOcd(float a)
{
    if (!(a > 0.0f)) return 0;
    const int c = static_cast<int>(a / OCD_A_PER_STEP + 0.5f) - 1;
    if (c < 0) return 0;
    return c > OCD_CODE_MAX ? OCD_CODE_MAX : static_cast<uint8_t>(c);
}
inline float ocdToAmps(uint32_t code) { return static_cast<float>((code & OCD_CODE_MAX) + 1u) * OCD_A_PER_STEP; }

inline uint8_t stallToCode(float x)
{
    if (!(x > 0.0f)) return 0;
    const float c = x * STALL_CODE_MAX + 0.5f;
    return c >= static_cast<float>(STALL_CODE_MAX) ? STALL_CODE_MAX : static_cast<uint8_t>(c);
}
inline float codeToStall(uint32_t code) { return static_cast<float>(code & STALL_CODE_MAX) / STALL_CODE_MAX; }

inline bool currentMode(const Harness::DriverSettings& s) { return s.mode == Harness::DriveMode::CURRENT; }

/// Hold / run / acc / dec as codes: TVAL (current) or KVAL (voltage)
inline void levelCodes(const Harness::DriveLevels& l, bool current, uint8_t out[4])
{
    const float v[4] = { l.hold, l.run, l.acc, l.dec };
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = current ? ampsToTval(v[i]) : dutyToKval(v[i]);
    }
}

inline Harness::DriveLevels levelsFromCodes(const uint8_t code[4], bool current)
{
    float v[4];
    for (unsigned i = 0; i < 4; ++i) {
        v[i] = current ? tvalToAmps(code[i]) : kvalToDuty(code[i]);
    }
    return Harness::DriveLevels{ v[0], v[1], v[2], v[3] };
}

/// STEP_SEL in force for the settings (the current-mode cap applied)
inline uint8_t stepSel(const Harness::DriverSettings& s)
{
    return Harness::microstepsLog2(Harness::effectiveMicrosteps(caps(), s.microsteps, s.mode));
}

/// DriverFault alarm bits (+ the stall bridges in tuning) -> ALARM_EN
inline uint8_t alarmEn(uint8_t faults, uint8_t stallBridges)
{
    uint8_t a = 0;
    if (faults & Harness::DriverFault::OVERCURRENT)      a |= 1u << 0;
    if (faults & Harness::DriverFault::THERMAL_SHUTDOWN) a |= 1u << 1;
    if (faults & Harness::DriverFault::THERMAL_WARNING)  a |= 1u << 2;
    if (faults & Harness::DriverFault::UNDERVOLTAGE)     a |= 1u << 3;
    if (faults & Harness::DriverFault::STALL) {
        if (stallBridges == 0 || (stallBridges & 1u)) a |= 1u << 5;
        if (stallBridges == 0 || (stallBridges & 2u)) a |= 1u << 6;
    }
    if (faults & Harness::DriverFault::COMMAND_ERROR)    a |= 1u << 7;
    return a;                                             // bit 4 UVLO_ADC never
}

inline uint8_t alarmFaults(uint32_t alarmEnReg)
{
    uint8_t f = 0;
    if (alarmEnReg & (1u << 0)) f |= Harness::DriverFault::OVERCURRENT;
    if (alarmEnReg & (1u << 1)) f |= Harness::DriverFault::THERMAL_SHUTDOWN;
    if (alarmEnReg & (1u << 2)) f |= Harness::DriverFault::THERMAL_WARNING;
    if (alarmEnReg & (1u << 3)) f |= Harness::DriverFault::UNDERVOLTAGE;
    if (alarmEnReg & (3u << 5)) f |= Harness::DriverFault::STALL;
    if (alarmEnReg & (1u << 7)) f |= Harness::DriverFault::COMMAND_ERROR;
    return f;
}

// --- The register image of a DriverSettings --------------------------------------------
struct RegisterImage {
    bool     currentMode;
    uint8_t  level[4];          ///< KVAL_* (voltage) or TVAL_* (current), hold/run/acc/dec
    uint8_t  tFast, tonMin, toffMin;
    uint8_t  ocdTh, stallTh;
    uint16_t acc, dec, maxSpeed, minSpeed, fsSpd;
    uint8_t  alarmEn;
    uint8_t  stepMode;          ///< the whole register: SYNC | CM_VM | STEP_SEL
    uint16_t configHi;          ///< CONFIG[15:10] (current mode only)
};

inline void toLegacy(const Harness::DriverSettings& s, LegacyDriverUnits& u);
inline void fromLegacy(const LegacyDriverUnits& u, Harness::DriverSettings& s);

/// The registers for the settings: the chip codes (shared with the protocol 2 translation),
/// the resolution capped for the drive mode, the alarms, the Hi-Z-only bits
inline RegisterImage encode(const Harness::DriverSettings& s)
{
    LegacyDriverUnits u;
    toLegacy(s, u);
    RegisterImage r = {};
    const Tuning t = unpack(s.tuning);
    r.currentMode = currentMode(s);
    for (unsigned i = 0; i < 4; ++i) r.level[i] = r.currentMode ? u.tval[i] : u.kval[i];
    r.tFast = u.tFast;
    r.tonMin = u.tonMin & 0x7Fu;
    r.toffMin = u.toffMin & 0x7Fu;
    r.ocdTh = u.ocdTh;
    r.stallTh = u.stallTh;
    r.acc = u.acc;
    r.dec = u.dec;
    r.maxSpeed = u.maxSpeed;
    r.minSpeed = u.minSpeed;
    r.fsSpd = u.fsSpd;
    r.alarmEn = alarmEn(s.faultAlarms, t.stallBridges);
    r.stepMode = static_cast<uint8_t>(STEP_MODE_SYNC_8FS | (r.currentMode ? STEP_MODE_CM_VM : 0u) | stepSel(s));
    r.configHi = r.currentMode
        ? static_cast<uint16_t>((u.predEn ? 0x8000u : 0u) | (static_cast<uint32_t>(u.tsw & 0x1Fu) << 10))
        : 0u;
    return r;
}

/// What apply() would put in force (pure): every value through its code and back, the
/// resolution capped for the drive mode
inline Harness::DriverSettings quantize(const Harness::DriverSettings& s)
{
    LegacyDriverUnits u;
    toLegacy(s, u);
    Harness::DriverSettings q = s;
    fromLegacy(u, q);
    q.microsteps = Harness::effectiveMicrosteps(caps(), s.microsteps, q.mode);
    q.tuning = s.tuning;
    q.faultAlarms = static_cast<uint8_t>(s.faultAlarms & caps().faultsAlarmable);
    return q;
}

// --- Apply, read back, diagnostics -----------------------------------------------------
/// Write the settings. Every register the chip takes at any time is written; STEP_MODE and
/// (current mode) CONFIG[15:10] only take effect with the bridges in Hi-Z (a write with the
/// bridges on is silently ignored: bench 2026-10-17), so they are written, read-modify-write,
/// and verified only when they differ and the bridges are in Hi-Z.
template <class Io>
Harness::ApplyResult apply(Io& io, const Harness::DriverSettings& s)
{
    using namespace PowerStepReg;
    const RegisterImage r = encode(s);
    io.writeReg(KVAL_HOLD, r.level[0]);
    io.writeReg(KVAL_RUN,  r.level[1]);
    io.writeReg(KVAL_ACC,  r.level[2]);
    io.writeReg(KVAL_DEC,  r.level[3]);
    if (r.currentMode) {                                   // regulation timing (motor stopped)
        io.writeReg(T_FAST,   r.tFast);
        io.writeReg(TON_MIN,  r.tonMin);
        io.writeReg(TOFF_MIN, r.toffMin);
    }
    io.writeReg(OCD_TH, r.ocdTh);
    io.writeReg(STALL_TH, r.stallTh);
    io.writeReg(ACC, r.acc);
    io.writeReg(DEC, r.dec);
    io.writeReg(MAX_SPEED, r.maxSpeed);
    io.writeReg(MIN_SPEED, r.minSpeed);
    io.writeReg(FS_SPD, r.fsSpd);
    io.writeReg(ALARM_EN, r.alarmEn);

    bool stepOk = (io.readReg(STEP_MODE) & 0xFFu) == r.stepMode;
    bool cfgOk = true;
    if (r.currentMode) {                                   // voltage mode: those bits set the PWM
        cfgOk = (io.readReg(CONFIG) & CONFIG_CM_MASK) == r.configHi;
    }
    bool hiZ = false;
    if (!stepOk || !cfgOk) {
        hiZ = io.outputsOff();
        if (hiZ) {
            if (!stepOk) {
                io.writeReg(STEP_MODE, r.stepMode);
                stepOk = (io.readReg(STEP_MODE) & 0xFFu) == r.stepMode;
            }
            if (!cfgOk) {
                io.writeReg(CONFIG, (io.readReg(CONFIG) & 0x03FFu) | r.configHi);
                cfgOk = (io.readReg(CONFIG) & CONFIG_CM_MASK) == r.configHi;
            }
        }
    }
    io.clearFaults();                                      // latched bits from the write sequence
    if (stepOk && cfgOk) return Harness::ApplyResult::APPLIED;
    return hiZ ? Harness::ApplyResult::VERIFY_FAILED : Harness::ApplyResult::NEEDS_OUTPUTS_OFF;
}

/// The chip's registers decoded (resolution, drive mode, the active levels, ramps, thresholds,
/// alarms; tuning left at defaults)
template <class Io>
Harness::DriverSettings readBack(Io& io)
{
    using namespace PowerStepReg;
    Harness::DriverSettings s = {};
    const uint32_t sm = io.readReg(STEP_MODE);
    const bool cm = (sm & STEP_MODE_CM_VM) != 0u;
    s.mode = cm ? Harness::DriveMode::CURRENT : Harness::DriveMode::VOLTAGE;
    uint8_t sel = static_cast<uint8_t>(sm & 0x07u);
    if (cm && sel > STEP_SEL_MAX_CURRENT) sel = STEP_SEL_MAX_CURRENT;   // what the chip runs
    s.microsteps = static_cast<uint16_t>(1u << sel);
    const uint8_t lv[4] = { static_cast<uint8_t>(io.readReg(KVAL_HOLD)), static_cast<uint8_t>(io.readReg(KVAL_RUN)),
                            static_cast<uint8_t>(io.readReg(KVAL_ACC)), static_cast<uint8_t>(io.readReg(KVAL_DEC)) };
    (cm ? s.currentA : s.duty) = levelsFromCodes(lv, cm);
    s.accelFsps2 = accToFsps2(io.readReg(ACC));
    s.decelFsps2 = accToFsps2(io.readReg(DEC));
    s.maxSpeedFsps = PowerStepUnits::maxSpeedRegToFsps(io.readReg(MAX_SPEED));
    s.overcurrentA = ocdToAmps(io.readReg(OCD_TH));
    s.stallThreshold01 = codeToStall(io.readReg(STALL_TH));
    s.faultAlarms = alarmFaults(io.readReg(ALARM_EN));
    return s;
}

/// DBG:MOTOR's register lines (REGISTERS) or DRV:CFG:APPLY's chip line (SUMMARY)
template <class Io>
size_t diagnostics(Io& io, Harness::DriverDiagView view, char* buf, size_t size)
{
    using namespace PowerStepReg;
    if (buf == nullptr || size == 0) return 0;
    buf[0] = '\0';
    int n = 0;
    if (view == Harness::DriverDiagView::REGISTERS) {
        n = snprintf(buf, size,
                     "KVAL: HOLD=%02X RUN=%02X ACC=%02X DEC=%02X\n"
                     "ACC=%u DEC=%u MAXSPD=%u POS=%ld\n"
                     "ADC_OUT=%u CONFIG=%04X T_FAST=%02X",
                     (unsigned)io.readReg(KVAL_HOLD), (unsigned)io.readReg(KVAL_RUN),
                     (unsigned)io.readReg(KVAL_ACC), (unsigned)io.readReg(KVAL_DEC),
                     (unsigned)io.readReg(ACC), (unsigned)io.readReg(DEC),
                     (unsigned)io.readReg(MAX_SPEED),
                     (long)PowerStepUnits::absPosToUsteps(io.readReg(ABS_POS)),
                     (unsigned)(io.readReg(ADC_OUT) & 0x1Fu), (unsigned)io.readReg(CONFIG),
                     (unsigned)io.readReg(T_FAST));
    } else if (view == Harness::DriverDiagView::SUMMARY) {
        n = snprintf(buf, size, "STEP_MODE=%u OCD_TH=%02X STALL_TH=%02X ALARM_EN=%02X CONFIG=%04X FS_SPD=%03X",
                     (unsigned)(io.readReg(STEP_MODE) & 0x07u), (unsigned)io.readReg(OCD_TH),
                     (unsigned)io.readReg(STALL_TH), (unsigned)io.readReg(ALARM_EN),
                     (unsigned)io.readReg(CONFIG), (unsigned)io.readReg(FS_SPD));
    }
    if (n < 0) return 0;
    return (static_cast<size_t>(n) < size) ? static_cast<size_t>(n) : size - 1;
}

/// The chip-reset watch. GATECFG1 is written only by init(); after a supply drop the chip is
/// back at its reset defaults (gate drive 4 mA, too weak for the IHM03A1 MOSFETs; OC_SD on),
/// so the next enable trips OCD (bench 2026-10-19). Two mismatching reads in a row = reset.
class ResetWatch {
public:
    /// After init(): what init left in GATECFG1 is the signature (0 = no watch, e.g. a fake)
    template <class Io> void arm(Io& io)
    {
        m_signature = static_cast<uint16_t>(io.readReg(PowerStepReg::GATECFG1));
        m_mismatches = 0;
    }
    template <class Io> bool check(Io& io)
    {
        if (m_signature == 0) return false;
        if (static_cast<uint16_t>(io.readReg(PowerStepReg::GATECFG1)) != m_signature) {
            if (++m_mismatches >= 2) {
                m_mismatches = 0;
                return true;
            }
        } else {
            m_mismatches = 0;
        }
        return false;
    }
private:
    uint16_t m_signature = 0;
    uint8_t  m_mismatches = 0;
};

// --- Protocol 2 chip units ----------------------------------------------------------------
inline void toLegacy(const Harness::DriverSettings& s, LegacyDriverUnits& u)
{
    const Tuning t = unpack(s.tuning);
    levelCodes(s.duty, false, u.kval);
    levelCodes(s.currentA, true, u.tval);
    u.driveMode = currentMode(s) ? 1 : 0;
    const uint8_t k = Harness::microstepsLog2(s.microsteps);
    u.stepMode = k > STEP_SEL_MAX_VOLTAGE ? STEP_SEL_MAX_VOLTAGE : k;
    u.ocdTh = ampsToOcd(s.overcurrentA);
    u.stallTh = stallToCode(s.stallThreshold01);
    u.acc = fsps2ToAcc(s.accelFsps2);
    u.dec = fsps2ToAcc(s.decelFsps2);
    u.maxSpeed = static_cast<uint16_t>(PowerStepUnits::fspsToMaxSpeedReg(s.maxSpeedFsps));
    u.minSpeed = t.minSpeed;
    u.fsSpd = t.fsSpd;
    u.predEn = t.pred() ? 1 : 0;
    u.tsw = t.tsw();
    u.tFast = t.tFastR();
    u.tonMin = t.tonMinR();
    u.toffMin = t.toffMinR();
    const bool stall = (s.faultAlarms & Harness::DriverFault::STALL) != 0u;
    uint8_t a = 0;
    if (s.faultAlarms & Harness::DriverFault::OVERCURRENT)      a |= 1u << 0;
    if (s.faultAlarms & Harness::DriverFault::THERMAL_SHUTDOWN) a |= 1u << 1;
    if (s.faultAlarms & Harness::DriverFault::THERMAL_WARNING)  a |= 1u << 2;
    if (s.faultAlarms & Harness::DriverFault::UNDERVOLTAGE)     a |= 1u << 3;
    if (stall && (t.stallBridges == 0 || (t.stallBridges & 1u))) a |= 1u << 4;
    if (stall && (t.stallBridges == 0 || (t.stallBridges & 2u))) a |= 1u << 5;
    if (s.faultAlarms & Harness::DriverFault::COMMAND_ERROR)    a |= 1u << 6;
    u.alarms = a;
}

inline void fromLegacy(const LegacyDriverUnits& u, Harness::DriverSettings& s)
{
    s.duty = levelsFromCodes(u.kval, false);
    s.currentA = levelsFromCodes(u.tval, true);
    s.mode = u.driveMode ? Harness::DriveMode::CURRENT : Harness::DriveMode::VOLTAGE;
    s.microsteps = static_cast<uint16_t>(1u << (u.stepMode > STEP_SEL_MAX_VOLTAGE ? STEP_SEL_MAX_VOLTAGE : u.stepMode));
    s.overcurrentA = ocdToAmps(u.ocdTh);
    s.stallThreshold01 = codeToStall(u.stallTh);       // 5 bits: the chip keeps only STALL_TH[4:0]
    s.accelFsps2 = accToFsps2(u.acc);
    s.decelFsps2 = accToFsps2(u.dec);
    s.maxSpeedFsps = PowerStepUnits::maxSpeedRegToFsps(u.maxSpeed);
    Tuning t;
    t.predEn = u.predEn ? 2 : 1;
    t.tswPlus1 = (u.tsw == 0xFF) ? 0 : static_cast<uint8_t>((u.tsw & 0x1Fu) + 1u);
    t.tFast = u.tFast;
    t.tonMin = static_cast<uint8_t>(u.tonMin & 0x7Fu);
    t.toffMin = static_cast<uint8_t>(u.toffMin & 0x7Fu);
    t.minSpeed = u.minSpeed;
    t.fsSpd = u.fsSpd;
    const bool stallA = (u.alarms & (1u << 4)) != 0u, stallB = (u.alarms & (1u << 5)) != 0u;
    t.stallBridges = static_cast<uint8_t>((stallA ? 1u : 0u) | (stallB ? 2u : 0u));
    s.tuning = pack(t);
    uint8_t f = 0;
    if (u.alarms & (1u << 0)) f |= Harness::DriverFault::OVERCURRENT;
    if (u.alarms & (1u << 1)) f |= Harness::DriverFault::THERMAL_SHUTDOWN;
    if (u.alarms & (1u << 2)) f |= Harness::DriverFault::THERMAL_WARNING;
    if (u.alarms & (1u << 3)) f |= Harness::DriverFault::UNDERVOLTAGE;
    if (stallA || stallB)     f |= Harness::DriverFault::STALL;
    if (u.alarms & (1u << 6)) f |= Harness::DriverFault::COMMAND_ERROR;
    s.faultAlarms = f;
}

} // namespace PowerStepSettings
} // namespace Drivers
