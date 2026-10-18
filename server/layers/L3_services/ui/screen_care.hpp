/**
 * @file screen_care.hpp
 * @brief Settings for the panel's care: the screen saver and the retention-recovery run.
 *
 * Owned by the DisplayService, edited by the Screen Care screen, read by the saver. The
 * panel is an IPS LCD: a static image for hours leaves a ghost, so the saver keeps nothing
 * still and the recovery run drives every pixel through full-scale colours for a while.
 */
#pragma once

#include <stdint.h>

namespace UI {

enum class SaverStyle : uint8_t { DRIFT = 0, BLANK = 1 };

struct ScreenCareSettings {
    uint32_t   saverAfterMs = 2u * 60u * 1000u;   ///< 0 = never
    SaverStyle saverStyle = SaverStyle::DRIFT;
    uint8_t    recoveryMinutes = 5;
};

/// Choices the care screen steps through
constexpr uint32_t SAVER_AFTER_CHOICES_MS[] = { 0, 60000u, 120000u, 300000u, 600000u };
constexpr uint8_t  SAVER_AFTER_CHOICE_COUNT = 5;
constexpr uint8_t  RECOVERY_MINUTE_CHOICES[] = { 1, 2, 5, 10, 20 };
constexpr uint8_t  RECOVERY_MINUTE_CHOICE_COUNT = 5;

} // namespace UI
