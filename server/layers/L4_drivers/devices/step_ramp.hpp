/**
 * @file step_ramp.hpp
 * @brief Trapezoidal STEP pulse ramps for drivers that have no motion engine of their own
 *        (plain STEP/DIR chips): the per-pulse recurrence for the step timer's interrupt,
 *        the software position count, and the set-up in physical units.
 *
 * L4 only (driver boundary step 5, docs/design/DRIVER_BOUNDARY.md). The powerSTEP01 runs its
 * profiles in the chip; a STEP/DIR chip only counts the pulses it gets, so its driver makes
 * every pulse here and counts them (the position is the driver's, not the chip's).
 *
 * The recurrence is D. Austin's ("Generate stepper-motor speed profiles in real time",
 * Embedded Systems Programming, Jan 2005; Atmel AVR446): with the timer at f ticks/s and an
 * acceleration of a microsteps/s^2, the period before the first pulse is
 *     c0 = 0.676 x f x sqrt(2 / a)                 (0.676: Austin's first-step correction)
 * and after the n-th pulse of a ramp
 *     c(n) = c(n-1) - (2 c(n-1) + rest) / (4n + 1)  (rest: the division's remainder, carried)
 * n counts up while accelerating; deceleration runs n from -k up to 0, k pulses to a standstill.
 * Periods are Q24.8 timer ticks and the fraction is dithered into the integer periods the
 * timer takes, so a cruise speed is exact on average. The index n at a speed v is v^2 / 2a,
 * so the pulses needed to stop from it are n x a / d: one 32 x 32 multiply per pulse.
 *
 * Contexts: configure / run / moveTo / softStop / halt / begin / setPosition and the setters
 * are task context, called with the pulse interrupt masked (the driver's critical section),
 * and may use floats. onPulse() is the interrupt: integers only, bounded, no calls out.
 *
 * Modes: RUN holds a speed in a direction (a reversal or speed 0 decelerates to a standstill
 * first), POSITION goes to a target and stops on it (overshooting and coming back only when
 * a new target is too close to stop for), STOP decelerates to a standstill. Changing the
 * mode or the target while pulses run takes effect at the next pulse.
 */
#pragma once

#include <stdint.h>
#include <math.h>

namespace Drivers {

class StepRamp {
public:
    enum class Mode : uint8_t { IDLE = 0, RUN, POSITION, STOP };
    enum class Phase : uint8_t { STOPPED = 0, ACCEL, CRUISE, DECEL };

    static constexpr uint32_t MAX_PERIOD_Q8 = 1u << 29;  ///< 2^21 ticks: anything slower is a standstill
    static constexpr int32_t  MAX_INDEX     = 1 << 26;   ///< ramp index cap: 2c + rest stays in int32
    static constexpr float    C0_FACTOR     = 0.676f;    ///< Austin's first-step correction

    // --- Set-up (task context, pulse interrupt masked) -------------------------------------
    /// Timer rate, resolution in force, ramps (full steps/s^2), speed ceiling (full steps/s)
    /// and the shortest period the timer sustains (ticks)
    void configure(uint32_t tickHz, uint16_t microsteps, float accelFsps2, float decelFsps2,
                   float ceilingFsps, uint32_t minPeriodTicks)
    {
        m_tickHz = tickHz != 0 ? tickHz : 1;
        m_microsteps = microsteps != 0 ? microsteps : 1;
        m_minQ8 = (minPeriodTicks != 0 ? minPeriodTicks : 1) << 8;
        m_accel = accelFsps2;
        m_decel = decelFsps2;
        m_ceilingFsps = ceilingFsps;
        recompute();
    }
    void setAccel(float fsps2) { m_accel = fsps2; recompute(); }
    void setDecel(float fsps2) { m_decel = fsps2; recompute(); }
    void setCeiling(float fsps) { m_ceilingFsps = fsps; recompute(); }
    /// A new resolution: the position is rescaled so it stays the same shaft angle
    /// (call at a standstill; the driver writes the chip's mode pins)
    void setMicrosteps(uint16_t microsteps)
    {
        const uint16_t n = microsteps != 0 ? microsteps : 1;
        const int64_t p = static_cast<int64_t>(m_pos) * n / m_microsteps;
        m_pos = static_cast<int32_t>(p);
        m_target = static_cast<int32_t>(static_cast<int64_t>(m_target) * n / m_microsteps);
        m_microsteps = n;
        recompute();
    }

