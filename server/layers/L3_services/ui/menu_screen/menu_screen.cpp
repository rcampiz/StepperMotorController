/**
 * @file menu_screen.cpp
 * @brief Menu screen implementation
 */

#include "ui/menu_screen/menu_screen.hpp"
#include "harness/pins/icanvas.hpp"
#include <string.h>

namespace UI {

// Layout constants (medium font: 8×12 char cell)
static constexpr uint16_t MARGIN_LEFT = 8;
static constexpr uint16_t MARGIN_TOP = 4;
static constexpr uint16_t TITLE_HEIGHT = 18;
static constexpr uint16_t ITEM_HEIGHT = 22;
static constexpr uint16_t ITEM_PADDING = 4;
static constexpr uint8_t  TEXT_SCALE = 3;  // Medium (8×12)

// Colors
static constexpr uint16_t COLOR_TITLE = 0x07FF;      // Cyan
static constexpr uint16_t COLOR_ITEM = 0xFFFF;       // White
static constexpr uint16_t COLOR_ITEM_DISABLED = 0x8410; // Gray
static constexpr uint16_t COLOR_SELECTED_BG = 0x001F;   // Blue
static constexpr uint16_t COLOR_SELECTED_FG = 0xFFFF;   // White
static constexpr uint16_t COLOR_BG = 0x0000;         // Black

MenuScreen::MenuScreen(const char* title)
    : m_itemCount(0)
    , m_selectedIndex(0)
    , m_scrollOffset(0)
    , m_selectionCallback(nullptr)
    , m_needsRedraw(true)
    , m_dirty(true)
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
    return true;
}

void MenuScreen::clearItems()
{
    m_itemCount = 0;
    m_selectedIndex = 0;
    m_scrollOffset = 0;
    m_needsRedraw = true;
    m_dirty = true;
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
}

void MenuScreen::render(Harness::ICanvas& lcd)
{
    // Skip render if nothing changed — menu is static between inputs
    if (!m_dirty) return;
    m_dirty = false;

    uint16_t y = MARGIN_TOP;

    // Draw title if set
    if (m_title[0] != '\0') {
        lcd.drawString(MARGIN_LEFT, y, m_title, COLOR_TITLE, COLOR_BG, TEXT_SCALE);
        y += TITLE_HEIGHT;

        // Draw separator line
        lcd.drawHLine(MARGIN_LEFT, y, Harness::Canvas::WIDTH - 2 * MARGIN_LEFT, COLOR_TITLE);
        y += 4;
    }

    // Calculate visible items
    uint8_t visibleCount = MENU_VISIBLE_ITEMS;
    if (m_title[0] != '\0') {
        visibleCount -= 1; // Reduce for title
    }

    // Draw visible items
    for (uint8_t i = 0; i < visibleCount && (m_scrollOffset + i) < m_itemCount; i++) {
        uint8_t itemIndex = m_scrollOffset + i;
        bool selected = (itemIndex == m_selectedIndex);
        renderItem(lcd, itemIndex, y, selected);
        y += ITEM_HEIGHT;
    }

    // Draw scroll indicators if needed
    if (m_scrollOffset > 0) {
        // Up arrow indicator
        lcd.drawString(Harness::Canvas::WIDTH - 20, MARGIN_TOP + TITLE_HEIGHT + 4, "^", COLOR_TITLE, COLOR_BG, TEXT_SCALE);
    }
    if (m_scrollOffset + visibleCount < m_itemCount) {
        // Down arrow indicator
        lcd.drawString(Harness::Canvas::WIDTH - 20, Harness::Canvas::HEIGHT - 20, "v", COLOR_TITLE, COLOR_BG, TEXT_SCALE);
    }
}

void MenuScreen::renderItem(Harness::ICanvas& lcd, uint8_t itemIndex, uint16_t y, bool selected)
{
    const MenuItem& item = m_items[itemIndex];
    uint16_t itemWidth = Harness::Canvas::WIDTH - 2 * MARGIN_LEFT;

    uint16_t fg, bg;
    if (selected) {
        fg = COLOR_SELECTED_FG;
        bg = COLOR_SELECTED_BG;
        // Draw selection background
        lcd.fillRect(MARGIN_LEFT, y, itemWidth, ITEM_HEIGHT - 2, bg);
    } else {
        fg = item.enabled ? COLOR_ITEM : COLOR_ITEM_DISABLED;
        bg = COLOR_BG;
        // Clear item area
        lcd.fillRect(MARGIN_LEFT, y, itemWidth, ITEM_HEIGHT - 2, bg);
    }

    // Draw item text
    lcd.drawString(MARGIN_LEFT + ITEM_PADDING, y + ITEM_PADDING, item.name, fg, bg, TEXT_SCALE);
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
    uint8_t visibleCount = MENU_VISIBLE_ITEMS;
    if (m_title[0] != '\0') {
        visibleCount -= 1;
    }

    // Scroll up if selection is above visible area
    if (m_selectedIndex < m_scrollOffset) {
        m_scrollOffset = m_selectedIndex;
    }

    // Scroll down if selection is below visible area
    if (m_selectedIndex >= m_scrollOffset + visibleCount) {
        m_scrollOffset = m_selectedIndex - visibleCount + 1;
    }
}

} // namespace UI
