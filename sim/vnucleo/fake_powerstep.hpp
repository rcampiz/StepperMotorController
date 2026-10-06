/**
 * @file fake_powerstep.hpp
 * @brief A believable powerSTEP01 behind Drivers::IMotorDriver (virtual Nucleo).
 *
 * Register model (powerSTEP01 datasheet DS9836): ABS_POS 22-bit two's complement in
 * microsteps; SPEED 20-bit, full steps/s = SPEED / 67.108864; ACC/DEC 12-bit, steps/s^2 =
 * reg x 14.5519; MAX_SPEED 10-bit, steps/s = reg x 15.2588; STEP_MODE bits 2:0. STATUS:
 * HiZ bit0, BUSY bit1 (active low), DIR bit4, MOT_STATUS bits 6:5 (0 stop, 1 acc, 2 dec,
 * 3 const), UVLO/OCD/STALL active low (set = no fault).
 *
 * Motion is integrated lazily on every call from the elapsed wall time: trapezoidal
 * profiles for RUN, MOVE, GOTO and SoftStop; HardStop/HardHiZ stop at once.
 */
#pragma once

#include "L4_drivers/boundary/imotor_driver.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace Sim {

class FakePowerStep : public Drivers::IMotorDriver {
public:
    static constexpr double SPEED_LSB = 67.108864;      // SPEED reg units per full step/s
    static constexpr double ACC_LSB = 14.5519;          // steps/s^2 per ACC/DEC unit
    static constexpr double MAXSPD_LSB = 15.2588;       // steps/s per MAX_SPEED unit

    FakePowerStep() { resetRegisters(); }

    void init() override;
    void run(bool forward, uint32_t speed) override;
    void move(bool forward, uint32_t steps) override;
    void goTo(int32_t position) override;
    void softStop() override;
    void hardStop() override;
    void softHiZ() override;
    void hardHiZ() override;
    void goHome() override;
    void goMark() override;
    void resetPos() override;
    uint32_t getParam(uint8_t reg) override;
    void setParam(uint8_t reg, uint32_t value) override;
    Drivers::MotorStatus getStatus() override;
    bool isBusy() override;
    bool flagActive() override { return false; }

    /// Optional log of every motion command on the machine-wide monotonic clock
    /// (steady_clock ns), so start skew between board processes can be measured.
    static void openMotionLog(const char* path);

    /// Microstep position (for the fake encoder) and microsteps per full step.
    double positionMicrosteps();
    int microstepsPerFullStep();

private:
    enum class Mode { Idle, Run, Position, Stopping };
    void resetRegisters();
    void update();                       // integrate to now (call with m_ held)
    static void logMotion(const char* what, long long value);
    double vmax() const { return regs_[Drivers::MotorReg::MAX_SPEED] * MAXSPD_LSB; }
    double acc() const { return std::max(1.0, regs_[Drivers::MotorReg::ACC] * ACC_LSB); }
    double dec() const { return std::max(1.0, regs_[Drivers::MotorReg::DEC] * ACC_LSB); }
    int usPerFs() const { return 1 << (regs_[Drivers::MotorReg::STEP_MODE] & 7); }

    std::recursive_mutex m_;
    uint32_t regs_[32] = {};
    double pos_ = 0.0;                   // microsteps
    double v_ = 0.0;                     // full steps/s, signed
    double targetV_ = 0.0;               // Run mode
    double targetPos_ = 0.0;             // Position mode (microsteps)
    Mode mode_ = Mode::Idle;
    bool hiZ_ = true;
    bool hiZAfterStop_ = false;
    int motStatus_ = 0;
    double last_ = -1.0;
};

} // namespace Sim
