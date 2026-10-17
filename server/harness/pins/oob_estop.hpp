/**
 * @file oob_estop.hpp
 * @brief Out-of-band emergency stop pin: the USART2 ISR -> the motor service (Phase 4 step 1)
 *
 * Byte 0x18 (CAN) in the host stream is recognised in the USART2 interrupt before it reaches
 * the ring (docs/PROTOCOL.md section 5). The ISR calls invokeOobEstop(); the composition
 * root installs the handler (MotorService::oobEstopFromIsr), which drives STBY/RESET low,
 * latches, and leaves the SPI HardHiZ to the Motor task. ISR context: the handler may only
 * write GPIO registers, atomics and the microsecond clock.
 */
#pragma once

namespace Harness {

using OobEstopHandler = void (*)();

void setOobEstopHandler(OobEstopHandler handler);
void invokeOobEstop();                      // ISR context; no-op until a handler is installed

} // namespace Harness
