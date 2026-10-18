/**
 * @file screen_care_screen.cpp
 * @brief Screen Care rows and the explanation under them.
 */

#include "ui/screens/screen_care_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include <stdio.h>

namespace UI {

static constexpr uint16_t FIELDS_Y = Theme::CONTENT_Y + 4;
static constexpr uint16_t HINT_Y   = FIELDS_Y + 4 * Chrome::ROW_H + 4;
static constexpr uint16_t TEXT_Y   = HINT_Y + 16;

static uint16_t fieldY(uint8_t i) { return static_cast<uint16_t>(FIELDS_Y + i * Chrome::ROW_H); }

void ScreenCareScreen::onActivate()
{
    m_needsRedraw = true;
    m_full = true;
    m_selectedField = 0;
    m_editing = false;
    m_hintDirty = true;
}

static uint8_t indexOfAfter(uint32_t ms)
{
    for (uint8_t i = 0; i < SAVER_AFTER_CHOICE_COUNT; i++) if (SAVER_AFTER_CHOICES_MS[i] == ms) return i;
    return 2;
}

static uint8_t indexOfMinutes(uint8_t m)
{
    for (uint8_t i = 0; i < RECOVERY_MINUTE_CHOICE_COUNT; i++) if (RECOVERY_MINUTE_CHOICES[i] == m) return i;
    return 2;
}

void ScreenCareScreen::renderField(Services::ICanvas& lcd, uint8_t field)
{
    char val[14];
    const char* label = "";
    uint16_t color = 0;
    val[0] = '\0';
    switch (field) {
        case FIELD_SAVER_AFTER:
            label = "Saver after";
            if (m_settings->saverAfterMs == 0) snprintf(val, sizeof val, "never");
            else snprintf(val, sizeof val, "%lu min", static_cast<unsigned long>(m_settings->saverAfterMs / 60000u));
            break;
        case FIELD_SAVER_STYLE:
            label = "Saver style";
            snprintf(val, sizeof val, "%s", m_settings->saverStyle == SaverStyle::DRIFT ? "drift" : "blank");
            break;
        case FIELD_RECOVERY_MIN:
            label = "Recovery";
            snprintf(val, sizeof val, "%u min", m_settings->recoveryMinutes);
            break;
        case FIELD_RUN:
            label = "Run now";
            snprintf(val, sizeof val, "recovery");
            color = Theme::YELLOW;
            break;
        default: break;
    }
    Chrome::RowState st = Chrome::RowState::NORMAL;
    if (field == m_selectedField) st = m_editing ? Chrome::RowState::EDITING : Chrome::RowState::SELECTED;
    Chrome::fieldRow(lcd, fieldY(field), label, val, st, color);
}

void ScreenCareScreen::render(Services::ICanvas& lcd)
{
    if (m_full) {
        m_full = false;
        m_rowsDirty = 0xFFFF;
        static const char* const LINES[] = {
            "IPS panel: a still image left for",
            "hours leaves a ghost of itself.",
            "",
            "The saver keeps nothing still after",
            "the quiet time; any move wakes it.",
            "",
            "Recovery drives every pixel through",
            "full-scale colours for the time set,",
            "then comes back here. Run it when a",
            "ghost stays on a blank page.",
        };
        for (uint8_t i = 0; i < 10; i++) {
            lcd.blitTextLine(Theme::MARGIN, static_cast<uint16_t>(TEXT_Y + i * 11),
                             static_cast<uint16_t>(Theme::WIDTH - 2 * Theme::MARGIN), Theme::SMALL_H,
                             LINES[i], Theme::DIM, Theme::BG);
        }
        Chrome::drawCorners(lcd, Theme::MARGIN, static_cast<uint16_t>(TEXT_Y - 4),
                            static_cast<uint16_t>(Theme::WIDTH - 2 * Theme::MARGIN), 118, 8, Theme::GRID_MAJ);
    }
    for (uint8_t i = 0; i < NUM_FIELDS; i++) {
        if (m_rowsDirty & (1u << i)) renderField(lcd, i);
    }
    m_rowsDirty = 0;
    if (m_hintDirty) {
        m_hintDirty = false;
        Chrome::hintLine(lcd, HINT_Y, m_editing ? "LEFT/RIGHT adjust   CENTER done" : "", Theme::DIM);
    }
}

void ScreenCareScreen::adjust(int8_t delta)
{
    switch (m_selectedField) {
        case FIELD_SAVER_AFTER: {
            int8_t i = static_cast<int8_t>(indexOfAfter(m_settings->saverAfterMs) + delta);
            if (i < 0) i = 0;
            if (i >= SAVER_AFTER_CHOICE_COUNT) i = SAVER_AFTER_CHOICE_COUNT - 1;
            m_settings->saverAfterMs = SAVER_AFTER_CHOICES_MS[i];
            break;
        }
        case FIELD_SAVER_STYLE:
            m_settings->saverStyle = (m_settings->saverStyle == SaverStyle::DRIFT) ? SaverStyle::BLANK : SaverStyle::DRIFT;
            break;
        case FIELD_RECOVERY_MIN: {
            int8_t i = static_cast<int8_t>(indexOfMinutes(m_settings->recoveryMinutes) + delta);
            if (i < 0) i = 0;
            if (i >= RECOVERY_MINUTE_CHOICE_COUNT) i = RECOVERY_MINUTE_CHOICE_COUNT - 1;
            m_settings->recoveryMinutes = RECOVERY_MINUTE_CHOICES[i];
            break;
        }
        default: break;
    }
    markRow(m_selectedField);
}

InputResult ScreenCareScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) return InputResult::HANDLED;
    switch (dir) {
        case JoyDirection::UP:
        case JoyDirection::DOWN: {
            if (m_editing) { m_editing = false; m_hintDirty = true; }
            uint8_t prev = m_selectedField;
            if (dir == JoyDirection::UP && m_selectedField > 0) m_selectedField--;
            if (dir == JoyDirection::DOWN && m_selectedField < NUM_FIELDS - 1) m_selectedField++;
            markRow(prev); markRow(m_selectedField);
            return InputResult::HANDLED;
        }
        case JoyDirection::LEFT:
            if (m_editing) { adjust(-1); return InputResult::HANDLED; }
            return InputResult::EXIT_SCREEN;
        case JoyDirection::RIGHT:
            if (m_editing) { adjust(1); return InputResult::HANDLED; }
            if (m_selectedField != FIELD_RUN) { m_editing = true; m_hintDirty = true; markRow(m_selectedField); }
            return InputResult::HANDLED;
        case JoyDirection::CENTER:
            if (m_editing) { m_editing = false; m_hintDirty = true; markRow(m_selectedField); return InputResult::HANDLED; }
            if (m_selectedField == FIELD_RUN) { if (m_run != nullptr) m_run(); return InputResult::HANDLED; }
            m_editing = true; m_hintDirty = true; markRow(m_selectedField);
            return InputResult::HANDLED;
        default:
            return InputResult::UNHANDLED;
    }
}

} // namespace UI
