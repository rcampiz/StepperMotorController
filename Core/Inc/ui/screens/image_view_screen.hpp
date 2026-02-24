/**
 * @file image_view_screen.hpp
 * @brief Full-screen flash image viewer
 *
 * Streams a stored RGB565 image from NOR flash to the LCD using
 * a double-buffered DMA pipeline (DMA1/SPI2 flash ↔ DMA2/SPI1 LCD).
 * Any button press exits back to the image browser.
 */

#ifndef IMAGE_VIEW_SCREEN_HPP
#define IMAGE_VIEW_SCREEN_HPP

#include "ui/screen.hpp"
#include <stdint.h>

class LCD;

namespace UI {

class ImageViewScreen : public IScreen {
public:
    ScreenType getType() const override { return ScreenType::IMAGE; }
    void render(LCD& lcd) override;
    InputResult handleInput(JoyDirection dir, bool pressed) override;
    void onActivate() override;

    void setSlot(uint32_t slot) { m_slot = slot; }

private:
    uint32_t m_slot = 0;
    bool m_drawn = false;
};

} // namespace UI

#endif // IMAGE_VIEW_SCREEN_HPP
