/**
 * @file bringup_task.cpp
 * @brief Hardware bring-up and test task implementation
 */

#include "F_platform/tasks/bringup_task.hpp"
#include "L5_board/board_pins.hpp"
#include "L5_board/uart.hpp"

#include "L4_drivers/spi/spi_bus.hpp"
#include "L4_drivers/spi/spi_manager.hpp"
#include "L4_drivers/devices/powerstep01.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/flash_nor.hpp"
#include "L4_drivers/devices/joystick.hpp"

// External UART for debug output (defined in main.cpp)
extern UART* usb;

// Helper to print hex byte
static void printHex(UART* uart, uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    char buf[3] = { hex[(val >> 4) & 0xF], hex[val & 0xF], '\0' };
    uart->print(buf);
}

void bringupTask(void* pvParameters) {
    (void)pvParameters;

    // Wait for system to stabilize
    vTaskDelay(pdMS_TO_TICKS(500));

    usb->println("\r\n=== Hardware Bring-up ===\r\n");

    // -------------------------------------------------------------------------
    // Get SPI buses from global manager
    // -------------------------------------------------------------------------
    usb->println("[SPI] Using SPI from g_spiManager...");
    SPIBus spi1(*g_spiManager.getSPI1());  // SPI1: Motor + LCD (hardware SPI)
    SPIBus spi2(*g_spiManager.getSPI2());  // SPI2: Flash (bit-bang)

    // -------------------------------------------------------------------------
    // Initialize and test powerSTEP01
    // -------------------------------------------------------------------------
    usb->println("\r\n[IHM03A1] Initializing powerSTEP01...");
    PowerSTEP01 stepper(spi1);
    stepper.init();

    PowerSTEP01::Status status = stepper.getStatus();
    usb->print("[IHM03A1] Status: 0x");
    printHex(usb, status.raw >> 8);
    printHex(usb, status.raw & 0xFF);
    usb->println("");

    usb->print("[IHM03A1] HiZ: ");
    usb->println(status.hiZ() ? "yes" : "no");
    usb->print("[IHM03A1] Busy: ");
    usb->println(status.busy() ? "yes" : "no");
    usb->print("[IHM03A1] UVLO: ");
    usb->println(status.uvlo() ? "FAULT" : "ok");
    usb->print("[IHM03A1] Thermal: ");
    usb->println(status.thermalSD() ? "SHUTDOWN" : "ok");
    usb->print("[IHM03A1] OCD: ");
    usb->println(status.ocd() ? "FAULT" : "ok");

    // -------------------------------------------------------------------------
    // Initialize and test LCD
    // -------------------------------------------------------------------------
    usb->println("\r\n[LCD] Initializing display...");
    LCD lcd(spi1);
    lcd.init();

    usb->println("[LCD] Drawing test pattern...");
    lcd.drawTestPattern();
    usb->println("[LCD] Test pattern displayed (color bars)");

    // -------------------------------------------------------------------------
    // Initialize and test SPI Flash
    // -------------------------------------------------------------------------
    usb->println("\r\n[FLASH] Initializing SPI NOR flash...");
    SPIFlash flash(spi2);
    flash.init();

    SPIFlash::JEDEC_ID jedec = flash.readJEDEC();
    usb->print("[FLASH] JEDEC ID: Manufacturer=0x");
    printHex(usb, jedec.manufacturer);
    usb->print(" Type=0x");
    printHex(usb, jedec.memoryType);
    usb->print(" Capacity=0x");
    printHex(usb, jedec.capacity);
    usb->println("");

    // Decode common manufacturers
    usb->print("[FLASH] Manufacturer: ");
    switch (jedec.manufacturer) {
        case 0xEF: usb->println("Winbond"); break;
        case 0xC2: usb->println("Macronix"); break;
        case 0x20: usb->println("Micron"); break;
        case 0x01: usb->println("Spansion/Cypress"); break;
        case 0xBF: usb->println("SST/Microchip"); break;
        default:   usb->println("Unknown"); break;
    }

    // -------------------------------------------------------------------------
    // Initialize joystick
    // -------------------------------------------------------------------------
    usb->println("\r\n[JOYSTICK] Initializing 5-way joystick...");
    Joystick joystick;
    usb->println("[JOYSTICK] Ready - monitoring in main loop\r\n");

    // -------------------------------------------------------------------------
    // Main monitoring loop
    // -------------------------------------------------------------------------
    usb->println("=== Bring-up Complete ===\r\n");
    usb->println("Joystick: Press any direction to test");
    usb->println("          (monitoring every 100ms)\r\n");

    Joystick::Direction lastDir = Joystick::Direction::None;
    TickType_t lastWake = xTaskGetTickCount();

    while (1) {
        // Poll joystick
        Joystick::Direction dir = joystick.readDirection();
        if (dir != lastDir) {
            if (dir != Joystick::Direction::None) {
                usb->print("[JOYSTICK] ");
                usb->println(joystick.directionName(dir));

                // Visual feedback on LCD
                uint16_t color;
                switch (dir) {
                    case Joystick::Direction::Left:   color = LCD::RED;     break;
                    case Joystick::Direction::Right:  color = LCD::GREEN;   break;
                    case Joystick::Direction::Up:     color = LCD::BLUE;    break;
                    case Joystick::Direction::Down:   color = LCD::YELLOW;  break;
                    case Joystick::Direction::Center: color = LCD::WHITE;   break;
                    default:                          color = LCD::BLACK;   break;
                }
                lcd.fillRect(80, 80, 80, 80, color);
            } else {
                lcd.fillRect(80, 80, 80, 80, LCD::BLACK);
            }
            lastDir = dir;
        }

        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));
    }
}
