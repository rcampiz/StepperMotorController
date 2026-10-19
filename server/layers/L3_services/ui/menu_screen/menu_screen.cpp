/**
 * @file menu_screen.cpp
 * @brief Menu screen implementation
 */

#include "ui/menu_screen/menu_screen.hpp"
#include "ui/chrome/chrome.hpp"
#include "ui/theme.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include <string.h>

namespace UI {

// Layout: items are 18 px rows under the chrome; the selected one carries a pink bar.
// An item is "> Name" or "  Name" in one text box, so moving the selection repaints just
// the two rows that changed and no pixel twice (the old fill-then-text flashed).
static constexpr uint16_t ITEM_X = Theme::MARGIN;
static constexpr uint16_t ITEM_W = Theme::WIDTH - 2 * Theme::MARGIN;
static constexpr uint16_t ITEM_HEIGHT = 18;
static constexpr uint16_t ITEM_PAD_X = 6;
static constexpr uint16_t ITEM_PAD_Y = 2;
static constexpr uint16_t ITEMS_Y = Theme::CONTENT_Y + 2;

namespace {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t mix565(uint16_t a, uint16_t b, int t256) {
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return static_cast<uint16_t>(((ar + (br - ar) * t256 / 256) << 11) |
                                 ((ag + (bg - ag) * t256 / 256) << 5) | (ab + (bb - ab) * t256 / 256));
}

int16_t sinApprox(int32_t a1024, int16_t amp) {             // a sine from two parabolas
    int32_t a = a1024 & 1023;
    const int32_t sign = (a < 512) ? 1 : -1;
    a &= 511;
    return static_cast<int16_t>(sign * a * (512 - a) * amp / 65536);
}

// The selected item's animation (frames at 20 Hz; the row repaints every other frame)
constexpr uint16_t SWAY_FRAMES = 32;          // 1.6 s side to side
constexpr int16_t  SWAY_PX = 3;
constexpr uint16_t FLASH_FRAMES = 12;         // a new colour every 0.6 s
constexpr uint16_t STRIPE_W = 7;              // the bar's diagonal stripes
constexpr uint16_t STRIPE_DARK = rgb(196, 18, 150);
constexpr uint16_t SHADOW = rgb(46, 0, 70);   // hard shadow, 2 px right and down
struct Ink { uint16_t top, bottom; };
constexpr Ink FACES[3] = {
    {rgb(255, 255, 255), rgb(255, 210, 240)},  // white
    {rgb(255, 250, 120), rgb(255, 160, 20)},   // yellow to amber
    {rgb(120, 255, 255), rgb(60, 150, 255)},   // cyan to blue
};

} // namespace

MenuScreen::MenuScreen(const char* title)
    : m_itemCount(0)
    , m_selectedIndex(0)
    , m_scrollOffset(0)
    , m_selectionCallback(nullptr)
    , m_needsRedraw(true)
    , m_dirty(true)
    , m_fullPending(true)
    , m_prevSelected(0)
{
    memset(m_title, 0, sizeof(m_title));
    memset(m_items, 0, sizeof(m_items));

    if (title != nullptr) {
        setTitle(title);
    }
}

void MenuScreen::setTitle(const char* title)
{
    if (title != nullptr) {
        strncpy(m_title, title, MENU_ITEM_NAME_LEN - 1);
        m_title[MENU_ITEM_NAME_LEN - 1] = '\0';
    } else {
        m_title[0] = '\0';
    }
    m_needsRedraw = true;
    m_dirty = true;
    m_fullPending = true;
}

bool MenuScreen::addItem(const char* name, MenuCallback callback, bool enabled)
{
    if (m_itemCount >= MENU_MAX_ITEMS || name == nullptr) {
        return false;
    }

    MenuItem& item = m_items[m_itemCount];
    strncpy(item.name, name, MENU_ITEM_NAME_LEN - 1);
    item.name[MENU_ITEM_NAME_LEN - 1] = '\0';
    item.callback = callback;
    item.enabled = enabled;

    m_itemCount++;
    m_needsRedraw = true;
    m_dirty = true;
    m_fullPending = true;
    return true;
}

void MenuScreen::clearItems()
{
    m_itemCount = 0;
    m_selectedIndex = 0;
    m_scrollOffset = 0;
    m_needsRedraw = true;
    m_dirty = true;
    m_fullPending = true;
}

void MenuScreen::setSelectedIndex(uint8_t index)
{
    if (index < m_itemCount) {
        m_selectedIndex = index;
        ensureSelectionVisible();
        m_needsRedraw = true;
        m_dirty = true;
    }
}

void MenuScreen::onActivate()
{
    m_needsRedraw = true;
    m_dirty = true;
    m_fullPending = true;
}

uint8_t MenuScreen::visibleCount() const
{
    return static_cast<uint8_t>((Theme::CONTENT_BOTTOM - ITEMS_Y) / ITEM_HEIGHT);
}

uint16_t MenuScreen::itemY(uint8_t visibleSlot) const
{
    return static_cast<uint16_t>(ITEMS_Y + visibleSlot * ITEM_HEIGHT);
}

void MenuScreen::render(Services::ICanvas& lcd)
{
    m_animTick++;
    if (!m_dirty) {
        // Nothing moved: only the selected item animates, every other frame
        if ((m_animTick & 1) == 0 && m_selectedIndex < m_itemCount) renderSelectedAnimated(lcd);
        return;
    }
    m_dirty = false;

    const uint8_t visible = visibleCount();
    if (m_fullPending) {
        m_fullPending = false;
        uint8_t shown = 0;
        for (uint8_t i = 0; i < visible && (m_scrollOffset + i) < m_itemCount; i++, shown++) {
            renderItem(lcd, static_cast<uint8_t>(m_scrollOffset + i), (m_scrollOffset + i) == m_selectedIndex);
        }
        // Scroll marks at the right edge, small font
        lcd.drawString(Theme::WIDTH - 6, ITEMS_Y, m_scrollOffset > 0 ? "^" : " ",
                       Theme::CYAN, Theme::BG, Theme::FONT_SMALL);
        bool more = (m_scrollOffset + visible) < m_itemCount;
        lcd.drawString(Theme::WIDTH - 6, static_cast<uint16_t>(itemY(visible) - 10), more ? "v" : " ",
                       Theme::CYAN, Theme::BG, Theme::FONT_SMALL);
        // The floor under a short list
        auto floorTop = static_cast<uint16_t>(itemY(shown) + 6);
        if (!more && floorTop + 30 < Theme::CONTENT_BOTTOM) {
            Chrome::drawDeck(lcd, floorTop, Theme::CONTENT_BOTTOM);
        }
        m_prevSelected = m_selectedIndex;
        return;
    }

    // Selection moved within the visible window: repaint the row that lost the bar and the
    // row that gained it
    if (m_prevSelected != m_selectedIndex && m_prevSelected < m_itemCount) {
        renderItem(lcd, m_prevSelected, false);
    }
    renderItem(lcd, m_selectedIndex, true);
    m_prevSelected = m_selectedIndex;
}

void MenuScreen::renderItem(Services::ICanvas& lcd, uint8_t itemIndex, bool selected)
{
    if (itemIndex < m_scrollOffset || itemIndex >= m_scrollOffset + visibleCount()) return;
    const MenuItem& item = m_items[itemIndex];
    uint16_t y = itemY(static_cast<uint8_t>(itemIndex - m_scrollOffset));

    char text[MENU_ITEM_NAME_LEN + 2];
    text[0] = selected ? '>' : ' ';
    text[1] = ' ';
    strncpy(text + 2, item.name, MENU_ITEM_NAME_LEN - 1);
    text[MENU_ITEM_NAME_LEN + 1] = '\0';

    uint16_t fg = selected ? Theme::TEXT : (item.enabled ? Theme::TEXT : Theme::DIM);
    uint16_t bg = selected ? Theme::PINK : Theme::BG;
    if (selected && lcd.mediumGlyphRows('A') != nullptr) {
        renderSelectedAnimated(lcd);
    } else {
        Chrome::textBox(lcd, ITEM_X, y, ITEM_W, ITEM_HEIGHT - 2, ITEM_PAD_X, ITEM_PAD_Y, text, fg, bg);
    }
    lcd.fillRect(ITEM_X, static_cast<uint16_t>(y + ITEM_HEIGHT - 2), ITEM_W, 1, Theme::GRID);
    lcd.fillRect(ITEM_X, static_cast<uint16_t>(y + ITEM_HEIGHT - 1), ITEM_W, 1, Theme::BG);
}

void MenuScreen::renderSelectedAnimated(Services::ICanvas& lcd)
{
    if (m_selectedIndex < m_scrollOffset || m_selectedIndex >= m_scrollOffset + visibleCount()) return;
    if (lcd.mediumGlyphRows('A') == nullptr) return;           // no font to compose with
    const MenuItem& item = m_items[m_selectedIndex];
    const uint16_t y = itemY(static_cast<uint8_t>(m_selectedIndex - m_scrollOffset));
    constexpr uint16_t ROWS = ITEM_HEIGHT - 2;                 // the bar; the rule under it stays

    char text[MENU_ITEM_NAME_LEN + 2];
    text[0] = '>';
    text[1] = ' ';
    strncpy(text + 2, item.name, MENU_ITEM_NAME_LEN - 1);
    text[MENU_ITEM_NAME_LEN + 1] = '\0';
    const auto len = static_cast<int16_t>(strlen(text));

    // This frame's sway, flash colour and stripe phase
    const int16_t x0 = static_cast<int16_t>(ITEM_PAD_X + SWAY_PX +
                                            sinApprox(static_cast<int32_t>(m_animTick) * 1024 / SWAY_FRAMES, SWAY_PX));
    const Ink& ink = FACES[(m_animTick / FLASH_FRAMES) % 3];
    const uint16_t phase = static_cast<uint16_t>(m_animTick / 2);

    // Is (gx, gy) inked in the text, slanted (the top leans right by up to 2 px)?
    auto inked = [&](int16_t gx, int16_t gy) -> bool {
        if (gy < 0 || gy >= Theme::MED_H) return false;
        gx = static_cast<int16_t>(gx - (Theme::MED_H - 1 - gy) / 4);
        if (gx < 0 || gx >= len * Theme::MED_W) return false;
        const uint8_t* rows = lcd.mediumGlyphRows(text[gx / Theme::MED_W]);
        return rows != nullptr && (rows[gy] & (0x80u >> (gx % Theme::MED_W))) != 0;
    };

    static uint8_t row[ITEM_W * 2];
    lcd.streamBitmapStart(ITEM_X, y, ITEM_W, ROWS);
    for (uint16_t r = 0; r < ROWS; r++) {
        const int16_t gy = static_cast<int16_t>(r - ITEM_PAD_Y);
        const uint16_t face = mix565(ink.top, ink.bottom, gy <= 0 ? 0 : gy * 256 / (Theme::MED_H - 1));
        for (uint16_t x = 0; x < ITEM_W; x++) {
            const int16_t gx = static_cast<int16_t>(x - x0);
            uint16_t c;
            if (inked(gx, gy)) c = face;
            else if (inked(static_cast<int16_t>(gx - 2), static_cast<int16_t>(gy - 2))) c = SHADOW;
            else c = (((x + r + phase) / STRIPE_W) & 1u) ? STRIPE_DARK : Theme::PINK;
            row[x * 2] = static_cast<uint8_t>(c >> 8);
            row[x * 2 + 1] = static_cast<uint8_t>(c & 0xFF);
        }
        lcd.streamBitmapData(row, sizeof row);
    }
    lcd.streamBitmapEnd();
}

InputResult MenuScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (!pressed) {
        return InputResult::HANDLED;
    }

