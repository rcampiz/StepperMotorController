/**
 * @file boundary/iuimodeprovider.hpp
 * @brief Consumer port — Services side of the Scheduler::IUIModeProvider harness connection
 *
 * Structurally identical to Scheduler::IUIModeProvider.
 * The harness adapter bridges the provider to this consumer port.
 */

#pragma once

#include "harness/pins/ui_types.hpp"

namespace Services {

class IUIModeProvider {
public:
    virtual ~IUIModeProvider() = default;

    virtual UI::UIMode getMode() const = 0;
    virtual bool setMode(UI::UIMode mode) = 0;
};


} // namespace Services
