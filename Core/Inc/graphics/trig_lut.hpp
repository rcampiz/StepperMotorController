/**
 * @file trig_lut.hpp
 * @brief Q15 fixed-point sin/cos lookup table and rotation helpers
 *
 * 256-entry quarter-wave sin table. Full circle = 1024 angle units.
 * Q15 format: 32767 represents 1.0, -32768 represents -1.0.
 *
 * No RTOS. No driver dependencies. Pure math utility.
 */

#ifndef TRIG_LUT_HPP
#define TRIG_LUT_HPP

#include <stdint.h>

namespace Graphics {

// Quarter-wave sin table (256 entries, indices 0-255 cover 0 to 90 degrees)
// sin(i * 90 / 256) * 32767, rounded
static constexpr int16_t SIN_TABLE[256] = {
        0,   201,   402,   603,   804,  1005,  1206,  1407,
     1608,  1809,  2009,  2210,  2410,  2611,  2811,  3012,
     3212,  3412,  3612,  3811,  4011,  4210,  4410,  4609,
     4808,  5007,  5205,  5404,  5602,  5800,  5998,  6195,
     6393,  6590,  6786,  6983,  7179,  7375,  7571,  7767,
     7962,  8157,  8351,  8545,  8739,  8933,  9126,  9319,
     9512,  9704,  9896, 10087, 10278, 10469, 10659, 10849,
    11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
    12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
    15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673,
    16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357,
    19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
    20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
    23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143,
    24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198,
    26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
    27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
    28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534,
    29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783,
    30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
    31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
    32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382,
    32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717,
    32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766
};

/**
 * @brief Sin in Q15 fixed-point
 * @param angle Angle in 1024-unit circle (0-1023, wraps)
 * @return sin(angle) in Q15 (-32768 to 32767)
 */
inline int16_t sin_q15(uint16_t angle) {
    angle &= 0x03FF;  // Wrap to 0-1023
    uint16_t quadrant = angle >> 8;   // 0-3
    uint16_t index = angle & 0xFF;    // 0-255

    switch (quadrant) {
        case 0: return SIN_TABLE[index];
        case 1: return SIN_TABLE[255 - index];
        case 2: return static_cast<int16_t>(-SIN_TABLE[index]);
        case 3: return static_cast<int16_t>(-SIN_TABLE[255 - index]);
        default: return 0;  // unreachable
    }
}

/**
 * @brief Cos in Q15 fixed-point
 * @param angle Angle in 1024-unit circle (0-1023, wraps)
 * @return cos(angle) in Q15
 */
inline int16_t cos_q15(uint16_t angle) {
    return sin_q15(static_cast<uint16_t>(angle + 256));  // cos = sin(angle + 90)
}

/**
 * @brief Convert degrees (0-359) to 1024-unit angle
 * @param degrees Angle in degrees
 * @return Angle in 1024-unit circle
 */
inline uint16_t deg_to_angle1024(uint16_t degrees) {
    // (degrees * 1024 + 180) / 360 — rounded
    return static_cast<uint16_t>((static_cast<uint32_t>(degrees) * 1024 + 180) / 360);
}

/**
 * @brief Q15 fixed-point multiply: (a * b) >> 15
 * @param a First operand (Q15)
 * @param b Second operand (Q15)
 * @return Product in Q15
 */
inline int32_t q15_mul(int16_t a, int16_t b) {
    return (static_cast<int32_t>(a) * b) >> 15;
}

/**
 * @brief Rotate point (x, y) around origin by angle
 * @param x Input X coordinate (pixels, not Q15)
 * @param y Input Y coordinate (pixels, not Q15)
 * @param angle Angle in 1024-unit circle
 * @param[out] rx Rotated X (pixels)
 * @param[out] ry Rotated Y (pixels)
 */
inline void rotate_point(int16_t x, int16_t y, uint16_t angle,
                         int16_t& rx, int16_t& ry) {
    int16_t c = cos_q15(angle);
    int16_t s = sin_q15(angle);
    rx = static_cast<int16_t>(q15_mul(x, c) - q15_mul(y, s));
    ry = static_cast<int16_t>(q15_mul(x, s) + q15_mul(y, c));
}

} // namespace Graphics

#endif // TRIG_LUT_HPP
