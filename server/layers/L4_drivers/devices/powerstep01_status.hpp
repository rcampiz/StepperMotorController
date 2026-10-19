/**
 * @file powerstep01_status.hpp
 * @brief The powerSTEP01 STATUS register, decoded into the chip-neutral MotorState.
 *
 * The only place that knows the bit layout (the driver and the virtual Nucleo's fake chip
 * both use it). Flags marked active-low read 0 when the condition is present.
 */
#pragma once

#include <stdint.h>
#include "harness/pins/motor_state.hpp"

namespace Drivers {

inline Harness::MotorState decodePowerStepStatus(uint16_t raw)
{
    Harness::MotorState s;
    s.raw = raw;
    s.outputsOff = (raw & (1u << 0)) != 0;                          // HiZ
    s.moving = (raw & (1u << 1)) == 0;                              // BUSY, active low
    s.forward = (raw & (1u << 4)) != 0;                             // DIR
    s.phase = static_cast<Harness::MotionPhase>((raw >> 5) & 0x3u); // MOT_STATUS
    uint8_t f = 0;
    if ((raw & (1u << 7)) != 0) f |= Harness::DriverFault::COMMAND_ERROR;   // CMD_ERROR
    if ((raw & (1u << 9)) == 0) f |= Harness::DriverFault::UNDERVOLTAGE;    // UVLO, active low
    const unsigned th = (raw >> 11) & 0x3u;                                 // TH_STATUS
    if (th >= 1u) f |= Harness::DriverFault::THERMAL_WARNING;
    if (th >= 2u) f |= Harness::DriverFault::THERMAL_SHUTDOWN;              // bridge or device
    if ((raw & (1u << 13)) == 0) f |= Harness::DriverFault::OVERCURRENT;    // OCD, active low
    if ((raw & (1u << 14)) == 0 || (raw & (1u << 15)) == 0) f |= Harness::DriverFault::STALL;   // STALL_A/B
    s.faults = f;
    return s;
}

} // namespace Drivers
