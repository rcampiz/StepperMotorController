/**
 * @file config_dispatch.hpp
 * @brief L3 implementation of IMotorConfigDispatcher
 *
 * Routes motor configuration commands to Services::Config and
 * Services::motion.stepper.config / motion.configStore / motion.encoder.config.
 */

#pragma once

#include "harness/pins/imotor_config_dispatcher.hpp"

namespace Services {

class ConfigDispatch : public Harness::IMotorConfigDispatcher {
public:
    Harness::ServiceStatus configSetAccelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetDecelPhysical(uint32_t stepsPerS2) override;
    Harness::ServiceStatus configSetMaxSpeedPhysical(uint32_t stepsPerS) override;
    uint32_t getMicrostepsPerRev() override;
    uint16_t getFullStepsPerRev() override;
    bool configSaveToFlash() override;
    bool configLoadFromFlash() override;
    bool configFactoryReset() override;
    bool configIsValid() override;
    void configSetKval(uint8_t hold, uint8_t run, uint8_t acc, uint8_t dec) override;
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
