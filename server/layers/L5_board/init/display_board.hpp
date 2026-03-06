/**
 * @file display_board.hpp
 * @brief Harness contract for display board initialization
 *
 * Declares pin structs and init functions for LCD, joystick, and flash.
 * L5 provides the implementations; L4 calls through this harness interface.
 */

#pragma once

namespace Harness {

class IGPIO;
class ISPIBus;

struct LCDPins {
    IGPIO* cs = nullptr;
    IGPIO* dc = nullptr;
    IGPIO* nreset = nullptr;
    IGPIO* te = nullptr;
    ISPIBus* spi = nullptr;
};

struct JoystickPins {
    IGPIO* left = nullptr;
    IGPIO* right = nullptr;
    IGPIO* up = nullptr;
    IGPIO* down = nullptr;
    IGPIO* center = nullptr;
};

struct FlashPins {
    IGPIO* cs = nullptr;
    ISPIBus* spi = nullptr;
};

bool initLCDBoard(LCDPins& pins);
bool initJoystickBoard(JoystickPins& pins);
bool initFlashBoard(FlashPins& pins);

} // namespace Harness
