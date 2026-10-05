/**
 * @file trace_stub.cpp
 * @brief Host stand-in for L3_services/infra/trace (which needs the interrupt guard on target)
 */
#include <stdint.h>

namespace Trace {
void setCurrentServiceId(uint8_t) {}
void setCurrentTaskId(uint8_t) {}
}
