/**
 * @file apply_taps.hpp
 * @brief The one place a traced wrapper (tap) is inserted in front of a real interface.
 *
 * Only harness_connect.cpp and the composition root (Platform locks) call this. Layers
 * never mention ENABLE_INTERFACE_TRACE.
 *
 * Slot: distinct static instance per (Tap, Slot) so one tap type can wrap several objects
 * (SPI1 for motor, SPI1 for LCD, two locks ...). Pass the interface type explicitly when
 * the argument is a concrete class: applyTap<TracedLock, 0, Platform::ILock>(mutex).
 */
#pragma once

#ifdef ENABLE_INTERFACE_TRACE
#include "harness/taps/traced_lock.hpp"
#include "harness/taps/traced_spi_bus.hpp"
#include "harness/taps/traced_motor_driver.hpp"
#include "harness/taps/traced_transport.hpp"
#include "harness/taps/traced_encoder.hpp"
#else
namespace Harness {
class TracedLock; class TracedSPIBus; class TracedMotorDriver; class TracedTransport; class TracedEncoder;
}
#endif

#include "harness/taps/metered_taps.hpp"

namespace Harness {

/// Always-on meter tap (every build): counts the crossings of one harness signal into the
/// harness meter. Apply it first, then applyTap (trace, debug builds) on top of it.
template <typename Tap, int Slot = 0, typename Iface, typename... Args>
Iface& applyMeter(Iface& raw, Args... args) {
    static Tap s_tap(raw, args...);
    return s_tap;
}

template <typename Tap, int Slot = 0, typename Iface>
Iface& applyTap(Iface& raw) {
#ifdef ENABLE_INTERFACE_TRACE
    static Tap s_tap(raw);
    return s_tap;
#else
    return raw;
#endif
}

} // namespace Harness
