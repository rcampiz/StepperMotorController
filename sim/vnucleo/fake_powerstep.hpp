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
#include "L4_drivers/devices/powerstep01_settings.hpp"
#include "shaft.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace Sim {

class FakePowerStep : public Drivers::IMotorDriver, public IShaft {
public:
    static constexpr double SPEED_LSB = 67.108864;      // SPEED reg units per full step/s
    static constexpr double ACC_LSB = 14.5519;          // steps/s^2 per ACC/DEC unit
    static constexpr double MAXSPD_LSB = 15.2588;       // steps/s per MAX_SPEED unit

    FakePowerStep() { resetRegisters(); }

    void init() override;
    void setStandby(bool standby) override;
    void run(bool forward, float fullStepsPerSec) override;   // converted like the chip: SPEED register
    void move(bool forward, uint32_t steps) override;
    void goTo(int32_t position) override;
    void softStop() override;
    void hardStop() override;
    void softHiZ() override;
    void hardHiZ() override;
    void goHome() override;
    void goMark() override;
    void resetPos() override;
    void setMaxSpeed(float fullStepsPerSec) override;
    float getMaxSpeed() override;
    void setMark(int32_t positionMicrosteps) override;
    void setAcceleration(float fsps2) override;
    void setDeceleration(float fsps2) override;

    // Settings: the real driver's engine (powerstep01_settings.hpp) over these registers, so
    // the fake chip sees exactly the register traffic the firmware's driver would send
    const Drivers::DriverCaps& caps() const override;
    Drivers::DriverSettings quantize(const Drivers::DriverSettings& s) const override;
    Drivers::ApplyResult apply(const Drivers::DriverSettings& s) override;
    bool readSettings(Drivers::DriverSettings& inForce) override;
    bool checkReset() override;
    size_t diagnostics(Harness::DriverDiagView view, char* buf, size_t size) override;
    bool toLegacyUnits(const Drivers::DriverSettings& s, Drivers::LegacyDriverUnits& out) const override;
    bool fromLegacyUnits(const Drivers::LegacyDriverUnits& in, Drivers::DriverSettings& s) const override;

    // Register port (the settings engine's Io): the emulated chip's registers
    uint32_t readReg(uint8_t reg);
    void writeReg(uint8_t reg, uint32_t value);
    bool outputsOff() { return getStatus().hiZ(); }
    void clearFaults() { (void)getStatus(); }

    Drivers::MotorStatus getStatus() override;
    Drivers::MotorStatus readState() override;
    bool isBusy() override;
    bool flagActive() override { return false; }

    /// Optional log of every motion command on the machine-wide monotonic clock
    /// (steady_clock ns), so start skew between board processes can be measured.
    static void openMotionLog(const char* path);

    /// Microstep position and microsteps per full step.
    double positionMicrosteps();
    int microstepsPerFullStep();

    // The shaft (for the fake encoder) and the SYNC output: 8 pulses per full step (STEP_MODE
    // SYNC_SEL, counted by TIM3)
    double shaftFullSteps() override;
    bool hasSyncOutput() const override { return true; }
    double syncPulses() override { return shaftFullSteps() * 8.0; }

private:
    enum class Mode { Idle, Run, Position, Stopping };
    void resetRegisters();
    void update();                       // integrate to now (call with m_ held)
    static void logMotion(const char* what, long long value);
    void runRaw(bool forward, uint32_t speed);   // RUN with a SPEED register value
    double vmax() const { return regs_[Drivers::PowerStepReg::MAX_SPEED] * MAXSPD_LSB; }
    double acc() const { return std::max(1.0, regs_[Drivers::PowerStepReg::ACC] * ACC_LSB); }
    double dec() const { return std::max(1.0, regs_[Drivers::PowerStepReg::DEC] * ACC_LSB); }
    int usPerFs() const { return 1 << (regs_[Drivers::PowerStepReg::STEP_MODE] & 7); }

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
    Drivers::PowerStepSettings::ResetWatch resetWatch_;   // GATECFG1 stays 0 here: never fires
};

} // namespace Sim
