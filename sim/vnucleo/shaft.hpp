/**
 * @file shaft.hpp
 * @brief The virtual motor shaft the fake encoder reads: whichever fake driver chip turns it.
 */
#pragma once

namespace Sim {

class IShaft {
public:
    virtual ~IShaft() = default;
    /// Where the rotor is, full steps from power-on (what the encoder sees)
    virtual double shaftFullSteps() = 0;
    /// The chip has a step-sync output the board counts (the powerSTEP01's BUSY/SYNC into TIM3)
    virtual bool hasSyncOutput() const = 0;
    /// Pulses on that output so far (0 without one)
    virtual double syncPulses() { return 0.0; }
};

} // namespace Sim
