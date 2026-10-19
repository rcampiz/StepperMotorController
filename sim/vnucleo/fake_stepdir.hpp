/**
 * @file fake_stepdir.hpp
 * @brief A STEP/DIR stepper board for the virtual Nucleo (driver boundary step 5): fake L5
 *        pins and step pulse timer, a fake DRV8825-class chip turning the shaft, and the
 *        firmware's real L4 driver (Drivers::StepDirDriver) on top.
 *
 * Unlike the powerSTEP01 fake (which stands in for the L4 driver itself), here the driver is
 * the production code and only the hardware below it is fake, so the encoder checks the
 * driver: the shaft turns only by the STEP edges the driver's ramp makes, in the direction
 * of the DIR line, by the resolution of the MODE pins, while nENABLE is low.
 *
 *   vnucleo --driver stepdir
 */
#pragma once

#include "shaft.hpp"
#include "host_platform.hpp"

#include "F_platform/boundary/icritical_section.hpp"
#include "harness/defs/harness_l4_l5.hpp"
#include "L4_drivers/devices/stepdir_driver.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

namespace Sim {

/// A GPIO line (both ends see the same level: the driver writes, the chip reads, or back)
class FakeGpio : public Board::IGPIO {
public:
    explicit FakeGpio(bool level = false) : level_(level) {}
    void write(bool value) override { level_.store(value); }
    bool read() const override { return level_.load(); }
private:
    std::atomic<bool> level_;
};

/// The step timer: pulses at simulated instants on the host clock, its "interrupt" (the
/// driver's handler) run in order for each one. A background thread and every entry of the
/// interrupt mask catch it up to now, so the driver always sees the pulses that fell due,
/// as on hardware, where the interrupt would have run before the task masked it.
class FakeStepPulseTimer : public Board::IStepPulseTimer {
public:
    using StepLine = std::function<void()>;      ///< the STEP wire: the chip sees each pulse

    FakeStepPulseTimer(uint32_t tickHz, uint32_t minPeriodTicks);
    ~FakeStepPulseTimer() override;

    void connect(StepLine line) { std::lock_guard<std::recursive_mutex> lk(m_); line_ = std::move(line); }

    uint32_t tickHz() const override { return tickHz_; }
    uint32_t minPeriodTicks() const override { return minTicks_; }
    void setHandler(PulseHandler handler, void* context) override;
    void start(uint32_t firstPeriodTicks) override;
    void stop() override;
    bool running() const override;

    /// The driver's critical section: masks the pulse interrupt (entering it first runs the
    /// pulses due by now)
    Platform::ICriticalSection& isrMask() { return mask_; }
    /// Run every pulse due by now (any thread)
    void service();

private:
    class Mask : public Platform::ICriticalSection {
    public:
        explicit Mask(FakeStepPulseTimer& t) : t_(t) {}
        void enter() override;
        void exit() override;
    private:
        FakeStepPulseTimer& t_;
        int depth_ = 0;                          // guarded by t_.m_
    };
    void serviceLocked();                        // with m_ held

    const uint32_t tickHz_, minTicks_;
    mutable std::recursive_mutex m_;
    Mask mask_{*this};
    StepLine line_;
    PulseHandler handler_ = nullptr;
    void* context_ = nullptr;
    bool on_ = false;
    bool inService_ = false;
    double next_ = 0.0;                          // host seconds of the next pulse
    std::atomic<bool> quit_{false};
    std::thread thread_;
};

/// A DRV8825-class chip: the indexer counts STEP edges in DIR's direction by the MODE pins'
/// resolution; the rotor follows only while nENABLE is low. No step-sync output.
class FakeStepDirChip : public IShaft {
public:
    FakeStepDirChip(FakeGpio& dir, FakeGpio& nEnable, FakeGpio& mode0, FakeGpio& mode1, FakeGpio& mode2,
                    FakeStepPulseTimer& timer)
        : dir_(dir), nEnable_(nEnable), mode0_(mode0), mode1_(mode1), mode2_(mode2), timer_(timer) {}

    /// A rising edge on STEP (from the timer's thread or a task catching it up)
    void stepEdge();

    double shaftFullSteps() override;
    bool hasSyncOutput() const override { return false; }

private:
    FakeGpio& dir_;
    FakeGpio& nEnable_;
    FakeGpio& mode0_;
    FakeGpio& mode1_;
    FakeGpio& mode2_;
    FakeStepPulseTimer& timer_;
    std::mutex m_;
    long long rotor32_ = 0;                      // rotor angle in 1/32 full steps
};

/// The board: pins, timer, chip, and the firmware's driver wired to them
struct StepDirBoard {
    static constexpr uint32_t TICK_HZ = 1000000;   ///< a 1 MHz timebase (TIM2/TIM5 prescaled)
    static constexpr uint32_t MIN_PERIOD = 20;     ///< 50 kHz: what a per-pulse interrupt sustains

    FakeGpio dir, nEnable{true}, mode0, mode1, mode2, nFault{true};
    FakeStepPulseTimer timer{TICK_HZ, MIN_PERIOD};
    FakeStepDirChip chip{dir, nEnable, mode0, mode1, mode2, timer};
    Drivers::StepDirDriver driver;

    StepDirBoard();
};

} // namespace Sim
