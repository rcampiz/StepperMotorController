/**
 * @file display_dispatch.hpp
 * @brief L3 implementation of IDisplayDispatcher
 *
 * Remote rendering delegates to injected IRemoteDisplay harness pin.
 * UI mode queries use F_platform/ui foundation (accessible from any layer).
 */

#pragma once

#include "L3_services/boundary/idisplay_dispatcher.hpp"
#include "L3_services/boundary/iremote_display.hpp"

namespace Services {

class DisplayDispatch : public Services::IDisplayDispatcher {
public:
    DisplayDispatch() = default;
    explicit DisplayDispatch(Services::IRemoteDisplay* remote)
        : m_remoteDisplay(remote) {}

    void setRemoteDisplay(Services::IRemoteDisplay* r) { m_remoteDisplay = r; }

    bool displayIsRemoteMode() override;
    uint8_t displayGetMode() override;
    Harness::ServiceStatus displaySetMode(uint8_t mode) override;
    void displayClear(uint16_t color) override;
    void displayText(uint16_t x, uint16_t y, const char* text,
                      uint16_t fg, uint16_t bg) override;
    void displayRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t color, bool filled) override;
    void displayLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      uint16_t color) override;
    void displayBitmap(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                        const uint8_t* data, uint32_t len) override;
    void displayIndicator(uint16_t angle, int8_t rotation, bool translation, uint8_t speedPct, uint8_t rotPct) override;
    void displayEstopHold(uint8_t pct) override;
    bool displayPanelRegister(uint8_t reg, const uint8_t* data, uint8_t len) override;
    bool displayStreamStart(uint16_t x, uint16_t y, uint16_t w, uint16_t h) override;
    void displayStreamData(const uint8_t* data, uint32_t len) override;
    void displayStreamEnd() override;

private:
    Services::IRemoteDisplay* m_remoteDisplay = nullptr;
};

} // namespace Services
