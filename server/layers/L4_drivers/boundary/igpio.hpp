/**
 * @file boundary/igpio.hpp
 * @brief Consumer port — Drivers side of the Board::IGPIO harness connection
 *
 * Structurally identical to Board::IGPIO.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

namespace Drivers {

class IGPIO {
public:
    virtual ~IGPIO() = default;

    /**
     * @brief Write a digital value to the pin
     * @param value true for HIGH, false for LOW
     */
    virtual void write(bool value) = 0;

    /**
     * @brief Read the current state of the pin
     * @return true if pin is HIGH, false if LOW
     */
    virtual bool read() const = 0;
};

} // namespace Drivers
