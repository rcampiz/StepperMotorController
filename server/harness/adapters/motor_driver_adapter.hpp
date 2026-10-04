/**
 * @file motor_driver_adapter.hpp
 * @brief Harness adapter: Drivers::IMotorDriver (L4) --> Services::IMotorDriver (L3)
 *
 * Trivial forwarder — both interfaces are structurally identical.
 * Converts MotorStatus between Drivers:: and Services:: via raw field copy.
 */

#pragma once

#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L3_services/boundary/imotor_driver.hpp"

namespace Harness {

class MotorDriverAdapter : public Services::IMotorDriver {
public:
    explicit MotorDriverAdapter(Drivers::IMotorDriver& provider) : m_provider(provider) {}

    void init() override { m_provider.init(); }

    void run(bool forward, uint32_t speed) override { m_provider.run(forward, speed); }
    void move(bool forward, uint32_t steps) override { m_provider.move(forward, steps); }
    void goTo(int32_t position) override { m_provider.goTo(position); }
    void softStop() override { m_provider.softStop(); }
    void hardStop() override { m_provider.hardStop(); }
    void softHiZ() override { m_provider.softHiZ(); }
    void hardHiZ() override { m_provider.hardHiZ(); }
    void goHome() override { m_provider.goHome(); }
    void goMark() override { m_provider.goMark(); }
    void resetPos() override { m_provider.resetPos(); }

    uint32_t getParam(uint8_t reg) override { return m_provider.getParam(reg); }
    void setParam(uint8_t reg, uint32_t value) override { m_provider.setParam(reg, value); }

    Services::MotorStatus getStatus() override {
        Drivers::MotorStatus s = m_provider.getStatus();
        return Services::MotorStatus{s.raw};
    }

    bool isBusy() override { return m_provider.isBusy(); }
    bool flagActive() override { return m_provider.flagActive(); }

private:
    Drivers::IMotorDriver& m_provider;
};

} // namespace Harness
