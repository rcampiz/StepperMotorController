/**
 * @file screen_manager.cpp
 * @brief Stack-based screen navigation engine implementation
 */

#include "ui/screen_manager/screen_manager.hpp"
#include "harness/pins/icanvas.hpp"

namespace UI {

void ScreenManager::init(IScreen* rootScreen, Harness::ICanvas* lcd)
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

    // Clear screen on transitions or when screen requests it
    if (m_needsClear || screen->needsFullRedraw()) {
        m_lcd->fillScreen(Harness::Canvas::BLACK);
        m_needsClear = false;
        screen->clearRedrawFlag();
    }

    screen->render(*m_lcd);
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