    /// A speed as a period, Q8 ticks: 0 for a speed of 0 or slower than the timer can count
    uint32_t periodQ8(float fsps) const
    {
        const float pulses = fsps * static_cast<float>(m_microsteps);
        if (!(pulses > 0.0f)) return 0;
        const float q8 = static_cast<float>(m_tickHz) * 256.0f / pulses;
        if (!(q8 < static_cast<float>(MAX_PERIOD_Q8))) return 0;
        const uint32_t p = static_cast<uint32_t>(q8 + 0.5f);
        return p < m_minQ8 ? m_minQ8 : p;
    }

    // --- Commands (task context, pulse interrupt masked) -----------------------------------
    // True when the ramp is at a standstill and pulses must start: call begin() and start the
    // timer with the period it returns. Otherwise the running interrupt picks the change up.

    bool run(bool forward, float fsps)
    {
        m_mode = Mode::RUN;
        m_runForward = forward;
        m_runQ8 = periodQ8(fsps);
        return m_phase == Phase::STOPPED && m_runQ8 != 0;
    }
    bool moveTo(int32_t target)
    {
        m_mode = Mode::POSITION;
        m_target = target;
        if (m_phase == Phase::STOPPED && target == m_pos) {
            m_mode = Mode::IDLE;
            return false;
        }
        return m_phase == Phase::STOPPED;
    }
    /// Decelerate to a standstill (the speed ceiling's ramp)
    void softStop() { m_mode = (m_phase == Phase::STOPPED) ? Mode::IDLE : Mode::STOP; }
    /// A standstill now, no ramp (hard stop, Hi-Z, E-STOP); the timer is the caller's to stop
    void halt()
    {
        m_mode = Mode::IDLE;
        m_phase = Phase::STOPPED;
        m_n = 0;
        m_rest = 0;
    }
    /// From a standstill: the direction and first period (timer ticks) of the motion asked for
    uint32_t begin()
    {
        if (m_mode == Mode::RUN) {
            m_dir = m_runForward ? 1 : -1;
        } else if (m_mode == Mode::POSITION) {
            m_dir = (m_target > m_pos) ? 1 : -1;
        }
        return restart();
    }
    void setPosition(int32_t p)
    {
        m_target += p - m_pos;              // a target keeps its distance (RESET_POS mid-move)
        m_pos = p;
    }

    // --- The pulse interrupt ---------------------------------------------------------------
    /// After a pulse: count it, then the ticks to the next pulse (0: standstill, no more pulses)
    uint32_t onPulse()
    {
        if (m_phase == Phase::STOPPED) return 0;      // a pulse that raced a halt()
        m_pos += m_dir;

        bool stop = true;                              // decelerate to a standstill
        uint32_t want = m_ceilQ8;                      // otherwise the period to cruise at
        int32_t rem = 0;                               // POSITION: pulses left this way
        if (m_mode == Mode::RUN) {
            stop = (m_runQ8 == 0) || (m_runForward != (m_dir > 0));
            want = m_runQ8 > m_ceilQ8 ? m_runQ8 : m_ceilQ8;
        } else if (m_mode == Mode::POSITION) {
            rem = (m_target - m_pos) * m_dir;
            stop = false;
        }

        if (m_phase == Phase::DECEL) {
            if (++m_n >= 0) return standstill();
            const int32_t left = -m_n;
            if (m_mode == Mode::POSITION) {
                if (rem <= left + 2 + left / 8) return step();   // landing (or overshooting)
                want = m_ceilQ8;                       // far from the target: the ceiling rules
            }
            if (!stop && m_c >= want) {                // at or below the speed wanted: hold or speed up
                m_n = scale(left, m_decToAccQ16);
                m_phase = Phase::CRUISE;
                m_rest = 0;
                return toward(want);
            }
            step();
            if (!stop && m_c >= want) cruiseAt(want);  // slowed down to the speed wanted
            return ticks();
        }

        // ACCEL or CRUISE
        if (m_mode == Mode::POSITION) {
            const int32_t need = stopPulses();
            if (rem > need) return toward(m_ceilQ8);
            const int32_t slack = 2 + need / 8 + static_cast<int32_t>(m_accToDecQ16 >> 16);
            if (rem >= 0 && rem + slack >= need) {     // close enough to stop on the target
                return rem == 0 ? standstill() : decelerate(rem);
            }
            return need == 0 ? standstill() : decelerate(need);   // overshoot, then come back
        }
        if (stop) {
            const int32_t need = stopPulses();
            return need == 0 ? standstill() : decelerate(need);
        }
        return toward(want);
    }

