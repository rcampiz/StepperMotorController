/**
 * @file system_init.hpp
 * @brief System initialization — composition root
 *
 * High-level init functions called from main(). Each function
 * encapsulates one phase of startup. See system_init.cpp for detail.
 */

#ifndef SYSTEM_INIT_HPP
#define SYSTEM_INIT_HPP

namespace System {

/// Clocks (84 MHz PLL), early debug UART, microsecond timer, SystemView
void initHardware();

/// FreeRTOS locks/queues, SPI manager, NOR flash
void initPlatform();

/// Control mode, telemetry, events, command queue, UI mode
void initServices();

/// Encoder, motor, display, comms Task_Init() + adapter wiring
void initTasks();

/// xTaskCreate × 4 (encoder conditional on hardware)
void createTasks();

/// vTaskStartScheduler — never returns
[[noreturn]] void start();

} // namespace System

#endif // SYSTEM_INIT_HPP
