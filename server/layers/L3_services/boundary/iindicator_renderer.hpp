/**
 * @file iindicator_renderer.hpp
 * @brief Interface for motion indicator rendering
 *
 * Outbound harness interface: display_task calls through this to
 * IndicatorService (L3 service). Keeps task decoupled from service layer.
 */

#pragma once

#include <stdint.h>

namespace Services {

struct IndicatorParams {
    uint16_t angle_deg;      // 0-359 (0 = up/north, clockwise)
    int8_t   rotation_dir;   // -1 (CCW), 0 (none), 1 (CW)
    bool     has_translation;
    uint8_t  speed_pct;      // 0-100: translation magnitude (arrow length)
    uint8_t  rot_pct;        // 0-100: rotation magnitude (arc around the ring)
};

class IIndicatorRenderer {
public:
    virtual ~IIndicatorRenderer() = default;

    /** @brief Draw/update the motion indicator */
    virtual void draw(const IndicatorParams& p) = 0;

    /** @brief The screen was drawn by someone else: the next draw starts from a clear panel */
    virtual void invalidate() {}

    /** @brief One animation step (Display task, remote mode); draw() only sets the target.
     *  nowMs: the clock, so the easing follows time, not the (variable) frame rate */
    virtual void tick(uint32_t nowMs) { (void)nowMs; }

    /** @brief How far the 2 s CENTER hold to the menu has got, 0-100 (0: not held); the
     *  indicator shows it so the press is seen to register */
    virtual void setHoldProgress(uint8_t pct) { (void)pct; }
};

} // namespace Services
