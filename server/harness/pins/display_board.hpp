/**
 * @file display_board.hpp
 * @brief Harness contract for display board initialization
 *
 * Declares pin structs and init functions for LCD, joystick, and flash.
 * L5 provides the implementations; L4 calls through this harness interface.
 */

#pragma once

namespace Board { class IGPIO; class ISPIBus; }

namespace Harness {

struct LCDPins {
    Board::IGPIO* cs = nullptr;
    Board::IGPIO* dc = nullptr;
    Board::IGPIO* nreset = nullptr;
    Board::IGPIO* te = nullptr;
    Board::ISPIBus* spi = nullptr;
};

struct JoystickPins {
    Board::IGPIO* left = nullptr;
    Board::IGPIO* right = nullptr;
    Board::IGPIO* up = nullptr;
    Board::IGPIO* down = nullptr;
    Board::IGPIO* center = nullptr;
};

struct FlashPins {
    Board::IGPIO* cs = nullptr;
    Board::ISPIBus* spi = nullptr;
};

bool initLCDBoard(LCDPins& pins);
bool initJoystickBoard(JoystickPins& pins);
bool initFlashBoard(FlashPins& pins);

} // namespace Harness
