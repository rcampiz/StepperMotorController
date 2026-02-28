/**
 * @file main.cpp
 * @brief Stepper Motor Controller — entry point
 *
 * FreeRTOS-based stepper motor controller with:
 *   - Raspberry Pi upstream communication (VCP UART or RTT)
 *   - powerSTEP01 motor control via X-NUCLEO-IHM03A1
 *   - LCD display via X-NUCLEO-GFX01M2
 *   - Quadrature encoder feedback with index pulse
 *
 * Tasks:
 *   - MotorTask (Pri 4): Motor command processing
 *   - CommsTask (Pri 3): Upstream communication
 *   - EncoderTask (Pri 2): Encoder sampling at 100Hz
 *   - DisplayTask (Pri 1): LCD refresh at 20Hz
 *
 * All initialization detail is in system_init.cpp.
 */

#include "system_init.hpp"

int main(void)
{
    System::initHardware();   // Clocks (84 MHz), early debug UART, microsecond timer
    System::initPlatform();   // FreeRTOS locks/queues, SPI manager, NOR flash
    System::initServices();   // Control mode, telemetry, events, command queue, UI
    System::initTasks();      // Encoder, motor, display, comms task init + adapters
    System::createTasks();    // xTaskCreate × 4
    System::start();          // vTaskStartScheduler — never returns
}
