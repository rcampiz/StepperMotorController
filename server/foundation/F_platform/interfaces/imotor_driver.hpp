/**
 * @file imotor_driver.hpp
 * @brief Interface for motor driver hardware abstraction
 *
 * Foundation interface for motor driver hardware abstraction.
 * Implemented by PowerSTEP01Adapter in wiring/. Enables mock
 * injection for unit testing motor_task logic without real hardware.
 */

#pragma once

#include <stdint.h>

namespace Services {

/**
 * @brief Motor driver status register decoded fields
 *
 * Bit layout matches powerSTEP01 STATUS register.
 */
struct MotorStatus {
    uint16_t raw;

    bool hiZ()       const { return raw & (1 << 0); }
    bool busy()      const { return !(raw & (1 << 1)); }
    bool swOn()      const { return raw & (1 << 2); }
    bool swEvent()   const { return raw & (1 << 3); }
    bool dir()       const { return raw & (1 << 4); }
    uint8_t motStatus() const { return (raw >> 5) & 0x3; }
    bool cmdErr()    const { return raw & (1 << 7); }
    bool uvlo()      const { return !(raw & (1 << 9)); }
    uint8_t thStatus() const { return (raw >> 11) & 0x3; }
    bool thermalWarn() const { return thStatus() >= 1; }
    bool thermalSD()   const { return thStatus() >= 2; }
    bool ocd()       const { return !(raw & (1 << 13)); }
    bool stallA()    const { return !(raw & (1 << 14)); }
    bool stallB()    const { return !(raw & (1 << 15)); }
};

/**
 * @brief Register address constants (match powerSTEP01 addresses)
 */
namespace MotorReg {
    constexpr uint8_t ABS_POS    = 0x01;
    constexpr uint8_t EL_POS     = 0x02;
    constexpr uint8_t MARK       = 0x03;
    constexpr uint8_t SPEED      = 0x04;
    constexpr uint8_t ACC        = 0x05;
    constexpr uint8_t DEC        = 0x06;
    constexpr uint8_t MAX_SPEED  = 0x07;
    constexpr uint8_t MIN_SPEED  = 0x08;
    constexpr uint8_t KVAL_HOLD  = 0x09;
    constexpr uint8_t KVAL_RUN   = 0x0A;
    constexpr uint8_t KVAL_ACC   = 0x0B;
    constexpr uint8_t KVAL_DEC   = 0x0C;
    constexpr uint8_t INT_SPEED  = 0x0D;
    constexpr uint8_t OCD_TH     = 0x13;
    constexpr uint8_t STALL_TH   = 0x14;
    constexpr uint8_t FS_SPD     = 0x15;
    constexpr uint8_t STEP_MODE  = 0x16;
    constexpr uint8_t ALARM_EN   = 0x17;
    constexpr uint8_t CONFIG     = 0x1A;
} // namespace MotorReg

/**
 * @brief Abstract motor driver interface
 *
 * Provides motion commands, register access, and status queries.
 * Hot path: getStatus() + getParam() called every 50ms in motor loop.
 * Virtual dispatch ~5ns vs ~50us SPI transaction = negligible overhead.
 */
class IMotorDriver {
public:
    virtual ~IMotorDriver() = default;

    /** @brief Initialize/reinitialize the driver hardware */
    virtual void init() = 0;

    // --- Motion commands ---
    virtual void run(bool forward, uint32_t speed) = 0;
    virtual void move(bool forward, uint32_t steps) = 0;
    virtual void goTo(int32_t position) = 0;
    virtual void softStop() = 0;
    virtual void hardStop() = 0;
    virtual void softHiZ() = 0;
    virtual void hardHiZ() = 0;
    virtual void goHome() = 0;
    virtual void goMark() = 0;
    virtual void resetPos() = 0;

    // --- Register access ---
    virtual uint32_t getParam(uint8_t reg) = 0;
    virtual void setParam(uint8_t reg, uint32_t value) = 0;

    // --- Status ---
    virtual MotorStatus getStatus() = 0;
    virtual bool isBusy() = 0;
    virtual bool flagActive() = 0;
};

} // namespace Services
