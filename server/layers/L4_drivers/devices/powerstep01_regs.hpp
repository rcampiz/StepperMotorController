/**
 * @file powerstep01_regs.hpp
 * @brief powerSTEP01 register addresses (datasheet DS9836), L4 only.
 *
 * Shared by the driver's settings engine (powerstep01_settings.hpp) and the virtual Nucleo's
 * fake chip. Nothing above L4 names a register (docs/design/DRIVER_BOUNDARY.md, step 3).
 */
#pragma once

#include <stdint.h>

namespace Drivers {
namespace PowerStepReg {
constexpr uint8_t ABS_POS    = 0x01;
constexpr uint8_t EL_POS     = 0x02;
constexpr uint8_t MARK       = 0x03;
constexpr uint8_t SPEED      = 0x04;
constexpr uint8_t ACC        = 0x05;
constexpr uint8_t DEC        = 0x06;
constexpr uint8_t MAX_SPEED  = 0x07;
constexpr uint8_t MIN_SPEED  = 0x08;
constexpr uint8_t KVAL_HOLD  = 0x09;   // TVAL_HOLD in current mode
constexpr uint8_t KVAL_RUN   = 0x0A;   // TVAL_RUN
constexpr uint8_t KVAL_ACC   = 0x0B;   // TVAL_ACC
constexpr uint8_t KVAL_DEC   = 0x0C;   // TVAL_DEC
constexpr uint8_t INT_SPEED  = 0x0D;
constexpr uint8_t T_FAST     = 0x0E;   // current mode (ST_SLP in voltage mode)
constexpr uint8_t TON_MIN    = 0x0F;   // current mode (FN_SLP_ACC in voltage mode)
constexpr uint8_t TOFF_MIN   = 0x10;   // current mode (FN_SLP_DEC in voltage mode)
constexpr uint8_t ADC_OUT    = 0x12;   // 5-bit reading of the ADCIN pin (read-only)
constexpr uint8_t OCD_TH     = 0x13;
constexpr uint8_t STALL_TH   = 0x14;
constexpr uint8_t FS_SPD     = 0x15;
constexpr uint8_t STEP_MODE  = 0x16;
constexpr uint8_t ALARM_EN   = 0x17;
constexpr uint8_t GATECFG1   = 0x18;   // gate drive: written only by init, 0 after a chip reset
constexpr uint8_t GATECFG2   = 0x19;
constexpr uint8_t CONFIG     = 0x1A;
constexpr uint8_t STATUS     = 0x1B;
} // namespace PowerStepReg
} // namespace Drivers