    // --- State (any context; a task reads it with the interrupt masked) --------------------
    int32_t  position() const { return m_pos; }
    int32_t  target() const { return m_target; }
    bool     forward() const { return m_dir > 0; }
    Phase    phase() const { return m_phase; }
    Mode     mode() const { return m_mode; }
    bool     active() const { return m_phase != Phase::STOPPED; }
    uint16_t microsteps() const { return m_microsteps; }
    uint32_t periodQ8Now() const { return m_c; }
    /// The speed now, full steps/s (task context)
    float speedFsps() const
    {
        if (m_phase == Phase::STOPPED || m_c == 0) return 0.0f;
        return static_cast<float>(m_tickHz) * 256.0f / (static_cast<float>(m_c) * static_cast<float>(m_microsteps));
    }
    uint32_t firstPeriodQ8() const { return m_c0Q8; }
    uint32_t ceilingQ8() const { return m_ceilQ8; }
    /// Pulses needed to stop from the speed now, at the deceleration
    int32_t stopPulses() const
    {
        if (m_phase == Phase::DECEL) return -m_n;
        return m_n > 0 ? scale(m_n, m_accToDecQ16) : 0;
    }

private:
    void recompute()
    {
        const float a = (m_accel > 0.0f ? m_accel : 1.0f) * static_cast<float>(m_microsteps);
        const float d = (m_decel > 0.0f ? m_decel : 1.0f) * static_cast<float>(m_microsteps);
        float c0 = C0_FACTOR * static_cast<float>(m_tickHz) * sqrtf(2.0f / a) * 256.0f;
        if (!(c0 < static_cast<float>(MAX_PERIOD_Q8))) c0 = static_cast<float>(MAX_PERIOD_Q8);
        m_c0Q8 = static_cast<uint32_t>(c0);
        if (m_c0Q8 < m_minQ8) m_c0Q8 = m_minQ8;
        m_accToDecQ16 = ratioQ16(a / d);
        m_decToAccQ16 = ratioQ16(d / a);
        const uint32_t ceil = periodQ8(m_ceilingFsps);
        m_ceilQ8 = ceil != 0 ? ceil : MAX_PERIOD_Q8;   // no ceiling: the slowest speed there is
    }
    static uint32_t ratioQ16(float r)
    {
        const float q = r * 65536.0f;
        if (!(q >= 1.0f)) return 1;
        return q > 16777216.0f ? 16777216u : static_cast<uint32_t>(q + 0.5f);
    }
    static int32_t scale(int32_t n, uint32_t ratioQ16)
    {
        const uint64_t v = (static_cast<uint64_t>(n) * ratioQ16) >> 16;
        return v > static_cast<uint64_t>(MAX_INDEX) ? MAX_INDEX : static_cast<int32_t>(v);
    }

