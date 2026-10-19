/**
 * @file config_dispatch.cpp
 * @brief L3 IMotorConfigDispatcher implementation
 */

#include "config_dispatch.hpp"
#include "L3_services/config/config_service/config_service.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

static Harness::ServiceStatus mapConfigResult(Config::Result r) {
    switch (r) {
        case Config::Result::OK:            return Harness::ServiceStatus::Ok;
        case Config::Result::QUEUE_FULL:    return Harness::ServiceStatus::QueueFull;
        case Config::Result::INVALID_PARAM: return Harness::ServiceStatus::InvalidParam;
        default:                            return Harness::ServiceStatus::InvalidParam;
    }
}

Harness::ServiceStatus ConfigDispatch::configSetAccelPhysical(uint32_t stepsPerS2) {
    return mapConfigResult(Config::setAccelPhysical(stepsPerS2));
}

Harness::ServiceStatus ConfigDispatch::configSetDecelPhysical(uint32_t stepsPerS2) {
    return mapConfigResult(Config::setDecelPhysical(stepsPerS2));
}

Harness::ServiceStatus ConfigDispatch::configSetMaxSpeedPhysical(uint32_t stepsPerS) {
    return mapConfigResult(Config::setMaxSpeedPhysical(stepsPerS));
}

Harness::DriverCaps ConfigDispatch::driverCaps() {
    return motion.stepper.config.caps();
}

uint32_t ConfigDispatch::getMicrostepsPerRev() {
    return motion.stepper.config.getMicrostepsPerRev();
}

uint16_t ConfigDispatch::getFullStepsPerRev() {
    return motion.stepper.config.getFullStepsPerRev();
}

// --- Physical settings (protocol 3) ---

Harness::DriverSettings ConfigDispatch::driverSettings() {
    return motion.stepper.config.getDriverSettings();
}

Harness::DriverSettings ConfigDispatch::driverSettingsInForce() {
    return motion.stepper.config.getEffectiveSettings();
}

namespace {
/// Every value the edit changed is one the driver can take (unchanged ones are not judged:
/// a migrated or default field the caps would refuse does not block an unrelated edit)
bool editWithinCaps(const Harness::DriverCaps& c, const Harness::DriverSettings& o,
                    const Harness::DriverSettings& s)
{
    using namespace Harness;
    if (s.mode != o.mode && !c.hasMode(s.mode)) return false;
    if (s.microsteps != o.microsteps && !microstepsWithinCaps(c, s.microsteps)) return false;
    const float oc[4] = { o.currentA.hold, o.currentA.run, o.currentA.acc, o.currentA.dec };
    const float sc[4] = { s.currentA.hold, s.currentA.run, s.currentA.acc, s.currentA.dec };
    const float od[4] = { o.duty.hold, o.duty.run, o.duty.acc, o.duty.dec };
    const float sd[4] = { s.duty.hold, s.duty.run, s.duty.acc, s.duty.dec };
    for (unsigned i = 0; i < 4; ++i) {
        if (sc[i] != oc[i] && !currentWithinCaps(c, sc[i])) return false;
        if (sd[i] != od[i] && !dutyWithinCaps(c, sd[i])) return false;
    }
    if (s.accelFsps2 != o.accelFsps2 && !accelWithinCaps(c, s.accelFsps2)) return false;
    if (s.decelFsps2 != o.decelFsps2 && !accelWithinCaps(c, s.decelFsps2)) return false;
    if (s.maxSpeedFsps != o.maxSpeedFsps && !speedLimitWithinCaps(c, s.maxSpeedFsps)) return false;
    if (s.overcurrentA != o.overcurrentA && !overcurrentWithinCaps(c, s.overcurrentA)) return false;
    if (s.stallThreshold01 != o.stallThreshold01 && !stallWithinCaps(c, s.stallThreshold01)) return false;
    if ((s.faultAlarms & ~c.faultsAlarmable) != 0u && s.faultAlarms != o.faultAlarms) return false;
    return true;
}
} // namespace

