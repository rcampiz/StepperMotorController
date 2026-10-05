/**
 * @file boundary/idisplay_control.hpp
 * @brief Enable/disable the Display task's work without suspending the task (L3-internal interface)
 *
 * Used by the debug service (LCD_DISABLE) from the Comms task. The Display task keeps
 * running its loop; tick() returns immediately while disabled, so no LCD/SPI1 traffic.
 */
#pragma once

namespace Services {

class IDisplayControl {
public:
    virtual ~IDisplayControl() = default;
    virtual void setDisplayEnabled(bool enabled) = 0;
    virtual bool isDisplayEnabled() const = 0;
};

} // namespace Services
