/**
 * @file display.hpp
 * @brief Umbrella header — DisplayGroup (like MotionGroup)
 */

#ifndef L3_DISPLAY_HPP
#define L3_DISPLAY_HPP

#include "L3_services/display/display_dispatch/display_dispatch.hpp"

namespace Services {

struct DisplayGroup {
    DisplayDispatch& dispatch;
};

extern DisplayGroup display;

} // namespace Services

#endif // L3_DISPLAY_HPP