    /// One step of the recurrence at the current index (the period after this pulse)
    uint32_t step()
    {
        const int32_t den = 4 * m_n + 1;
        const int32_t num = 2 * static_cast<int32_t>(m_c) + static_cast<int32_t>(m_rest);
        const int32_t q = num / den;
        m_rest = static_cast<uint32_t>(num % den);    // sign of num (>= 0): 0 .. |den| - 1
        int32_t c = static_cast<int32_t>(m_c) - q;
        if (c < static_cast<int32_t>(m_minQ8)) c = static_cast<int32_t>(m_minQ8);
        if (c > static_cast<int32_t>(MAX_PERIOD_Q8)) c = static_cast<int32_t>(MAX_PERIOD_Q8);
        m_c = static_cast<uint32_t>(c);
        return ticks();
    }
    /// Accelerating or cruising: toward the period wanted (a lower speed decelerates to it)
    uint32_t toward(uint32_t want)
    {
        if (m_c > want) {                              // slower than wanted: accelerate
            if (m_phase != Phase::ACCEL) m_rest = 0;
            m_phase = Phase::ACCEL;
            if (m_n < MAX_INDEX) ++m_n;
            step();
            if (m_c <= want) cruiseAt(want);
            return ticks();
        }
        if (m_c < want) {                              // faster than wanted (lower speed or ceiling)
            const int32_t need = stopPulses();
            if (need == 0) {
                cruiseAt(want);
                return ticks();
            }
            decelerate(need);
            if (m_c >= want) cruiseAt(want);
            return ticks();
        }
        m_phase = Phase::CRUISE;                       // index kept: it is the speed's
        return ticks();
    }
    /// Start decelerating: k pulses to a standstill from here
    uint32_t decelerate(int32_t k)
    {
        m_phase = Phase::DECEL;
        m_n = -(k < MAX_INDEX ? k : MAX_INDEX);
        m_rest = 0;
        return step();
    }
    void cruiseAt(uint32_t want)
    {
        if (m_phase == Phase::DECEL) m_n = scale(-m_n, m_decToAccQ16);   // back to an accel index
        m_c = want;
        m_phase = Phase::CRUISE;
        m_rest = 0;
    }
    /// At a standstill: done, or start again (a reversal, or a target not reached)
    uint32_t standstill()
    {
        if (m_mode == Mode::RUN && m_runQ8 != 0) {
            m_dir = m_runForward ? 1 : -1;
            return restart();
        }
        if (m_mode == Mode::POSITION && m_target != m_pos) {
            m_dir = (m_target > m_pos) ? 1 : -1;
            return restart();
        }
        halt();
        return 0;
    }
    /// From a standstill in m_dir: the first period (the cruise one if that is slower)
    uint32_t restart()
    {
        const uint32_t want = (m_mode == Mode::RUN && m_runQ8 > m_ceilQ8) ? m_runQ8 : m_ceilQ8;
        m_n = 0;
        m_rest = 0;
        m_frac = 0;
        if (want >= m_c0Q8) {
            m_c = want;
            m_phase = Phase::CRUISE;
        } else {
            m_c = m_c0Q8;
            m_phase = Phase::ACCEL;
        }
        return ticks();
    }
    /// The next period in whole ticks, the Q8 fraction carried to the next one
    uint32_t ticks()
    {
        const uint32_t t = m_c + m_frac;
        m_frac = t & 0xFFu;
        const uint32_t whole = t >> 8;
        return whole != 0 ? whole : 1;
    }

    // Profile
    uint32_t m_tickHz = 1000000;
    uint16_t m_microsteps = 1;
    float    m_accel = 1000.0f;
    float    m_decel = 1000.0f;
    float    m_ceilingFsps = 1000.0f;
    uint32_t m_minQ8 = 256;
    uint32_t m_c0Q8 = MAX_PERIOD_Q8;
    uint32_t m_ceilQ8 = MAX_PERIOD_Q8;
    uint32_t m_accToDecQ16 = 65536;
    uint32_t m_decToAccQ16 = 65536;
    // Commands
    volatile Mode m_mode = Mode::IDLE;
    bool     m_runForward = true;
    uint32_t m_runQ8 = 0;
    int32_t  m_target = 0;
    // Interrupt state
    volatile Phase m_phase = Phase::STOPPED;
    volatile int32_t m_pos = 0;
    int32_t  m_dir = 1;
    int32_t  m_n = 0;
    uint32_t m_c = MAX_PERIOD_Q8;
    uint32_t m_rest = 0;
    uint32_t m_frac = 0;
};

} // namespace Drivers
