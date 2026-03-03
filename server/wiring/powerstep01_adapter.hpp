/**
 * @file powerstep01_adapter.hpp
 * @brief Foundation adapter: PowerSTEP01 -> IMotorDriver
 *
 * Thin wrapper delegating IMotorDriver calls to PowerSTEP01 concrete driver.
 * Header-only — all methods are trivial one-liners.
 */

#pragma once

#include "F_platform/hal/imotor_driver.hpp"
#include "L4_drivers/devices/powerstep01.hpp"

class PowerSTEP01Adapter : public Services::IMotorDriver {
public:
    explicit PowerSTEP01Adapter(PowerSTEP01& driver) : m_driver(driver) {}

    void init() override { m_driver.init(); }

    void run(bool forward, uint32_t speed) override { m_driver.run(forward, speed); }
    void move(bool forward, uint32_t steps) override { m_driver.move(forward, steps); }
    void goTo(int32_t position) override { m_driver.goTo(position); }
    void softStop() override { m_driver.softStop(); }
    void hardStop() override { m_driver.hardStop(); }
    void softHiZ() override { m_driver.softHiZ(); }
    void hardHiZ() override { m_driver.hardHiZ(); }
    void goHome() override { m_driver.goHome(); }
    void goMark() override { m_driver.goMark(); }
    void resetPos() override { m_driver.resetPos(); }

    uint32_t getParam(uint8_t reg) override {
        return m_driver.getParam(static_cast<PowerSTEP01::Reg>(reg));
    }

    void setParam(uint8_t reg, uint32_t value) override {
        m_driver.setParam(static_cast<PowerSTEP01::Reg>(reg), value);
    }

    Services::MotorStatus getStatus() override {
        auto st = m_driver.getStatus();
        return Services::MotorStatus{st.raw};
    }

    bool isBusy() override { return m_driver.isBusy(); }
    bool flagActive() override { return m_driver.flagActive(); }

private:
    PowerSTEP01& m_driver;
};
