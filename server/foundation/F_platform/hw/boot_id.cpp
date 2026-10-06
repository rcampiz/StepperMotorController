/**
 * @file boot_id.cpp
 * @brief Per-boot id and device serial for the STM32F401 (no hardware RNG)
 */
#include "F_platform/boundary/boot_id.hpp"
#include "X_vendor/CMSIS/stm32f401xe.h"

namespace {

constexpr uint32_t UID_ADDR = 0x1FFF7A10UL;   // 96-bit unique device ID (RM0368 §31.1)

uint32_t fnv1a(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; ++i) {
        h ^= (v >> (8 * i)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

uint32_t s_bootId = 0;
bool     s_bootIdReady = false;
char     s_serial[9] = {0};

} // namespace

namespace Platform {

uint32_t bootId()
{
    if (!s_bootIdReady) {
        const volatile uint32_t* uid = reinterpret_cast<const volatile uint32_t*>(UID_ADDR);
        if ((CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) == 0) {
            CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
            DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
        }
        uint32_t h = 2166136261u;
        h = fnv1a(h, uid[0]);
        h = fnv1a(h, uid[1]);
        h = fnv1a(h, uid[2]);
        h = fnv1a(h, DWT->CYCCNT);        // cycles since reset at first call: varies per boot
        h = fnv1a(h, SysTick->VAL);
        h = fnv1a(h, RCC->CSR);           // reset-cause flags
        s_bootId = h ? h : 1u;
        s_bootIdReady = true;
    }
    return s_bootId;
}

const char* deviceSerial()
{
    if (s_serial[0] == '\0') {
        const volatile uint32_t* uid = reinterpret_cast<const volatile uint32_t*>(UID_ADDR);
        uint32_t folded = uid[0] ^ uid[1] ^ uid[2];
        static const char hex[] = "0123456789ABCDEF";
        for (int i = 7; i >= 0; --i) {
            s_serial[i] = hex[folded & 0xFu];
            folded >>= 4;
        }
        s_serial[8] = '\0';
    }
    return s_serial;
}

} // namespace Platform
