/**
 * @file display_scratch.hpp
 * @brief Scratch memory the Display task's renderers share instead of each keeping its own.
 *
 * DISPLAY TASK ONLY, NOT REENTRANT. Everything that draws a screen (ScreenManager::render,
 * the screens, Chrome, the remote-mode IndicatorService::tick) runs on the Display task, one
 * call after another, so one buffer serves them all as long as no user holds it across a call
 * into another user. Nothing here locks: a second task must never take these.
 *
 * - ScratchRow: one RGB565 panel row (480 B) for code that composes a row and streams it
 *   (streamBitmapStart / Data / End). Take it as a local lease for the length of the stream:
 *       ScratchRow lease; uint8_t* row = lease.data();
 *   A lease taken while another is held (a nested use, which would corrupt the outer row) is
 *   counted in ScratchRow::collisions(); the host tests check it stays 0.
 *
 * - SharedTitleMask: the 2-bit title masks of the synthwave saver and of the E-STOP page.
 *   Only one of those pages shows at a time; each claims the buffer before it reads or builds
 *   its mask, and claim() says whether the buffer still holds that owner's mask (when it
 *   does not, the owner rebuilds it: the saver's "built" flag and the E-STOP page's word
 *   cache both start over).
 *
 * The canvas below (L4 LCD driver) copies a row before streamBitmapData returns, so a row
 * buffer is free again as soon as the call returns. The L4/L5 buffers (LCD invert buffers,
 * the SPI DMA fill buffer) are not here: the Comms task reaches them too.
 */
#pragma once

#include "ui/theme.hpp"
#include <stddef.h>
#include <stdint.h>

namespace UI {

class ScratchRow {
public:
    static constexpr size_t BYTES = static_cast<size_t>(Theme::WIDTH) * 2u;   ///< one RGB565 row

    ScratchRow() {
        if (depth()++ != 0u) collisionCount()++;
    }
    ~ScratchRow() { depth()--; }
    ScratchRow(const ScratchRow&) = delete;
    ScratchRow& operator=(const ScratchRow&) = delete;

    uint8_t* data() const { return storage(); }

    /// Leases taken while another was held, since boot (tests: must stay 0)
    static uint32_t collisions() { return collisionCount(); }

private:
    // Inline functions: one instance across the image (zero-initialised, no guard)
    static uint8_t* storage() {
        static uint8_t s_row[BYTES];
        return s_row;
    }
    static uint8_t& depth() {
        static uint8_t s_depth;
        return s_depth;
    }
    static uint32_t& collisionCount() {
        static uint32_t s_collisions;
        return s_collisions;
    }
};

class SharedTitleMask {
public:
    /// Sized for the larger user, the saver's title (MH * MW4 = 5875 B in saver_screen.cpp);
    /// each user static_asserts that its mask fits
    static constexpr size_t BYTES = 5876;

    /// INDICATOR_ROWS: the motion indicator's frame, many rows per stream call (scratch: it
    /// keeps nothing, and the title masks rebuild after it; remote mode only)
    enum class Owner : uint8_t { NONE = 0, SAVER_TITLE, ESTOP_TITLE, INDICATOR_ROWS };

    /// Take the buffer for `who`. True when it still holds who's mask (nobody else claimed
    /// it since who last did); false when who must rebuild its mask before reading it.
    static bool claim(Owner who) {
        Owner& o = owner();
        const bool kept = (o == who);
        o = who;
        return kept;
    }

    static uint8_t* data() {
        static uint8_t s_mask[BYTES];
        return s_mask;
    }

private:
    static Owner& owner() {
        static Owner s_owner = Owner::NONE;
        return s_owner;
    }
};

} // namespace UI