    switch (dir) {
        case JoyDirection::UP:
            if (m_selectedIndex > 0) {
                m_selectedIndex--;
                ensureSelectionVisible();
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::DOWN:
            if (m_selectedIndex < m_itemCount - 1) {
                m_selectedIndex++;
                ensureSelectionVisible();
                m_dirty = true;
            }
            return InputResult::HANDLED;

        case JoyDirection::CENTER:
            if (m_itemCount > 0) {
                const MenuItem& item = m_items[m_selectedIndex];
                if (item.enabled) {
                    bool stayInMenu = true;

                    // Call item callback if set
                    if (item.callback != nullptr) {
                        stayInMenu = item.callback(m_selectedIndex);
                    }

                    // Call general selection callback if set
                    if (m_selectionCallback != nullptr) {
                        stayInMenu = m_selectionCallback(m_selectedIndex) && stayInMenu;
                    }

                    if (!stayInMenu) {
                        return InputResult::EXIT_SCREEN;
                    }
                }
            }
            return InputResult::HANDLED;

        case JoyDirection::LEFT:
            return InputResult::EXIT_SCREEN;

        default:
            return InputResult::UNHANDLED;
    }
}

void MenuScreen::ensureSelectionVisible()
{
    const uint8_t visible = visibleCount();
    const uint8_t before = m_scrollOffset;

    if (m_selectedIndex < m_scrollOffset) {
        m_scrollOffset = m_selectedIndex;
    }
    if (m_selectedIndex >= m_scrollOffset + visible) {
        m_scrollOffset = static_cast<uint8_t>(m_selectedIndex - visible + 1);
    }
    if (m_scrollOffset != before) {
        m_fullPending = true;          // every row moved
    }
}

} // namespace UI
