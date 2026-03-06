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

uint32_t ConfigDispatch::getMicrostepsPerRev() {
    return motion.stepper.config.getMicrostepsPerRev();
}

uint16_t ConfigDispatch::getFullStepsPerRev() {
    return motion.stepper.config.getFullStepsPerRev();
}

bool ConfigDispatch::configSaveToFlash() {
    return motion.configStore.saveToFlash();
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

void ConfigDispatch::configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    motion.stepper.config.setKval(hold, run, acc, dec);
}

void ConfigDispatch::configSetOcdThreshold(uint8_t thresh) {
    motion.stepper.config.setOcdThreshold(thresh);
}

void ConfigDispatch::configSetStallThreshold(uint8_t thresh) {
    motion.stepper.config.setStallThreshold(thresh);
}

void ConfigDispatch::configSetFaultAction(uint8_t action) {
    motion.stepper.config.setFaultAction(action);
}

void ConfigDispatch::configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                                bool uvlo, bool stallA, bool stallB, bool cmdErr) {
    FaultEnableFlags flags = {};
    flags.ocd = ocd;
    flags.thermalSD = thermalSD;
    flags.thermalWarn = thermalWarn;
    flags.uvlo = uvlo;
    flags.stallA = stallA;
    flags.stallB = stallB;
    flags.cmdErr = cmdErr;
    motion.stepper.config.setFaultEnable(flags);
}

void ConfigDispatch::configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
    motion.stepper.config.setMotionParams(acc, dec, maxSpd);
}

void ConfigDispatch::configSetStepMode(uint8_t mode) {
    motion.stepper.config.setStepMode(mode);
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

Harness::IMotorConfigDispatcher::MotorConfigSnapshot ConfigDispatch::getMotorConfig() {
    const auto& cfg = motion.stepper.config.getConfig();
    MotorConfigSnapshot snap = {};
    snap.kvalHold = cfg.kvalHold;
    snap.kvalRun = cfg.kvalRun;
    snap.kvalAcc = cfg.kvalAcc;
    snap.kvalDec = cfg.kvalDec;
    snap.ocdThreshold = cfg.ocdThreshold;
    snap.stallThreshold = cfg.stallThreshold;
    snap.acceleration = cfg.acceleration;
    snap.deceleration = cfg.deceleration;
    snap.maxSpeed = cfg.maxSpeed;
    snap.minSpeed = cfg.minSpeed;
    snap.fsSpeed = cfg.fsSpeed;
    snap.faultOcd = cfg.faultEnable.ocd;
    snap.faultThermalSD = cfg.faultEnable.thermalSD;
    snap.faultThermalWarn = cfg.faultEnable.thermalWarn;
    snap.faultUvlo = cfg.faultEnable.uvlo;
    snap.faultStallA = cfg.faultEnable.stallA;
    snap.faultStallB = cfg.faultEnable.stallB;
    snap.faultCmdErr = cfg.faultEnable.cmdErr;
    snap.faultAction = cfg.faultAction;
    snap.stepMode = cfg.stepMode;
    snap.valid = motion.configStore.isValid();
    return snap;
}

} // namespace Services
