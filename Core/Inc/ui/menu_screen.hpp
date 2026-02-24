/**
 * @file menu_screen.hpp
 * @brief Menu screen for list-based navigation
 *
 * Displays a scrollable list of menu items. Up/Down navigates,
 * Center selects, Left goes back.
 */

#ifndef MENU_SCREEN_HPP
#define MENU_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

// Forward declaration
class LCD;

namespace UI {

// Menu configuration
constexpr uint8_t MENU_MAX_ITEMS = 16;      ///< Max menu items
constexpr uint8_t MENU_ITEM_NAME_LEN = 24;  ///< Max item name length
constexpr uint8_t MENU_VISIBLE_ITEMS = 12;  ///< Items visible at once (at medium scale)

/**
 * @brief Menu item callback type
 * @param itemIndex Index of selected item
 * @return true to stay in menu, false to exit
 */
using MenuCallback = bool (*)(uint8_t itemIndex);

/**
 * @brief Single menu item
 */
struct MenuItem {
    char name[MENU_ITEM_NAME_LEN];  ///< Display name
    MenuCallback callback;          ///< Selection callback (optional)
    bool enabled;                   ///< Item is selectable
};

/**
 * @brief Menu screen for list-based navigation
 *
 * Displays a vertical list of items with selection highlight.
 * Supports scrolling when items exceed visible area.
 *
 * Input:
 *   - UP/DOWN: Navigate items
 *   - CENTER: Select current item
 *   - LEFT: Exit menu (returns EXIT_SCREEN)
 */
class MenuScreen : public IScreen {
public:
    /**
     * @brief Construct a menu screen
     * @param title Menu title (displayed at top)
     */
    explicit MenuScreen(const char* title = nullptr);

    // IScreen interface
    ScreenType getType() const override { return ScreenType::MENU; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;
    bool needsFullRedraw() const override { return m_needsRedraw; }
    void clearRedrawFlag() override { m_needsRedraw = false; }

    /**
     * @brief Set menu title
     * @param title Title string (copied)
     */
    void setTitle(const char* title);

    /**
     * @brief Add an item to the menu
     * @param name Item display name
     * @param callback Selection callback (can be nullptr)
     * @param enabled Whether item is selectable (default true)
     * @return true if item added successfully
     */
    bool addItem(const char* name, MenuCallback callback = nullptr, bool enabled = true);

    /**
     * @brief Remove all items from menu
     */
    void clearItems();

    /**
     * @brief Get current selection index
     * @return Selected item index
     */
    uint8_t getSelectedIndex() const { return m_selectedIndex; }

    /**
     * @brief Set selection index
     * @param index Item index to select
     */
    void setSelectedIndex(uint8_t index);

    /**
     * @brief Get number of items
     * @return Item count
     */
    uint8_t getItemCount() const { return m_itemCount; }

    /**
     * @brief Set callback for any selection
     * @param callback Called when any item selected (after item's own callback)
     */
    void setSelectionCallback(MenuCallback callback) { m_selectionCallback = callback; }

private:
    char m_title[MENU_ITEM_NAME_LEN];
    MenuItem m_items[MENU_MAX_ITEMS];
    uint8_t m_itemCount;
    uint8_t m_selectedIndex;
    uint8_t m_scrollOffset;  // First visible item index
    MenuCallback m_selectionCallback;
    bool m_needsRedraw;
    bool m_dirty;  // Per-frame render gate (skip render when nothing changed)

    void ensureSelectionVisible();
    void renderItem(LCD& lcd, uint8_t itemIndex, uint16_t y, bool selected);
};

} // namespace UI

#endif // MENU_SCREEN_HPP
