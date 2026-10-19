/**
 * @file motor_state.hpp
 * @brief The motor driver's state, decoded by the driver: chip-neutral (shared POD pin).
 *
 * Every chip has its own status word and bit layout; only its L4 driver knows them. Above
 * L4 the state is these flags and a fault mask, so a different driver chip changes nothing
 * in L3, L2 or the client (docs/design/DRIVER_BOUNDARY.md, step 1). `raw` is the chip's own
 * status word, opaque: logs and diagnostics only, never decoded outside the driver.
 */
#pragma once

#include <stdint.h>

namespace Harness {

/// Fault bits, chip-neutral (a driver sets the ones its chip can report)
namespace DriverFault {
constexpr uint8_t OVERCURRENT      = 1u << 0;
constexpr uint8_t UNDERVOLTAGE     = 1u << 1;
constexpr uint8_t THERMAL_SHUTDOWN = 1u << 2;
constexpr uint8_t THERMAL_WARNING  = 1u << 3;
constexpr uint8_t STALL            = 1u << 4;
constexpr uint8_t COMMAND_ERROR    = 1u << 5;
/// The faults that stop the bridges (a fault latch / E-STOP page condition)
constexpr uint8_t SHUTDOWN = OVERCURRENT | UNDERVOLTAGE | THERMAL_SHUTDOWN;
}

/// Where a motion is in its profile (chips without a profile engine report STOPPED/CONSTANT)
enum class MotionPhase : uint8_t { STOPPED = 0, ACCELERATING = 1, DECELERATING = 2, CONSTANT = 3 };

struct MotorState {
    uint32_t raw = 0;                   ///< the chip's own status word: opaque, diagnostics only
    uint8_t faults = 0;                 ///< DriverFault bits
    MotionPhase phase = MotionPhase::STOPPED;
    bool outputsOff = true;             ///< bridges released (high impedance)
    bool moving = false;                ///< a motion command is executing
    bool forward = true;
    bool readable = true;               ///< false: the chip could not be read (held in standby)

    bool hiZ() const { return outputsOff; }
    bool busy() const { return moving; }
    bool ocd() const { return (faults & DriverFault::OVERCURRENT) != 0; }
    bool uvlo() const { return (faults & DriverFault::UNDERVOLTAGE) != 0; }
    bool thermalSD() const { return (faults & DriverFault::THERMAL_SHUTDOWN) != 0; }
    bool thermalWarn() const { return (faults & DriverFault::THERMAL_WARNING) != 0; }
    bool stalled() const { return (faults & DriverFault::STALL) != 0; }
    bool cmdErr() const { return (faults & DriverFault::COMMAND_ERROR) != 0; }
    bool shutdownFault() const { return (faults & DriverFault::SHUTDOWN) != 0; }

    /// The state reported while the chip is held in standby (outputs off, not readable)
    static MotorState standby()
    {
        MotorState s;
        s.readable = false;
        return s;
    }
};

/// "OCD UVLO TH_SD TH_WARN STALL CMD" for the faults set (empty when none)
inline void faultNames(uint8_t faults, char* out, unsigned size)
{
    if (out == nullptr || size == 0) return;
    out[0] = '\0';
    static const struct { uint8_t bit; const char* name; } NAMES[] = {
        {DriverFault::OVERCURRENT, "OCD"}, {DriverFault::UNDERVOLTAGE, "UVLO"},
        {DriverFault::THERMAL_SHUTDOWN, "TH_SD"}, {DriverFault::THERMAL_WARNING, "TH_WARN"},
        {DriverFault::STALL, "STALL"}, {DriverFault::COMMAND_ERROR, "CMD"},
    };
    unsigned n = 0;
    for (const auto& e : NAMES) {
        if ((faults & e.bit) == 0) continue;
        for (const char* p = (n == 0) ? "" : " "; *p != '\0' && n + 1 < size; ++p) out[n++] = *p;
        for (const char* p = e.name; *p != '\0' && n + 1 < size; ++p) out[n++] = *p;
    }
    out[n] = '\0';
}

} // namespace Harness
