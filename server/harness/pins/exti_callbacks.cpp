/**
 * @file exti_callbacks.cpp
 * @brief EXTI callback storage
 */

#include "harness/pins/exti_callbacks.hpp"

namespace Harness {

static ExtiCallback s_extiCallbacks[16] = {};

void setExtiCallback(uint8_t line, ExtiCallback cb) {
    if (line < 16) s_extiCallbacks[line] = cb;
}

ExtiCallback getExtiCallback(uint8_t line) {
    return (line < 16) ? s_extiCallbacks[line] : nullptr;
}

} // namespace Harness
