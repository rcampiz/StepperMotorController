/**
 * @file encoder.hpp
 * @brief Quadrature encoder interface placeholder
 *
 * Pin assignments (timer encoder mode capable):
 *   EA  - PA0 (TIM2_CH1 / TIM5_CH1) - Quadrature A
 *   EB  - PA1 (TIM2_CH2 / TIM5_CH2) - Quadrature B
 *   EZ  - PC4                       - Index pulse (optional)
 *   nG  - PC2                       - Config pin (directly active low)
 *   G   - PC3                       - Config pin
 *
 * Implementation will use TIM2 or TIM5 in encoder mode for
 * hardware quadrature decoding.
 */

#ifndef ENCODER_HPP
#define ENCODER_HPP

#include "stm32f401xe.h"
#include "board/board_pins.hpp"

class Encoder {
public:
    Encoder() {
        // Placeholder - pins defined but not initialized yet
    }

    void init() {
        // TODO: Implement encoder initialization
        // 1. Enable GPIO clocks
        // 2. Configure EA/EB as alternate function for timer
        // 3. Configure EZ as input with interrupt (optional)
        // 4. Configure nG/G as needed for encoder type
        // 5. Set up TIM2 or TIM5 in encoder mode
    }

    int32_t read() const {
        // TODO: Return timer counter value
        return 0;
    }

    void reset() {
        // TODO: Reset counter to zero
    }

    bool indexPulse() const {
        // TODO: Read EZ pin state
        return false;
    }

    // Configuration pins for encoder type selection
    void setGatingConfig(bool g, bool ng) {
        (void)g;
        (void)ng;
        // TODO: Set G and nG pins
    }

private:
    // Timer to use (TIM2 has 32-bit counter)
    static constexpr TIM_TypeDef* TIMER = TIM2;
};

#endif // ENCODER_HPP
