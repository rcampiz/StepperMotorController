#include "harness/pins/oob_estop.hpp"

namespace Harness {

static OobEstopHandler s_handler = nullptr;

void setOobEstopHandler(OobEstopHandler handler) { s_handler = handler; }

void invokeOobEstop()
{
    OobEstopHandler h = s_handler;
    if (h != nullptr) {
        h();
    }
}

} // namespace Harness
