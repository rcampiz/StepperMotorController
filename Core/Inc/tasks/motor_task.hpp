/**
 * @file motor_task.hpp
 * @brief Motor control task for powerSTEP01
 *
 * Receives commands via queue, drives motor, updates telemetry.
 * Priority: Highest (tskIDLE_PRIORITY + 4)
 */

#ifndef MOTOR_TASK_HPP
#define MOTOR_TASK_HPP

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdint.h>

namespace Tasks {

// Task configuration
constexpr uint32_t MOTOR_TASK_STACK_SIZE = 256;
constexpr UBaseType_t MOTOR_TASK_PRIORITY = tskIDLE_PRIORITY + 4;
constexpr size_t MOTOR_CMD_QUEUE_DEPTH = 8;

/**
 * @brief Motor command types
 */
enum class MotorCmdType : uint8_t {
  // Motion commands
  Move,     // Relative move: param1 = steps (signed)
  GoTo,     // Absolute move: param1 = position
  Run,      // Continuous: param1 = speed, param2 = direction (0=rev, 1=fwd)
  SoftStop, // Decelerate to stop
  HardStop, // Immediate stop
  SoftHiZ,  // Decelerate then Hi-Z
  HardHiZ,  // Immediate Hi-Z
  GoHome,   // Return to home position
  GoMark,   // Go to mark position
  ResetPos, // Set current position as zero

  // Configuration commands
  SetAccel,    // param1 = acceleration value
  SetDecel,    // param1 = deceleration value
  SetMaxSpeed, // param1 = max speed value
  SetMark,     // param1 = mark position

  // Query (response via telemetry)
  GetStatus // Trigger status read and telemetry update
};

/**
 * @brief Motor command structure (sent via queue)
 */
struct MotorCommand {
  MotorCmdType type;
  int32_t param1;
  int32_t param2;
};

/**
 * @brief Initialize motor task resources
 *
 * Creates command queue and initializes powerSTEP01 driver.
 * Uses g_spiManager for SPI communication.
 * Call before vTaskStartScheduler().
 *
 * @return true on success
 */
bool MotorTask_Init();

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
bool MotorTask_SendCommand(const MotorCommand &cmd, TickType_t timeout = 0);

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
 * @brief Debug info from powerSTEP01 registers
 */
struct MotorDebugInfo {
    uint16_t status;      // STATUS register
    uint8_t kvalHold;     // KVAL_HOLD
    uint8_t kvalRun;      // KVAL_RUN
    uint8_t kvalAcc;      // KVAL_ACC
    uint8_t kvalDec;      // KVAL_DEC
    uint16_t accel;       // ACC register
    uint16_t decel;       // DEC register
    uint16_t maxSpeed;    // MAX_SPEED register
    int32_t absPos;       // ABS_POS register
    uint8_t ocdTh;        // OCD_TH register (readback from chip)
    uint8_t stallTh;      // STALL_TH register (readback from chip)
    uint16_t config;      // CONFIG register
    uint8_t alarmEn;      // ALARM_EN register
    uint16_t fsSpd;       // FS_SPD register
    uint8_t stepMode;     // STEP_MODE register (bits [2:0])
};

/**
 * @brief Read debug info from powerSTEP01
 * @param info Output structure
 * @return true if successful
 */
bool MotorTask_GetDebugInfo(MotorDebugInfo& info);

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
 * Reads the current configuration from g_motorConfig and applies
 * all settings to the powerSTEP01 registers (KVAL, thresholds,
 * motion params, alarm enables).
 *
 * @return true if successful
 */
bool MotorTask_ApplyConfig();

// Command queue handle (for direct access if needed)
extern QueueHandle_t g_motorCmdQueue;

// Task handle (for suspend/resume)
extern TaskHandle_t g_motorTaskHandle;

} // namespace Tasks

#endif // MOTOR_TASK_HPP
