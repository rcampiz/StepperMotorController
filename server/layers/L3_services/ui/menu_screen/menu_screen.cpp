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
    if (!m_dirty) return;                    // the selected item animates via Chrome
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
    if (selected) {
        Chrome::selectRow(lcd, ITEM_X, y, ITEM_W, ITEM_HEIGHT - 2, text, "");    // animated (Hotline)
    } else {
        Chrome::unselectRowAt(y);
        Chrome::textBox(lcd, ITEM_X, y, ITEM_W, ITEM_HEIGHT - 2, ITEM_PAD_X, ITEM_PAD_Y, text, fg, bg);
    }
    lcd.fillRect(ITEM_X, static_cast<uint16_t>(y + ITEM_HEIGHT - 2), ITEM_W, 1, Theme::GRID);
    lcd.fillRect(ITEM_X, static_cast<uint16_t>(y + ITEM_HEIGHT - 1), ITEM_W, 1, Theme::BG);
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
