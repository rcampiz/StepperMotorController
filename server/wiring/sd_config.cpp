/**
 * @file sd_config.cpp
 * @brief ServiceDispatcher — IMotorConfigDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "L3_services/config/config_service.hpp"
#include "L3_services/motion/motor_config.hpp"

static Comms::ServiceStatus mapConfigResult(Services::Config::Result r) {
    switch (r) {
        case Services::Config::Result::OK:            return Comms::ServiceStatus::Ok;
        case Services::Config::Result::QUEUE_FULL:    return Comms::ServiceStatus::QueueFull;
        case Services::Config::Result::INVALID_PARAM: return Comms::ServiceStatus::InvalidParam;
        default:                                      return Comms::ServiceStatus::InvalidParam;
    }
}

Comms::ServiceStatus ServiceDispatcher::configSetAccelPhysical(uint32_t stepsPerS2) {
    return mapConfigResult(Services::Config::setAccelPhysical(stepsPerS2));
}

Comms::ServiceStatus ServiceDispatcher::configSetDecelPhysical(uint32_t stepsPerS2) {
    return mapConfigResult(Services::Config::setDecelPhysical(stepsPerS2));
}

Comms::ServiceStatus ServiceDispatcher::configSetMaxSpeedPhysical(uint32_t stepsPerS) {
    return mapConfigResult(Services::Config::setMaxSpeedPhysical(stepsPerS));
}

uint32_t ServiceDispatcher::getMicrostepsPerRev() {
    return Services::g_motorConfig.getMicrostepsPerRev();
}

uint16_t ServiceDispatcher::getFullStepsPerRev() {
    return Services::g_motorConfig.getFullStepsPerRev();
}

bool ServiceDispatcher::configSaveToFlash() {
    return Services::g_motorConfig.saveToFlash();
}

bool ServiceDispatcher::configLoadFromFlash() {
    return Services::g_motorConfig.loadFromFlash();
}

bool ServiceDispatcher::configFactoryReset() {
    return Services::g_motorConfig.factoryReset();
}

bool ServiceDispatcher::configIsValid() {
    return Services::g_motorConfig.isValid();
}

void ServiceDispatcher::configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) {
    Services::g_motorConfig.setKval(hold, run, acc, dec);
}

void ServiceDispatcher::configSetOcdThreshold(uint8_t thresh) {
    Services::g_motorConfig.setOcdThreshold(thresh);
}

void ServiceDispatcher::configSetStallThreshold(uint8_t thresh) {
    Services::g_motorConfig.setStallThreshold(thresh);
}

void ServiceDispatcher::configSetFaultAction(uint8_t action) {
    Services::g_motorConfig.setFaultAction(action);
}

void ServiceDispatcher::configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                                   bool uvlo, bool stallA, bool stallB, bool cmdErr) {
    Services::FaultEnableFlags flags = {};
    flags.ocd = ocd;
    flags.thermalSD = thermalSD;
    flags.thermalWarn = thermalWarn;
    flags.uvlo = uvlo;
    flags.stallA = stallA;
    flags.stallB = stallB;
    flags.cmdErr = cmdErr;
    Services::g_motorConfig.setFaultEnable(flags);
}

void ServiceDispatcher::configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) {
    Services::g_motorConfig.setMotionParams(acc, dec, maxSpd);
}

void ServiceDispatcher::configSetStepMode(uint8_t mode) {
    Services::g_motorConfig.setStepMode(mode);
}

void ServiceDispatcher::configSetFullStepsPerRev(uint16_t steps) {
    Services::g_motorConfig.setFullStepsPerRev(steps);
}

void ServiceDispatcher::configSetEncoderPPR(uint16_t ppr) {
    Services::g_motorConfig.setEncoderPPR(ppr);
}

uint16_t ServiceDispatcher::configGetEncoderPPR() {
    return Services::g_motorConfig.getEncoderPPR();
}

Comms::IMotorConfigDispatcher::MotorConfigSnapshot ServiceDispatcher::getMotorConfig() {
    const auto& cfg = Services::g_motorConfig.getConfig();
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
    snap.valid = Services::g_motorConfig.isValid();
    return snap;
}
