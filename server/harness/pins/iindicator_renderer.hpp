/**
 * @file iindicator_renderer.hpp
 * @brief Interface for motion indicator rendering
 *
 * Outbound harness interface: display_task calls through this to
 * IndicatorService (L3 service). Keeps task decoupled from service layer.
 */

#pragma once

#include <stdint.h>

namespace Harness {

struct IndicatorParams {
    uint16_t angle_deg;      // 0-359 (0 = up/north, clockwise)
    int8_t   rotation_dir;   // -1 (CCW), 0 (none), 1 (CW)
    bool     has_translation;
};

class IIndicatorRenderer {
public:
    virtual ~IIndicatorRenderer() = default;

    /** @brief Draw/update the motion indicator */
    virtual void draw(const IndicatorParams& p) = 0;
};

} // namespace Harness
