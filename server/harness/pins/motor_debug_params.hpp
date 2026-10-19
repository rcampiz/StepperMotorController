/**
 * @file motor_debug_params.hpp
 * @brief Motor driver diagnostic snapshot -- shared boundary type
 *
 * Chip-neutral fields (decoded by the driver) plus the driver's own diagnostic text, which
 * is opaque above L4 (a register dump for the bring-up record; never parsed by firmware).
 * Used by IMotorControlService (task -> service) and IMotorDriverDispatcher (service -> protocol).
 */

#pragma once

#include <stdint.h>

namespace Harness {

/// Which diagnostic text the driver adds to a MotorDebugParams (Drivers::IMotorDriver::diagnostics)
enum class DriverDiagView : uint8_t {
    NONE = 0,       ///< decoded fields only (fault checks)
    REGISTERS,      ///< the driver's register dump, several lines (DBG:MOTOR)
    SUMMARY         ///< one line of what the last apply left on the chip (DRV:CFG:APPLY)
};

struct MotorDebugParams {
    uint16_t status;          ///< the chip's status word (opaque; decoded fields below)
    uint8_t  faults;          ///< Harness::DriverFault bits, decoded by the driver
    bool     outputsOff;
    bool     moving;
    uint8_t  driveMode;       ///< in force: 0 voltage, 1 current (Harness::DriveMode)
    uint16_t microsteps;      ///< resolution in force, microsteps per full step (driver readback)
    int32_t  absPos;          ///< position, microsteps
    static constexpr unsigned TEXT_MAX = 128;
    char     driverText[TEXT_MAX];   ///< the driver's diagnostic text for the view asked ("" if none)
};

} // namespace Harness
