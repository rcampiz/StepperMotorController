#include "F_platform/tasks/boundary/iui_mode.hpp"

namespace Scheduler {

static IUIModeProvider* s_uiModeProvider = nullptr;

void setUIModeProvider(IUIModeProvider* provider) {
    s_uiModeProvider = provider;
}

IUIModeProvider& uiMode() {
    return *s_uiModeProvider;
}

} // namespace Scheduler
