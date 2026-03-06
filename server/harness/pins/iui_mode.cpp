#include "harness/pins/iui_mode.hpp"

namespace Harness {

static IUIModeProvider* s_uiModeProvider = nullptr;

void setUIModeProvider(IUIModeProvider* provider) {
    s_uiModeProvider = provider;
}

IUIModeProvider& uiMode() {
    return *s_uiModeProvider;
}

} // namespace Harness
