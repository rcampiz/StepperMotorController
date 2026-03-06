/**
 * @file motor_task.hpp
 * @brief Motor control task for powerSTEP01
 *
 * Receives commands via queue, drives motor, updates telemetry.
 * Priority: Highest (tskIDLE_PRIORITY + 4)
 */

#ifndef MOTOR_TASK_HPP
#define MOTOR_TASK_HPP

#include "harness/pins/motor_debug_params.hpp"
#include "harness/pins/imotor_command_sink.hpp"
#include "harness/pins/ischeduler.hpp"
#include "harness/pins/iqueue.hpp"
#include "harness/pins/iclock.hpp"
#include "harness/pins/imotor_driver.hpp"
#include <stdint.h>

namespace Harness { class IMotorTaskControl; class IMotorControlService; }

namespace Scheduler {

// Task configuration
constexpr uint32_t MOTOR_TASK_STACK_SIZE = 1024; // 4096 bytes (sysid + trim + supervisor + telemetry snapshot)
constexpr uint32_t MOTOR_TASK_PRIORITY = 4;
constexpr size_t MOTOR_CMD_QUEUE_DEPTH = 8;

// Types moved to Harness namespace — aliases for backward compatibility
using Harness::MotorCmdType;
using Harness::MotorCommand;
using Harness::MotorDebugParams;

/**
 * @brief Initialize motor task resources
 *
 * Stores injected dependencies and initializes motor configuration.
 * Queue and driver are created/wired by the composition root.
 * Call before vTaskStartScheduler().
 *
 * @param driver     Motor driver (wired by composition root)
 * @param cmdQueue   Command queue (receive side; send side via IMotorCommandSink)
 * @param clock      System clock (for delays and tick timing)
 * @param controller Motor control service (L3, wired by composition root)
 * @param scheduler  Scheduler for suspend/resume (injected, no Platform::resources)
 * @return true on success
 */
bool MotorTask_Init(Harness::IMotorDriver& driver,
                    Harness::IQueue<Harness::MotorCommand, MOTOR_CMD_QUEUE_DEPTH>& cmdQueue,
                    Harness::IClock& clock,
                    Harness::IMotorControlService& controller,
                    Harness::IScheduler& scheduler);

/**
 * @brief Motor task entry point
 * @param pvParameters Unused
 */
void vMotorTask(void *pvParameters);

/**
 * @brief Send command to motor task
 * @param cmd Command to send
 * @param timeout Ticks to wait if queue full (0 = don't wait)
 * @return true if command queued successfully
 */
bool MotorTask_SendCommand(const MotorCommand &cmd, uint32_t timeout = 0);

/**
 * @brief Convenience: send move command
 * @param steps Steps to move (negative = reverse)
 * @return true if queued
 */
bool MotorTask_Move(int32_t steps);

/**
 * @brief Convenience: send stop command
 * @param hard true for hard stop, false for soft stop
 * @return true if queued
 */
bool MotorTask_Stop(bool hard = false);

/**
 * @brief Read debug info from powerSTEP01
 * @param info Output structure
 * @return true if successful
 */
bool MotorTask_GetDebugInfo(MotorDebugParams& info);

/**
 * @brief Suspend motor task (for SPI debug commands)
 */
void MotorTask_Suspend();

/**
 * @brief Resume motor task after suspend
 */
void MotorTask_Resume();

/**
 * @brief Reinitialize motor driver
 *
 * Call after LCD_DISABLE to reinitialize powerSTEP01 with correct settings.
 * This ensures proper SPI communication and register configuration.
 */
void MotorTask_Reinit();

/**
 * @brief Apply motor configuration to powerSTEP01 driver
 *
 * Reads the current configuration from motion.motorConfig and applies
 * all settings to the powerSTEP01 registers (KVAL, thresholds,
 * motion params, alarm enables).
 *
 * @return true if successful
 */
bool MotorTask_ApplyConfig();

/**
 * @brief Set microstep mode with Hi-Z safety sequence
 *
 * Hi-Z → write STEP_MODE → readback verify → SoftStop re-enable.
 * Must be called when motor is idle (not busy).
 * Caller must suspend motor task first (MotorTask_Suspend).
 *
 * @param mode Step mode 0-7 (0=full, 7=1/128)
 * @param readback Output: verified step mode read back from hardware
 * @return true if write and verify succeeded
 */
bool MotorTask_SetStepModeSafe(uint8_t mode, uint8_t& readback);

// Task handle (for suspend/resume via IScheduler)
extern Harness::TaskHandle g_motorTaskHandle;

/**
 * @brief Get IMotorTaskControl interface implemented by this task
 * @return Pointer to static IMotorTaskControl instance (valid after init)
 */
Harness::IMotorTaskControl* MotorTask_GetControlInterface();

} // namespace Scheduler

#endif // MOTOR_TASK_HPP
