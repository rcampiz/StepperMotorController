/**
 * @file image_view_screen.cpp
 * @brief Full-screen flash image viewer implementation
 */

#include "ui/screens/image_view_screen.hpp"
#include "harness/pins/icanvas.hpp"
#include "L3_services/infra/flash_image_service/flash_image_service.hpp"

namespace UI {

// Double-buffer for flash→LCD pipeline (static to avoid stack usage)
static uint8_t s_buf[2][512];

void ImageViewScreen::onActivate()
{
    m_drawn = false;
}

void ImageViewScreen::render(Harness::ICanvas& lcd)
{
    if (m_drawn) return;
    m_drawn = true;

    auto& svc = Services::g_flashImageService;
    if (!svc.isAvailable() || m_slot >= svc.maxSlots()) return;

    constexpr uint32_t CHUNK_SIZE = 512;

    // Start full-screen streaming
    lcd.streamBitmapStart(0, 0, Harness::Canvas::WIDTH, Harness::Canvas::HEIGHT);

    // Read first chunk synchronously
    if (!svc.readSlotChunk(m_slot, 0, s_buf[0], CHUNK_SIZE)) {
        lcd.streamBitmapEnd();
        return;
    }

    int cur = 0;
    uint32_t offset = CHUNK_SIZE;

    // Double-buffered pipeline: DMA1 (SPI2 flash) overlaps DMA2 (SPI1 LCD)
    while (offset < Services::FLASH_IMAGE_SIZE) {
        uint32_t remaining = Services::FLASH_IMAGE_SIZE - offset;
        uint32_t toRead = (remaining < CHUNK_SIZE) ? remaining : CHUNK_SIZE;

        // Start async flash read into other buffer
        if (!svc.readSlotChunkStart(m_slot, offset, s_buf[1 - cur], toRead)) {
            lcd.streamBitmapEnd();
            return;
        }

        // Write current buffer to LCD while flash DMA runs
        lcd.streamBitmapData(s_buf[cur], CHUNK_SIZE);

        // Wait for flash read to complete
        svc.readSlotChunkFinish();

        cur = 1 - cur;
        offset += toRead;
    }

    // Write final chunk
    uint32_t lastSize = Services::FLASH_IMAGE_SIZE - (offset - CHUNK_SIZE);
    if (lastSize > CHUNK_SIZE) lastSize = CHUNK_SIZE;
    lcd.streamBitmapData(s_buf[cur], lastSize);

    lcd.streamBitmapEnd();
}

InputResult ImageViewScreen::handleInput(JoyDirection dir, bool pressed)
{
    if (pressed && dir != JoyDirection::NONE) {
        return InputResult::EXIT_SCREEN;
    }
    return InputResult::HANDLED;
}

} // namespace UI
