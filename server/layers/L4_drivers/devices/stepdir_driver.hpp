/**
 * @file stepdir_driver.hpp
 * @brief A generic STEP/DIR stepper driver behind Drivers::IMotorDriver (driver boundary step 5)
 *
 * The second driver, the proof that the boundary is the chip's: same IMotorDriver, same L3,
 * same protocol, nothing in common with the powerSTEP01 below it. A STEP/DIR chip has no
 * registers, no status word, no motion engine and no position: the MCU makes every STEP
 * pulse (Board::IStepPulseTimer + the ramp in step_ramp.hpp), counts them, and sets the
 * resolution on the chip's mode pins. What the chip cannot do, the caps say (DriverCaps):
 * no software current or voltage drive, no stall detection, no hardware positioning, no
 * step-sync output, faults only as one nFAULT line.
 *
 * Chip facts (the TI DRV8825, the usual part on STEP/DIR carrier boards; datasheet SLVSA73F):
 *   MODE2..0       microsteps 1 << MODE (000 full .. 101 1/32; 110, 111 also 1/32). The
 *                  indexer takes a new mode at the next STEP edge and keeps its electrical
 *                  angle, so the pins may change at a standstill with the bridges on; never
 *                  while pulses run (the ramp's pulse rate is per microstep).
 *   nENABLE        high = bridges off (the indexer keeps counting STEP edges). One GPIO write:
 *                  the out-of-band E-STOP (setStandby) and Hi-Z.
 *   STEP, DIR      rising edge steps; DIR set up >= 650 ns before it, STEP high and low
 *                  >= 1.9 us (250 kHz). DIR high = forward (a wiring convention).
 *   nFAULT         open drain, low on overcurrent (latched until nRESET or power) or thermal
 *                  shutdown; the chip does not say which, so both faults are reported.
 *   Current        the chopper's reference is set by the VREF pot: I = VREF / (5 x Rsense),
 *                  not software-settable (DriverCaps::currentControl = true, the chip
 *                  regulates current; currentSettable = false).
 *   Overcurrent    the chip's own fixed trip, ~3 A (datasheet IOCP), not settable.
 *   nSLEEP/nRESET  tied high on the carrier; no reset to detect (checkReset() stays false).
 *
 * Positions are the driver's own pulse count, microsteps at the resolution in force (a
 * resolution change rescales it, so it stays the same shaft angle); it survives init().
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include "harness/defs/harness_l4_l5.hpp"
#include "F_platform/boundary/icritical_section.hpp"
#include "L4_drivers/boundary/imotor_driver.hpp"
#include "L4_drivers/devices/step_ramp.hpp"

namespace Drivers {

namespace StepDirChip {
constexpr uint16_t MAX_MICROSTEPS   = 32;        ///< MODE 101
constexpr float    OCP_TRIP_A       = 3.0f;      ///< datasheet IOCP; fixed in the chip
constexpr uint8_t  FAULTS_ON_NFAULT = Harness::DriverFault::OVERCURRENT | Harness::DriverFault::THERMAL_SHUTDOWN;
constexpr float    SPEED_LIMIT_STEP = 1.0f / 64.0f;   ///< ceiling grid, full steps/s (exact in binary)
constexpr float    ACCEL_STEP       = 1.0f;      ///< ramp grid, full steps/s^2
constexpr float    ACCEL_MAX        = 60000.0f;
constexpr int32_t  POSITION_LIMIT   = (1 << 30) - 1;   ///< the count's range (target - position fits int32)
constexpr float    DEFAULT_ACCEL    = 1000.0f;   ///< until the first apply()
constexpr float    DEFAULT_CEILING  = 1000.0f;
}

class StepDirDriver : public IMotorDriver {
public:
    /// The chip's lines: DIR, nENABLE and MODE0..2 outputs, nFAULT input (nullptr: not wired)
    struct Pins {
        Board::IGPIO* dir = nullptr;
        Board::IGPIO* nEnable = nullptr;
        Board::IGPIO* mode[3] = { nullptr, nullptr, nullptr };
        Board::IGPIO* nFault = nullptr;
    };

    /// `critical` must mask the step timer's interrupt (the ramp is shared with it).
    /// Outputs off, full step (the chip's power-on state with the MODE pins low).
    StepDirDriver(Board::IStepPulseTimer& timer, const Pins& pins, Platform::ICriticalSection& critical)
        : m_timer(timer), m_pins(pins), m_critical(critical)
    {
        buildCaps();
        m_inForce = defaultSettings();
        m_ceilingFsps = m_inForce.maxSpeedFsps;
        outputs(false);
        writeModePins(m_microsteps);
        m_ramp.configure(m_timer.tickHz(), m_microsteps, m_inForce.accelFsps2, m_inForce.decelFsps2,
                         m_ceilingFsps, m_timer.minPeriodTicks());
        m_timer.setHandler(&StepDirDriver::pulseIsr, this);
    }

    void init() override
    {
        Guard g(m_critical);
        m_timer.stop();
        m_ramp.halt();
        m_halted = false;
        m_releaseAtStop = false;
        outputs(false);                           // as after a chip reset: bridges off
        writeModePins(m_microsteps);              // MCU outputs: the resolution is still ours
    }

    // --- Motion ---------------------------------------------------------------------------
    void run(bool forward, float fullStepsPerSec) override
    {
        Guard g(m_critical);
        beginCommand();
        outputs(true);
        if (m_ramp.run(forward, clampSpeed(fullStepsPerSec))) startPulses();
    }
    void move(bool forward, uint32_t microsteps) override
    {
        Guard g(m_critical);
        const int64_t d = static_cast<int64_t>(microsteps) * (forward ? 1 : -1);
        goToLocked(clampPosition(static_cast<int64_t>(m_ramp.position()) + d));
    }
    void goTo(int32_t positionMicrosteps) override
    {
        Guard g(m_critical);
        goToLocked(clampPosition(positionMicrosteps));
    }
    void softStop() override
    {
        Guard g(m_critical);
        beginCommand();
        outputs(true);                            // as the powerSTEP01: a stop energises the bridges
        m_ramp.softStop();
    }
    void hardStop() override
    {
        Guard g(m_critical);
        beginCommand();
        m_timer.stop();
        m_ramp.halt();
        outputs(true);
    }
    void softHiZ() override
    {
        Guard g(m_critical);
        beginCommand();
        if (m_ramp.active()) {
            m_releaseAtStop = true;               // the pulse interrupt lets go at the standstill
            m_ramp.softStop();
        } else {
            outputs(false);
        }
    }
    void hardHiZ() override
    {
        Guard g(m_critical);
        m_timer.stop();
        m_ramp.halt();
        m_releaseAtStop = false;
        outputs(false);
    }
    void goHome() override { goTo(0); }
    void goMark() override { goTo(m_mark); }
    void resetPos() override
    {
        Guard g(m_critical);
        m_ramp.setPosition(0);
    }
    void setMark(int32_t positionMicrosteps) override { m_mark = clampPosition(positionMicrosteps); }

    void setMaxSpeed(float fullStepsPerSec) override
    {
        Guard g(m_critical);
        m_ceilingFsps = quantizeSpeedLimit(fullStepsPerSec);
        m_ramp.setCeiling(m_ceilingFsps);
    }
    float getMaxSpeed() override { return m_ceilingFsps; }
    void setAcceleration(float fsps2) override
    {
        Guard g(m_critical);
        m_inForce.accelFsps2 = quantizeAccel(fsps2);
        m_ramp.setAccel(m_inForce.accelFsps2);
    }
    void setDeceleration(float fsps2) override
    {
        Guard g(m_critical);
        m_inForce.decelFsps2 = quantizeAccel(fsps2);
        m_ramp.setDecel(m_inForce.decelFsps2);
    }

    // --- Settings -------------------------------------------------------------------------
    const DriverCaps& caps() const override { return m_caps; }

    /// What apply() puts in force: the resolution on the MODE pins, the ramps and ceiling on
    /// their grids; everything the chip has no input for at its fixed value (no software
    /// current or duty: 0; the overcurrent trip; no stall threshold; the faults nFAULT raises)
    DriverSettings quantize(const DriverSettings& s) const override
    {
        DriverSettings q = s;
        q.mode = Harness::DriveMode::CURRENT;     // a chopper drive, reference from VREF
        q.microsteps = Harness::effectiveMicrosteps(m_caps, s.microsteps, q.mode);
        q.currentA = Harness::DriveLevels{ 0.0f, 0.0f, 0.0f, 0.0f };
        q.duty = Harness::DriveLevels{ 0.0f, 0.0f, 0.0f, 0.0f };
        q.accelFsps2 = quantizeAccel(s.accelFsps2);
        q.decelFsps2 = quantizeAccel(s.decelFsps2);
        q.maxSpeedFsps = quantizeSpeedLimit(s.maxSpeedFsps);
        q.overcurrentA = StepDirChip::OCP_TRIP_A;
        q.stallThreshold01 = 0.0f;
        q.faultAlarms = m_caps.faultsReported;    // nFAULT always asserts on them
        return q;                                 // tuning: opaque, kept for its owner
    }

    /// The ramps and ceiling at once; the resolution only while no pulses run (else
    /// NEEDS_OUTPUTS_OFF: the caller stops, releases and applies again). Nothing to clear:
    /// a latched overcurrent needs nRESET or power, which this board does not switch.
    ApplyResult apply(const DriverSettings& s) override
    {
        const DriverSettings q = quantize(s);
        Guard g(m_critical);
        syncHalt();
        m_inForce = q;
        m_ceilingFsps = q.maxSpeedFsps;
        m_ramp.setAccel(q.accelFsps2);
        m_ramp.setDecel(q.decelFsps2);
        m_ramp.setCeiling(m_ceilingFsps);
        ApplyResult r = ApplyResult::APPLIED;
        if (q.microsteps != m_microsteps) {
            if (m_ramp.active()) {
                r = ApplyResult::NEEDS_OUTPUTS_OFF;
            } else {
                writeModePins(q.microsteps);
                m_microsteps = readModePins();
                m_ramp.setMicrosteps(m_microsteps);
                if (m_microsteps != q.microsteps) r = ApplyResult::VERIFY_FAILED;
            }
        }
        m_inForce.microsteps = m_microsteps;
        return r;
    }

    /// The settings in force, the resolution read back from the MODE pins' output latches
    bool readSettings(DriverSettings& inForce) override
    {
        Guard g(m_critical);
        inForce = m_inForce;
        inForce.microsteps = readModePins();
        return true;
    }

    size_t diagnostics(Harness::DriverDiagView view, char* buf, size_t size) override
    {
        if (buf == nullptr || size == 0) return 0;
        buf[0] = '\0';
        int n = 0;
        Guard g(m_critical);
        if (view == Harness::DriverDiagView::REGISTERS) {
            n = snprintf(buf, size,
                         "STEP/DIR: MODE=%u (1/%u) nEN=%u DIR=%u nFAULT=%u\n"
                         "POS=%ld TARGET=%ld PERIOD=%luus PHASE=%u\n"
                         "ACC=%lu DEC=%lu MAXSPD=%lu mfs/s",
                         static_cast<unsigned>(modeBits()), static_cast<unsigned>(readModePins()),
                         level(m_pins.nEnable), level(m_pins.dir), m_pins.nFault ? level(m_pins.nFault) : 1u,
                         static_cast<long>(m_ramp.position()), static_cast<long>(m_ramp.target()),
                         static_cast<unsigned long>(m_ramp.active() ? (m_ramp.periodQ8Now() >> 8) : 0u),
                         static_cast<unsigned>(m_ramp.phase()),
                         static_cast<unsigned long>(m_inForce.accelFsps2), static_cast<unsigned long>(m_inForce.decelFsps2),
                         static_cast<unsigned long>(m_ceilingFsps * 1000.0f));
        } else if (view == Harness::DriverDiagView::SUMMARY) {
            n = snprintf(buf, size, "STEP/DIR MODE=%u nEN=%u (current from VREF, OCP fixed)",
                         static_cast<unsigned>(modeBits()), level(m_pins.nEnable));
        }
        if (n < 0) return 0;
        return (static_cast<size_t>(n) < size) ? static_cast<size_t>(n) : size - 1;
    }

    // --- Status ---------------------------------------------------------------------------
    MotorStatus getStatus() override
    {
        Guard g(m_critical);
        syncHalt();
        return status();
    }
    MotorStatus readState() override
    {
        Guard g(m_critical);
        syncHalt();
        MotorStatus s = status();
        s.positionUsteps = m_ramp.position();
        s.speedFsps = m_ramp.speedFsps();
        return s;
    }
    bool isBusy() override
    {
        Guard g(m_critical);
        syncHalt();
        return m_ramp.active();
    }
    bool flagActive() override { return m_pins.nFault != nullptr && !m_pins.nFault->read(); }

    /// ISR-safe (the out-of-band E-STOP): the timer stops and nENABLE goes high, two register
    /// writes; the ramp is told at its next interrupt or task call. init() takes it back.
    void setStandby(bool standby) override
    {
        if (!standby) return;
        m_halted = true;
        m_timer.stop();
        outputs(false);
    }

    /// The pulse interrupt's body (public for host tests driving a fake timer)
    uint32_t onPulseIsr()
    {
        if (m_halted) {
            m_ramp.halt();
            return 0;
        }
        const uint32_t next = m_ramp.onPulse();
        if (next == 0) {
            if (m_releaseAtStop) {
                m_releaseAtStop = false;
                outputs(false);
            }
            return 0;
        }
        if (m_ramp.forward() != m_dirForward) setDir(m_ramp.forward());   // a reversal: DIR before the next edge
        return next;
    }

private:
    struct Guard {
        explicit Guard(Platform::ICriticalSection& c) : cs(c) { cs.enter(); }
        ~Guard() { cs.exit(); }
        Platform::ICriticalSection& cs;
    };

    static uint32_t pulseIsr(void* self) { return static_cast<StepDirDriver*>(self)->onPulseIsr(); }

    void buildCaps()
    {
        const float pulsesPerSec = static_cast<float>(m_timer.tickHz())
                                 / static_cast<float>(m_timer.minPeriodTicks() != 0 ? m_timer.minPeriodTicks() : 1u);
        float fastest = pulsesPerSec / StepDirChip::MAX_MICROSTEPS;   // at the finest resolution
        fastest = static_cast<float>(static_cast<uint32_t>(fastest / StepDirChip::SPEED_LIMIT_STEP))
                * StepDirChip::SPEED_LIMIT_STEP;
        m_caps = DriverCaps{};
        m_caps.voltageControl = false;
        m_caps.currentControl = true;             // the chopper regulates coil current...
        m_caps.currentSettable = false;           // ...to the VREF pot's reference
        m_caps.maxMicrostepsVoltage = 0;
        m_caps.maxMicrostepsCurrent = StepDirChip::MAX_MICROSTEPS;
        m_caps.stallDetect = false;
        m_caps.standbyPin = true;                 // nENABLE
        m_caps.hardwarePositioning = false;       // the ramps and the position are this driver's
        m_caps.syncPulsesPerFullStep = 0;
        m_caps.faultsReported = (m_pins.nFault != nullptr) ? StepDirChip::FAULTS_ON_NFAULT : 0u;
        m_caps.faultsAlarmable = 0;               // nFAULT is not configurable
        m_caps.maxCurrentA = 0.0f;
        m_caps.currentStepA = 0.0f;
        m_caps.dutyStep = 0.0f;
        m_caps.overcurrentMinA = StepDirChip::OCP_TRIP_A;
        m_caps.overcurrentMaxA = StepDirChip::OCP_TRIP_A;
        m_caps.overcurrentStepA = 0.0f;
        m_caps.overcurrentOhms = 0.0f;
        m_caps.stallStep01 = 0.0f;
        m_caps.maxSpeedFsps = fastest;
        m_caps.maxSpeedLimitFsps = fastest;
        m_caps.speedLimitStepFsps = StepDirChip::SPEED_LIMIT_STEP;
        m_caps.accelMinFsps2 = StepDirChip::ACCEL_STEP;
        m_caps.accelMaxFsps2 = StepDirChip::ACCEL_MAX;
        m_caps.accelStepFsps2 = StepDirChip::ACCEL_STEP;
        m_caps.minPositionUsteps = -StepDirChip::POSITION_LIMIT;
        m_caps.maxPositionUsteps = StepDirChip::POSITION_LIMIT;
    }

    DriverSettings defaultSettings() const
    {
        DriverSettings s = {};
        s.microsteps = 1;
        s.mode = Harness::DriveMode::CURRENT;
        s.accelFsps2 = StepDirChip::DEFAULT_ACCEL;
        s.decelFsps2 = StepDirChip::DEFAULT_ACCEL;
        s.maxSpeedFsps = StepDirChip::DEFAULT_CEILING;
        return quantize(s);
    }

    /// A positive value never rounds to 0 (the smallest step instead); NaN and <= 0 give 0
    float quantizeSpeedLimit(float f) const
    {
        if (!(f > 0.0f)) return 0.0f;
        if (f > m_caps.maxSpeedLimitFsps) f = m_caps.maxSpeedLimitFsps;
        const uint32_t n = static_cast<uint32_t>(f / StepDirChip::SPEED_LIMIT_STEP + 0.5f);
        return static_cast<float>(n != 0 ? n : 1u) * StepDirChip::SPEED_LIMIT_STEP;
    }
    static float quantizeAccel(float f)
    {
        if (!(f > 0.0f)) return 0.0f;
        if (f > StepDirChip::ACCEL_MAX) f = StepDirChip::ACCEL_MAX;
        const uint32_t n = static_cast<uint32_t>(f / StepDirChip::ACCEL_STEP + 0.5f);
        return static_cast<float>(n != 0 ? n : 1u) * StepDirChip::ACCEL_STEP;
    }
    /// RUN speeds: within the caps and under the ceiling (as a chip's MAX_SPEED would cap it)
    float clampSpeed(float f) const
    {
        if (!(f > 0.0f)) return 0.0f;
        if (f > m_caps.maxSpeedFsps) f = m_caps.maxSpeedFsps;
        return f;
    }
    static int32_t clampPosition(int64_t p)
    {
        if (p > StepDirChip::POSITION_LIMIT) return StepDirChip::POSITION_LIMIT;
        if (p < -StepDirChip::POSITION_LIMIT) return -StepDirChip::POSITION_LIMIT;
        return static_cast<int32_t>(p);
    }

    // Called with the critical section held
    void beginCommand()
    {
        syncHalt();
        m_halted = false;
        m_releaseAtStop = false;
    }
    void syncHalt()
    {
        if (m_halted) m_ramp.halt();              // an E-STOP from an interrupt stopped the timer
    }
    void goToLocked(int32_t target)
    {
        beginCommand();
        outputs(true);
        if (m_ramp.moveTo(target)) startPulses();
    }
    void startPulses()
    {
        const uint32_t first = m_ramp.begin();
        setDir(m_ramp.forward());
        m_timer.start(first);
    }
    void setDir(bool forward)
    {
        m_dirForward = forward;
        if (m_pins.dir != nullptr) m_pins.dir->write(forward);
    }
    void outputs(bool on)
    {
        m_outputsOn = on;
        if (m_pins.nEnable != nullptr) m_pins.nEnable->write(!on);
    }
    void writeModePins(uint16_t microsteps)
    {
        const uint8_t k = Harness::microstepsLog2(microsteps);
        for (unsigned i = 0; i < 3; ++i) {
            if (m_pins.mode[i] != nullptr) m_pins.mode[i]->write(((k >> i) & 1u) != 0u);
        }
        m_microsteps = microsteps;
    }
    unsigned modeBits() const
    {
        unsigned k = 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (m_pins.mode[i] != nullptr && m_pins.mode[i]->read()) k |= 1u << i;
        }
        return k;
    }
    /// The resolution the MODE pins select (110 and 111 are 1/32 too)
    uint16_t readModePins() const
    {
        const unsigned k = modeBits();
        return static_cast<uint16_t>(1u << (k > 5u ? 5u : k));
    }
    static unsigned level(const Board::IGPIO* p) { return (p != nullptr && p->read()) ? 1u : 0u; }

    MotorStatus status() const
    {
        MotorStatus s;
        s.outputsOff = !m_outputsOn;
        s.moving = m_ramp.active();
        s.forward = m_ramp.forward();
        switch (m_ramp.phase()) {
            case StepRamp::Phase::ACCEL:  s.phase = Harness::MotionPhase::ACCELERATING; break;
            case StepRamp::Phase::DECEL:  s.phase = Harness::MotionPhase::DECELERATING; break;
            case StepRamp::Phase::CRUISE: s.phase = Harness::MotionPhase::CONSTANT; break;
            default:                      s.phase = Harness::MotionPhase::STOPPED; break;
        }
        const bool fault = m_pins.nFault != nullptr && !m_pins.nFault->read();
        s.faults = fault ? StepDirChip::FAULTS_ON_NFAULT : 0u;
        // Opaque word for logs: nEN, DIR, MODE2..0, nFAULT, ramp phase, timer running
        s.raw = (m_outputsOn ? 0u : 1u) | (m_dirForward ? 2u : 0u) | (modeBits() << 2)
              | (fault ? 0u : 0x20u) | (static_cast<uint32_t>(m_ramp.phase()) << 8)
              | (m_timer.running() ? 0x400u : 0u);
        return s;
    }

    Board::IStepPulseTimer& m_timer;
    Pins m_pins;
    Platform::ICriticalSection& m_critical;
    DriverCaps m_caps = {};
    DriverSettings m_inForce = {};
    StepRamp m_ramp;
    uint16_t m_microsteps = 1;
    float m_ceilingFsps = 0.0f;
    int32_t m_mark = 0;
    bool m_dirForward = true;
    volatile bool m_outputsOn = false;
    volatile bool m_halted = false;               ///< set by setStandby() from an interrupt
    volatile bool m_releaseAtStop = false;        ///< softHiZ: release when the ramp stops
};

} // namespace Drivers
