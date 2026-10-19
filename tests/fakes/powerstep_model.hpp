/**
 * @file powerstep_model.hpp
 * @brief The powerSTEP01 driver's pure side for host tests: caps, quantize and the protocol 2
 *        chip-unit translation (L4 powerstep01_settings.hpp), no motion and no bus.
 *
 * What the config manager, the LCD and the alias commands ask of the driver.
 */
#pragma once

#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L4_drivers/devices/powerstep01_settings.hpp"

namespace fakes {

struct PowerStepModel : Drivers::IMotorDriver {
    void init() override {}
    void run(bool, float) override {}
    void move(bool, uint32_t) override {}
    void goTo(int32_t) override {}
    void softStop() override {} void hardStop() override {}
    void softHiZ() override {}  void hardHiZ() override {}
    void goHome() override {}   void goMark() override {} void resetPos() override {}
    void setMark(int32_t) override {}
    void setMaxSpeed(float) override {}
    float getMaxSpeed() override { return 0.0f; }
    void setAcceleration(float) override {}
    void setDeceleration(float) override {}
    const Drivers::DriverCaps& caps() const override { return Drivers::PowerStepSettings::caps(); }
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override { return Drivers::PowerStepSettings::quantize(s); }
    Drivers::ApplyResult apply(const Drivers::DriverSettings&) override { return Drivers::ApplyResult::APPLIED; }
    bool readSettings(Drivers::DriverSettings&) override { return false; }
    bool toLegacyUnits(const Drivers::DriverSettings& s, Drivers::LegacyDriverUnits& u) const override { Drivers::PowerStepSettings::toLegacy(s, u); return true; }
    bool fromLegacyUnits(const Drivers::LegacyDriverUnits& u, Drivers::DriverSettings& s) const override { Drivers::PowerStepSettings::fromLegacy(u, s); return true; }
    Drivers::MotorStatus getStatus() override { return Drivers::MotorStatus{}; }
    Drivers::MotorStatus readState() override { return Drivers::MotorStatus{}; }
    bool isBusy() override { return false; }
    bool flagActive() override { return false; }
};

} // namespace fakes
