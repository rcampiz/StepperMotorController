/**
 * @file screen_manager.cpp
 * @brief Stack-based screen navigation engine implementation
 */

#include "ui/screen_manager/screen_manager.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "ui/theme.hpp"
#include <string.h>

namespace UI {

void ScreenManager::init(IScreen* rootScreen, Services::ICanvas* lcd)
{
    m_lcd = lcd;
    m_depth = 0;

    if (rootScreen != nullptr) {
        m_stack[0] = rootScreen;
        m_depth = 1;
        m_needsClear = true;
        rootScreen->onActivate();
    }
}

void ScreenManager::push(IScreen* screen)
{
    if (screen == nullptr || m_depth >= SCREEN_STACK_MAX) {
        return;
    }

    // Deactivate current screen
    if (m_depth > 0) {
        m_stack[m_depth - 1]->onDeactivate();
    }

    // Push new screen
    m_stack[m_depth] = screen;
    m_depth++;
    m_needsClear = true;
    screen->onActivate();
}

void ScreenManager::pop()
{
    // Never pop the root screen
    if (m_depth <= 1) {
        return;
    }

    // Deactivate current screen
    m_stack[m_depth - 1]->onDeactivate();
    m_depth--;

    // Re-activate revealed screen
    m_needsClear = true;
    m_stack[m_depth - 1]->onActivate();
}

void ScreenManager::popToRoot()
{
    while (m_depth > 1) {
        m_stack[m_depth - 1]->onDeactivate();
        m_depth--;
    }

    if (m_depth > 0) {
        m_needsClear = true;
        m_stack[0]->onActivate();
    }
}

IScreen* ScreenManager::current() const
{
    if (m_depth == 0) {
        return nullptr;
    }
    return m_stack[m_depth - 1];
}

void ScreenManager::render()
{
    IScreen* screen = current();
    if (screen == nullptr || m_lcd == nullptr) {
        return;
    }

    // Transitions and requested full redraws repaint the whole panel: start at the tearing
    // edge so the fill races ahead of the scan instead of being cut by it.
    if (m_needsClear || screen->needsFullRedraw()) {
        Chrome::clearSelection();            // the screen registers its selected row again
        m_lcd->waitForVSync();
        if (screen->usesChrome()) {
            m_lcd->fillScreen(Theme::BG);
            Chrome::drawTitleBand(*m_lcd, screen->title());
            Chrome::drawFooter(*m_lcd, m_depth > 1, screen->centerHint());
            m_drawnHint = screen->centerHint();
            m_statusDirty = true;
        } else {
            m_lcd->fillScreen(Services::Canvas::BLACK);
        }
        m_needsClear = false;
        screen->clearRedrawFlag();
    }

    // A screen whose CENTER does something else now (e.g. EDIT, then SAVE) gets its footer back
    if (screen->usesChrome() && screen->centerHint() != m_drawnHint) {
        Chrome::drawFooter(*m_lcd, m_depth > 1, screen->centerHint());
        m_drawnHint = screen->centerHint();
    }

    if (m_statusDirty && screen->usesChrome()) {
        Chrome::drawStatusBar(*m_lcd, m_status);
        m_statusDirty = false;
    }

    m_renderMs += 50;
    screen->setNowMs(m_clock != nullptr ? m_clock->getTickMs() : m_renderMs);
    screen->render(*m_lcd);

    // The selected row (any chrome screen) animates, Hotline Miami style
    if (screen->usesChrome()) {
        m_frameMs += 50;
        Chrome::animateSelection(*m_lcd, m_clock != nullptr ? m_clock->getTickMs() : m_frameMs);
    }
}

void ScreenManager::setStatus(const StatusLine& s)
{
    if (memcmp(&s, &m_status, sizeof(StatusLine)) == 0) return;
    m_status = s;
    m_statusDirty = true;
}

void ScreenManager::handleInput(JoyDirection dir, bool pressed)
{
    IScreen* screen = current();
    if (screen == nullptr) {
        return;
    }

    InputResult result = screen->handleInput(dir, pressed);

    if (result == InputResult::EXIT_SCREEN) {
        pop();
    }
}

} // namespace UI
