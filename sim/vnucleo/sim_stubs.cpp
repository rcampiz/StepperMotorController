/**
 * @file sim_stubs.cpp
 * @brief Host versions of the few target-only symbols the L2/L3 code links against.
 */
#include <cstdint>
#include <string>

#include "harness/defs/harness_l2_l3.hpp"

namespace Sim {
extern std::string g_serial;
extern uint32_t g_bootId;
}

namespace Platform {
uint32_t bootId() { return Sim::g_bootId; }
const char* deviceSerial() { return Sim::g_serial.c_str(); }
}

namespace Harness {
// harness_connect.cpp also wires H_L3_L4 / H_L4_L5 / X_L5_L1 to real hardware, so the
// sim provides only the one connection it uses.
const L2L3Harness& connectL2L3(const L3Dispatchers& dispatchers, Protocol::ICommandProcessor& processor)
{
    static L2L3Harness s_h{ dispatchers, processor };
    return s_h;
}
}