Harness::ServiceStatus ConfigDispatch::configSetDriverSettings(const Harness::DriverSettings& s) {
    auto& sc = motion.stepper.config;
    if (!editWithinCaps(sc.caps(), sc.getDriverSettings(), s)) {
        return Harness::ServiceStatus::OutOfRange;
    }
    sc.setDriverSettings(s);
    return Harness::ServiceStatus::Ok;
}

Harness::ServiceStatus ConfigDispatch::configSaveToFlash() {
    if (motion.configStore.saveToFlash()) {
        return Harness::ServiceStatus::Ok;
    }
    return (motion.configStore.lastSaveStatus() == MotorConfigManager::SaveStatus::StoreFull)
         ? Harness::ServiceStatus::StoreFull : Harness::ServiceStatus::FlashError;
}

bool ConfigDispatch::configLoadFromFlash() {
    return motion.configStore.loadFromFlash();
}

bool ConfigDispatch::configFactoryReset() {
    return motion.configStore.factoryReset();
}

bool ConfigDispatch::configIsValid() {
    return motion.configStore.isValid();
}

// --- Protocol 2 chip units: the driver translates (pure calls), the settings stay physical ---

namespace {
/// Read the settings as chip units, change some, write them back through the driver
template <class Edit>
void editLegacy(Edit edit)
{
    Drivers::LegacyDriverUnits u = {};
    if (!motion.stepper.config.toLegacyUnits(u)) {
        return;                       // a driver without protocol 2 units (L2 refuses the alias first)
    }
    edit(u);
    (void)motion.stepper.config.setFromLegacyUnits(u);
}
} // namespace

bool ConfigDispatch::configHasChipUnits() {
    Drivers::LegacyDriverUnits u = {};
    return motion.stepper.config.toLegacyUnits(u);
}

void ConfigDispatch::configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) {
        u.kval[0] = hold; u.kval[1] = run; u.kval[2] = acc; u.kval[3] = dec;
    });
}

void ConfigDispatch::configSetDriveMode(uint8_t mode) {
    motion.stepper.config.setDriveMode(mode ? Harness::DriveMode::CURRENT : Harness::DriveMode::VOLTAGE);
}

void ConfigDispatch::configSetTval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) {
        u.tval[0] = hold; u.tval[1] = run; u.tval[2] = acc; u.tval[3] = dec;
    });
}

void ConfigDispatch::configSetCmRegulation(bool predEn, uint8_t tsw, uint8_t tFast,
                                           uint8_t tonMin, uint8_t toffMin) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) {
        u.predEn = predEn ? 1 : 0; u.tsw = tsw; u.tFast = tFast; u.tonMin = tonMin; u.toffMin = toffMin;
    });
}

void ConfigDispatch::configSetIdleRelease(uint16_t ms) {
    motion.stepper.config.setIdleReleaseMs(ms);
}

void ConfigDispatch::configSetOcdThreshold(uint8_t thresh) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) { u.ocdTh = thresh; });
}

void ConfigDispatch::configSetStallThreshold(uint8_t thresh) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) { u.stallTh = thresh; });
}

void ConfigDispatch::configSetFaultAction(uint8_t action) {
    motion.stepper.config.setFaultAction(action);
}

