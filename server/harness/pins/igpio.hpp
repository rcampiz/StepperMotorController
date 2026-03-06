/**
 * @file igpio.hpp
 * @brief Abstract GPIO interface for runtime pin control
 *
 * Minimal interface for GPIO write/read operations. L4 drivers use this
 * instead of Board::GPIO directly, decoupling drivers from board hardware.
 * Board::GPIO implements this interface.
 *
 * Configuration methods (setMode, setPull, etc.) are NOT part of this
 * interface — they are board-level concerns handled by L5 init modules.
 */

#pragma once

namespace Harness {

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

} // namespace Harness
