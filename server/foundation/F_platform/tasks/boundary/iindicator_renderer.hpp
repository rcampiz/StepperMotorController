/**
 * @file boundary/iindicatorrenderer.hpp
 * @brief Consumer port — Scheduler side of the Services::IIndicatorRenderer harness connection
 *
 * Structurally identical to Services::IIndicatorRenderer.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include <stdint.h>

namespace Scheduler {

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

} // namespace Scheduler