void ConfigDispatch::configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                                bool uvlo, bool stallA, bool stallB, bool cmdErr) {
    const uint8_t alarms = static_cast<uint8_t>((ocd ? 1u : 0u) | (thermalSD ? 2u : 0u) | (thermalWarn ? 4u : 0u)
                         | (uvlo ? 8u : 0u) | (stallA ? 16u : 0u) | (stallB ? 32u : 0u) | (cmdErr ? 64u : 0u));
    Drivers::LegacyDriverUnits probe = {};
    if (motion.stepper.config.toLegacyUnits(probe)) {
        editLegacy([&](Drivers::LegacyDriverUnits& u) { u.alarms = alarms; });   // bridge A/B kept apart
        return;
    }
    // Chip-neutral alarms (a driver without protocol 2 units): one stall flag
    Harness::DriverSettings s = motion.stepper.config.getDriverSettings();
    uint8_t f = 0;
    if (ocd)              f |= Harness::DriverFault::OVERCURRENT;
    if (thermalSD)        f |= Harness::DriverFault::THERMAL_SHUTDOWN;
    if (thermalWarn)      f |= Harness::DriverFault::THERMAL_WARNING;
    if (uvlo)             f |= Harness::DriverFault::UNDERVOLTAGE;
    if (stallA || stallB) f |= Harness::DriverFault::STALL;
    if (cmdErr)           f |= Harness::DriverFault::COMMAND_ERROR;
    s.faultAlarms = f;
    motion.stepper.config.setDriverSettings(s);
}

void ConfigDispatch::configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
    editLegacy([&](Drivers::LegacyDriverUnits& u) { u.acc = acc; u.dec = dec; u.maxSpeed = maxSpd; });
}

void ConfigDispatch::configSetStepMode(uint8_t mode) {
    motion.stepper.config.setMicrosteps(static_cast<uint16_t>(1u << (mode > 15 ? 15 : mode)));
}

void ConfigDispatch::configSetFullStepsPerRev(uint16_t steps) {
    motion.stepper.config.setFullStepsPerRev(steps);
}

void ConfigDispatch::configSetEncoderPPR(uint16_t ppr) {
    motion.encoder.config.setEncoderPPR(ppr);
}

uint16_t ConfigDispatch::configGetEncoderPPR() {
    return motion.encoder.config.getEncoderPPR();
}

Services::IMotorConfigDispatcher::MotorConfigSnapshot ConfigDispatch::getMotorConfig() {
    MotorConfigSnapshot snap = {};
    Drivers::LegacyDriverUnits u = {};
    if (motion.stepper.config.toLegacyUnits(u)) {
        snap.kvalHold = u.kval[0]; snap.kvalRun = u.kval[1]; snap.kvalAcc = u.kval[2]; snap.kvalDec = u.kval[3];
        snap.tvalHold = u.tval[0]; snap.tvalRun = u.tval[1]; snap.tvalAcc = u.tval[2]; snap.tvalDec = u.tval[3];
        snap.cmPredEn = u.predEn;
        snap.cmTsw = u.tsw;
        snap.cmTFast = u.tFast;
        snap.cmTonMin = u.tonMin;
        snap.cmToffMin = u.toffMin;
        snap.ocdThreshold = u.ocdTh;
        snap.stallThreshold = u.stallTh;
        snap.acceleration = u.acc;
        snap.deceleration = u.dec;
        snap.maxSpeed = u.maxSpeed;
        snap.minSpeed = u.minSpeed;
        snap.fsSpeed = u.fsSpd;
        snap.faultOcd = (u.alarms & 1u) != 0;
        snap.faultThermalSD = (u.alarms & 2u) != 0;
        snap.faultThermalWarn = (u.alarms & 4u) != 0;
        snap.faultUvlo = (u.alarms & 8u) != 0;
        snap.faultStallA = (u.alarms & 16u) != 0;
        snap.faultStallB = (u.alarms & 32u) != 0;
        snap.faultCmdErr = (u.alarms & 64u) != 0;
    }
    snap.driveMode = motion.stepper.config.getDriveMode();
    snap.stepMode = Harness::microstepsLog2(motion.stepper.config.getMicrosteps());
    snap.idleReleaseMs = motion.stepper.config.getIdleReleaseMs();
    snap.faultAction = motion.stepper.config.getFaultAction();
    snap.valid = motion.configStore.isValid();
    return snap;
}

} // namespace Services
