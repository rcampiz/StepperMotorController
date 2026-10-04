/**
 * @file harness_template.hpp
 * @brief Harness wiring template — connects provider to consumer via adapter + optional tap
 *
 * The harness<>() template implements the Ports & Adapters pattern:
 *   Provider --[optional Tap]--> Adapter --> Consumer
 *
 * Usage in harness_wiring_map.cpp:
 *   return harness<Board::ISPIBus, Drivers::ISPIBus, SPIAdapter, TracedSPIBus>(raw);
 *
 * Template parameters:
 *   Provider  — provider interface type (e.g., Board::ISPIBus)
 *   Consumer  — consumer port type (e.g., Drivers::ISPIBus)
 *   Adapter   — adapter class (implements Consumer, wraps Provider)
 *   Tap       — traced wrapper class (implements Provider, wraps Provider), or NoTap
 */

#pragma once

namespace Harness {

/// Convert between identically-laid-out POD types across namespace boundaries.
/// Used by adapters when provider and consumer define structurally identical types.
template<typename To, typename From>
To convert_pod(const From& src) {
    static_assert(sizeof(To) == sizeof(From), "POD layout size mismatch");
    To result;
    __builtin_memcpy(&result, &src, sizeof(To));
    return result;
}

/// Sentinel type: no tap/trace on this harness connection
struct NoTap {};

/// Primary template: insert a tap (traced wrapper) between provider and adapter
template<typename Tap, typename Provider>
struct TapApplier {
    static Provider& apply(Provider& raw) {
        static Tap s_tap(raw);
        return s_tap;
    }
};

/// Specialization: NoTap — passthrough (no traced wrapper)
template<typename Provider>
struct TapApplier<NoTap, Provider> {
    static Provider& apply(Provider& raw) {
        return raw;
    }
};

/**
 * @brief Wire a harness connection: Provider --[Tap]--> Adapter --> Consumer
 *
 * Creates a static adapter (and optional tap) on first call.
 * Returns a reference to the consumer port, valid for the lifetime of the program.
 *
 * @tparam Provider  Provider interface (e.g., Board::ISPIBus)
 * @tparam Consumer  Consumer port (e.g., Drivers::ISPIBus)
 * @tparam Adapter   Adapter class in Harness:: (wraps Provider, implements Consumer)
 * @tparam Tap       Traced wrapper class or NoTap
 * @param  raw       Reference to the raw provider object
 * @return Reference to the consumer port (static lifetime)
 */
template<typename Provider, typename Consumer, typename Adapter, typename Tap>
Consumer& harness(Provider& raw) {
#ifdef ENABLE_INTERFACE_TRACE
    Provider& tapped = TapApplier<Tap, Provider>::apply(raw);
    static Adapter s_adapter(tapped);
    return s_adapter;
#else
    static Adapter s_adapter(raw);
    return s_adapter;
#endif
}

} // namespace Harness
