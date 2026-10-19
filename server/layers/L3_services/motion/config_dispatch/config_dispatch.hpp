/**
 * @file config_dispatch.hpp
 * @brief L3 implementation of IMotorConfigDispatcher
 *
 * Routes motor configuration commands to Services::Config and
 * Services::motion.stepper.config / motion.configStore / motion.encoder.config.
 */

#pragma once

#include "L3_services/boundary/imotor_config_dispatcher.hpp"

namespace Services {

class ConfigDispatch : public Services::IMotorConfigDispatcher {
public:
    Harness::ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) override;
    Harness::DriverCaps driverCaps() override;
    uint32_t getMicrostepsPerRev() override;
    uint16_t getFullStepsPerRev() override;
    Harness::DriverSettings driverSettings() override;
    Harness::DriverSettings driverSettingsInForce() override;
    Harness::ServiceStatus configSetDriverSettings(const Harness::DriverSettings& s) override;
    Harness::ServiceStatus configSaveToFlash() override;
    bool configLoadFromFlash() override;
    bool configFactoryReset() override;
    bool configIsValid() override;
    bool configHasChipUnits() override;
    void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) override;
    void configSetDriveMode(uint8_t mode) override;
    void configSetTval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) override;
    void configSetCmRegulation(bool predEn, uint8_t tsw, uint8_t tFast, uint8_t tonMin, uint8_t toffMin) override;
    void configSetIdleRelease(uint16_t ms) override;
    void configSetOcdThreshold(uint8_t thresh) override;
    void configSetStallThreshold(uint8_t thresh) override;
    void configSetFaultAction(uint8_t action) override;
    void configSetFaultEnableFlags(bool ocd, bool thermalSD, bool thermalWarn,
                                    bool uvlo, bool stallA, bool stallB, bool cmdErr) override;
    void configSetMotionParams(uint16_t acc, uint16_t dec, uint16_t maxSpd) override;
    void configSetStepMode(uint8_t mode) override;
    void configSetFullStepsPerRev(uint16_t steps) override;
    void configSetEncoderPPR(uint16_t ppr) override;
    uint16_t configGetEncoderPPR() override;
    MotorConfigSnapshot getMotorConfig() override;
};

} // namespace Services
