/**
 * @file ite_sync.hpp
 * @brief Tearing effect sync — boundary contract
 *
 * Abstracts LCD TE pin setup (EXTI0) and DWT cycle-counter-based
 * wait. L3 indicator service calls these without CMSIS includes.
 * L5 provides the implementation.
 */

#pragma once

namespace Harness {

/** Initialize TE sync hardware (EXTI0 on PA0, DWT cycle counter) */
void initTearingEffectSync();

/** Wait for TE rising edge with DWT-based timeout (~18ms) */
void waitForTearingEffect();

} // namespace Harness
