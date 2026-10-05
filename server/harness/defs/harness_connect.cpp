/**
 * @file harness_connect.cpp
 * @brief connect*() implementations — the only place taps are inserted into harnesses.
 *
 * Each connect function builds one static harness struct on first call and returns it.
 * No heap, no registration tables. ENABLE_INTERFACE_TRACE never appears outside harness/.
 */
#include "harness/defs/harness_l4_l5.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/defs/harness_l1_l2.hpp"
#include "harness/defs/harness_l5_l1_bytes.hpp"
#include "harness/pins/motor_driver_init.hpp"
#include "harness/pins/display_driver_init.hpp"
#include "harness/pins/encoder_driver_init.hpp"
#include "harness/pins/config_store_init.hpp"
#include "harness/pins/debug_access_init.hpp"
#include "harness/defs/harness_l2_l3.hpp"
#include "harness/pins/exti_callbacks.hpp"
#include "harness/pins/imicrosecond_clock.hpp"
#include "harness/pins/spi_init.hpp"
#include "harness/taps/apply_taps.hpp"

namespace Harness {

// ============================================================================
// H_L4_L5
// ============================================================================
const L4L5Harness& connectL4L5()
{
    static bool         s_connected = false;
    static MotorPins    s_motor;
    static LCDPins      s_lcd;
    static JoystickPins s_joystick;
    static FlashPins    s_flash;
    static EncoderPins  s_encoder;
    static L4L5Harness  s_h{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &setExtiCallback };

    if (!s_connected) {
        s_connected = true;

        if (initMotorBoard(s_motor) && s_motor.spi != nullptr) {
            s_motor.spi = &applyTap<TracedSPIBus, 0>(*s_motor.spi);
            s_h.motor = &s_motor;
        }
        if (initLCDBoard(s_lcd) && s_lcd.spi != nullptr) {
            s_lcd.spi = &applyTap<TracedSPIBus, 1>(*s_lcd.spi);
            s_h.lcd = &s_lcd;
        }
        if (initJoystickBoard(s_joystick)) {
            s_h.joystick = &s_joystick;
        }
        if (initFlashBoard(s_flash)) {
            s_h.flash = &s_flash;
        }
        if (initEncoderBoard(s_encoder)) {
            s_h.encoder = &s_encoder;
        }
        s_h.internalFlash = &internalFlash();
        s_h.boardDebug = &boardDebug();
    }
    return s_h;
}

} // namespace Harness

namespace Harness {

// ============================================================================
// H_L3_L4
// ============================================================================
const L3L4Harness& connectL3L4(Platform::IClock& clock)
{
    static bool       s_connected = false;
    static L3L4Harness s_h{ nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };

    if (!s_connected) {
        s_connected = true;

        // Motor driver (mandatory). The trace tap is applied here, once.
        Drivers::IMotorDriver* motor = nullptr;
        if (Drivers::initMotorDriver(motor) && motor != nullptr) {
            s_h.motor = &applyTap<TracedMotorDriver>(*motor);
        }

        // Display devices (LCD mandatory for the display subsystem; others optional)
        Drivers::DisplayDriverHandle* display = nullptr;
        if (Drivers::initDisplayDrivers(display) && display != nullptr) {
            s_h.canvas   = display->lcd;
            s_h.joystick = display->joystick;
            s_h.flash    = display->norFlash;
        }

        // Encoder (optional)
        Drivers::IEncoderHardware* encoder = nullptr;
        Platform::IMicrosecondClock* usClock = Harness::microsecondClock();
        if (usClock != nullptr && Drivers::initEncoderDrivers(clock, *usClock, 1000, encoder) && encoder != nullptr) {
            s_h.encoderHw = &applyTap<TracedEncoder>(*encoder);
        }

        // Raw debug access (optional)
        Drivers::IDebugAccess* dbg = nullptr;
        if (Drivers::initDebugAccess(dbg)) {
            s_h.debug = dbg;
        }

        // Persistent config store (internal flash)
        Drivers::IConfigStore* store = nullptr;
        if (Drivers::initConfigStore(store)) {
            s_h.configStore = store;
        }
    }
    return s_h;
}

} // namespace Harness

namespace Harness {

// ============================================================================
// H_L1_L2
// ============================================================================
const L1L2Harness& connectL1L2(Transport::ITransport& rawTransport)
{
    static L1L2Harness s_h{ applyTap<TracedTransport>(rawTransport) };
    return s_h;
}

} // namespace Harness

namespace Harness {

// ============================================================================
// H_L2_L3
// ============================================================================
const L2L3Harness& connectL2L3(const L3Dispatchers& dispatchers, Protocol::ICommandProcessor& processor)
{
    static L2L3Harness s_h{ dispatchers, processor };
    return s_h;
}

} // namespace Harness

namespace Harness {

// ============================================================================
// X_L5_L1 (documented exception: host command bytes)
// ============================================================================
const L5L1ByteHarness* connectL5L1Bytes(CommsChannel type)
{
    static CommsBoardHw s_hw;
    static const L5L1ByteHarness* s_h = nullptr;
    if (s_h == nullptr) {
        if (!initCommsBoard(s_hw, type) || s_hw.channel == nullptr) {
            return nullptr;
        }
        static L5L1ByteHarness s_harness{ *s_hw.channel };
        s_h = &s_harness;
    }
    return s_h;
}

} // namespace Harness

namespace Harness {

// ============================================================================
// Platform SPI locks -> L5 SPI manager (power net; the TracedLock taps live here)
// ============================================================================
bool connectSPIManager(Platform::ILock& spi1Raw, Platform::ILock& spi2Raw)
{
    return initSPIManager(applyTap<TracedLock, 0>(spi1Raw),
                          applyTap<TracedLock, 1>(spi2Raw));
}

} // namespace Harness
