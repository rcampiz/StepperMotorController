/**
 * @file encoder_board.hpp
 * @brief Harness contract for encoder board initialization
 *
 * Declares pin struct and init function for the encoder subsystem.
 * L5 provides the implementation; L4 calls through this harness interface.
 */

#pragma once

namespace Harness {

class ITimerCounter;
class IEncoderSampler;

struct EncoderPins {
    ITimerCounter* timer = nullptr;
    IEncoderSampler* sampler = nullptr;
};

/**
 * @brief Initialize encoder board GPIO, EXTI, and timer
 *
 * Configures encoder pins and timer for quadrature mode.
 * Implemented by L5 board layer.
 *
 * @param pins Output struct populated with timer interface pointer
 * @return true on success
 */
bool initEncoderBoard(EncoderPins& pins);

} // namespace Harness
