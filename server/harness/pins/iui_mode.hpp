/**
 * @file iui_mode.hpp
 * @brief UI mode read/write interface — boundary contract
 *
 * L3 services access UI mode through this interface (via global accessor)
 * without including F_platform/ui/ui_mode.hpp directly.
 */

#pragma once

#include "harness/pins/ui_types.hpp"

namespace Harness {

class IUIModeProvider {
public:
    virtual ~IUIModeProvider() = default;

    virtual UI::UIMode getMode() const = 0;
    virtual bool setMode(UI::UIMode mode) = 0;
};

void setUIModeProvider(IUIModeProvider* provider);
IUIModeProvider& uiMode();

} // namespace Harness
