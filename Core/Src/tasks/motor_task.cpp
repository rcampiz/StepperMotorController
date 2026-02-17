/**
 * @file motor_task.cpp
 * @brief Motor control task implementation
 */

#include "tasks/motor_task.hpp"
#include <stdint.h>
#include "drivers/powerstep01.hpp"
#include "drivers/spi_bus.hpp"
#include "drivers/spi_manager.hpp"
#include "comms/telemetry.hpp"
#include "services/motor_config.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

namespace Tasks {

// Command queue handle
QueueHandle_t g_motorCmdQueue = nullptr;

// Task handle for suspend/resume
TaskHandle_t g_motorTaskHandle = nullptr;

// Motor driver instance (created in init)
static SPIBus* s_spi = nullptr;
static PowerSTEP01* s_motor = nullptr;

bool MotorTask_Init()
{
    // Create command queue
    g_motorCmdQueue = xQueueCreate(MOTOR_CMD_QUEUE_DEPTH, sizeof(MotorCommand));
    if (g_motorCmdQueue == nullptr) {
        return false;
    }

    // Create SPIBus wrapper around SPI1 from manager
    // Motor driver (powerSTEP01) uses SPI1 with Mode3
    ISPIBus* spi1 = g_spiManager.getSPI1();
    if (spi1 == nullptr) {
        return false;
    }
    s_spi = new SPIBus(*spi1);
    if (s_spi == nullptr) {
        return false;
    }

    // Initialize PowerSTEP01 motor driver
    s_motor = new PowerSTEP01(*s_spi);
    if (s_motor == nullptr) {
        return false;
    }
    s_motor->init();

    // Load motor configuration from flash (or apply defaults)
    Services::g_motorConfig.init();

    // Apply configuration to the driver
    MotorTask_ApplyConfig();

    return true;
}

void vMotorTask(void* pvParameters)
{
    (void)pvParameters;

    // Safety check: if motor driver not initialized, suspend task
    if (s_motor == nullptr) {
        vTaskSuspend(nullptr);
        return;
    }

    // Wait for system startup
    vTaskDelay(pdMS_TO_TICKS(100));

    MotorCommand cmd;
    TickType_t lastStatusUpdate = 0;
    constexpr TickType_t STATUS_UPDATE_PERIOD = pdMS_TO_TICKS(50);

    while (true) {
        // Wait for command with timeout (allows periodic status updates)
        if (xQueueReceive(g_motorCmdQueue, &cmd, STATUS_UPDATE_PERIOD) == pdTRUE) {
            // Process command
            switch (cmd.type) {
                case MotorCmdType::Move: {
                    bool forward = cmd.param1 >= 0;
                    // Safe absolute value: handle INT32_MIN edge case
                    uint32_t steps;
                    if (forward) {
                        steps = static_cast<uint32_t>(cmd.param1);
                    } else if (cmd.param1 == INT32_MIN) {
                        steps = static_cast<uint32_t>(INT32_MAX) + 1U;
                    } else {
                        steps = static_cast<uint32_t>(-cmd.param1);
                    }
                    s_motor->move(forward, steps);
                    break;
                }

                case MotorCmdType::GoTo:
                    s_motor->goTo(cmd.param1);
                    break;

                case MotorCmdType::Run:
                    s_motor->run(cmd.param2 != 0, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SoftStop:
                    s_motor->softStop();
                    break;

                case MotorCmdType::HardStop:
                    s_motor->hardStop();
                    break;

                case MotorCmdType::SoftHiZ:
                    s_motor->softHiZ();
                    break;

                case MotorCmdType::HardHiZ:
                    s_motor->hardHiZ();
                    break;

                case MotorCmdType::GoHome:
                    s_motor->goHome();
                    break;

                case MotorCmdType::GoMark:
                    s_motor->goMark();
                    break;

                case MotorCmdType::ResetPos:
                    s_motor->resetPos();
                    break;

                case MotorCmdType::SetAccel:
                    s_motor->setParam(PowerSTEP01::Reg::ACC, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetDecel:
                    s_motor->setParam(PowerSTEP01::Reg::DEC, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetMaxSpeed:
                    s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::SetMark:
                    s_motor->setParam(PowerSTEP01::Reg::MARK, static_cast<uint32_t>(cmd.param1));
                    break;

                case MotorCmdType::GetStatus:
                    // Force immediate status update
                    lastStatusUpdate = 0;
                    break;
            }
        }

        // Periodic status update
        TickType_t now = xTaskGetTickCount();
        if ((now - lastStatusUpdate) >= STATUS_UPDATE_PERIOD) {
            lastStatusUpdate = now;

            // Read motor status and update telemetry
            PowerSTEP01::Status status = s_motor->getStatus();
            uint32_t rawPos = s_motor->getParam(PowerSTEP01::Reg::ABS_POS);
            uint32_t rawSpeed = s_motor->getParam(PowerSTEP01::Reg::SPEED);

            // Convert raw SPEED register (20-bit) to steps/s
            // Formula: steps_s = raw * 15625 / 1048576
            // (derived from: step/s = raw * 2^-28 / 250ns tick)
            uint32_t speed = static_cast<uint32_t>(
                (static_cast<uint64_t>(rawSpeed) * 15625ULL) / 1048576ULL);

            // Sign-extend 22-bit position to 32-bit
            int32_t position;
            if ((rawPos & 0x200000) != 0) {
                position = static_cast<int32_t>(rawPos | 0xFFC00000U);
            } else {
                position = static_cast<int32_t>(rawPos);
            }

            Comms::MotorTelemetry telem = {};
            telem.position = position;
            telem.speed = speed;
            telem.statusReg = status.raw;
            telem.busy = status.busy();
            telem.hiZ = status.hiZ();
            telem.stalled = status.stallA() || status.stallB();

            Comms::g_telemetry.updateMotor(telem);
        }
    }
}

bool MotorTask_SendCommand(const MotorCommand& cmd, TickType_t timeout)
{
    if (g_motorCmdQueue == nullptr) {
        return false;
    }
    return xQueueSend(g_motorCmdQueue, &cmd, timeout) == pdTRUE;
}

bool MotorTask_Move(int32_t steps)
{
    MotorCommand cmd = {};
    cmd.type = MotorCmdType::Move;
    cmd.param1 = steps;
    return MotorTask_SendCommand(cmd);
}

bool MotorTask_Stop(bool hard)
{
    MotorCommand cmd = {};
    cmd.type = hard ? MotorCmdType::HardStop : MotorCmdType::SoftStop;
    return MotorTask_SendCommand(cmd);
}

void MotorTask_Suspend()
{
    if (g_motorTaskHandle != nullptr) {
        vTaskSuspend(g_motorTaskHandle);
    }
}

void MotorTask_Resume()
{
    if (g_motorTaskHandle != nullptr) {
        vTaskResume(g_motorTaskHandle);
    }
}

bool MotorTask_GetDebugInfo(MotorDebugInfo& info)
{
    if (s_motor == nullptr) {
        return false;
    }

    // Read all debug registers directly
    info.status = s_motor->getStatus().raw;
    info.kvalHold = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_HOLD));
    info.kvalRun = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_RUN));
    info.kvalAcc = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_ACC));
    info.kvalDec = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::KVAL_DEC));
    info.accel = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::ACC));
    info.decel = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::DEC));
    info.maxSpeed = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::MAX_SPEED));

    // Sign-extend 22-bit position
    uint32_t rawPos = s_motor->getParam(PowerSTEP01::Reg::ABS_POS);
    if ((rawPos & 0x200000) != 0) {
        info.absPos = static_cast<int32_t>(rawPos | 0xFFC00000U);
    } else {
        info.absPos = static_cast<int32_t>(rawPos);
    }

    // Readback protection/config registers for verification
    info.ocdTh = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::OCD_TH));
    info.stallTh = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::STALL_TH));
    info.config = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::CONFIG));
    info.alarmEn = static_cast<uint8_t>(s_motor->getParam(PowerSTEP01::Reg::ALARM_EN));
    info.fsSpd = static_cast<uint16_t>(s_motor->getParam(PowerSTEP01::Reg::FS_SPD));

    return true;
}

bool MotorTask_ApplyConfig()
{
    if (s_motor == nullptr) {
        return false;
    }

    const auto& cfg = Services::g_motorConfig.getConfig();

    // Apply KVAL values
    s_motor->setParam(PowerSTEP01::Reg::KVAL_HOLD, cfg.kvalHold);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_RUN, cfg.kvalRun);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_ACC, cfg.kvalAcc);
    s_motor->setParam(PowerSTEP01::Reg::KVAL_DEC, cfg.kvalDec);

    // Apply protection thresholds
    s_motor->setParam(PowerSTEP01::Reg::OCD_TH, cfg.ocdThreshold);
    s_motor->setParam(PowerSTEP01::Reg::STALL_TH, cfg.stallThreshold);

    // Apply motion parameters
    s_motor->setParam(PowerSTEP01::Reg::ACC, cfg.acceleration);
    s_motor->setParam(PowerSTEP01::Reg::DEC, cfg.deceleration);
    s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, cfg.maxSpeed);
    s_motor->setParam(PowerSTEP01::Reg::MIN_SPEED, cfg.minSpeed);
    s_motor->setParam(PowerSTEP01::Reg::FS_SPD, cfg.fsSpeed);

    // Build ALARM_EN register based on fault enable flags
    // ALARM_EN bits: 0=OCD, 1=TH_SD, 2=TH_WRN, 3=UVLO, 4=UVLO_ADC, 5=STALL_A, 6=STALL_B, 7=CMD_ERR
    // powerSTEP01: bit=1 enables alarm on FLAG pin, bit=0 disables
    uint8_t alarmEn = 0x00;  // Start with all disabled
    if (cfg.faultEnable.ocd) {
        alarmEn |= (1 << 0);
    }
    if (cfg.faultEnable.thermalSD) {
        alarmEn |= (1 << 1);
    }
    if (cfg.faultEnable.thermalWarn) {
        alarmEn |= (1 << 2);
    }
    if (cfg.faultEnable.uvlo) {
        alarmEn |= (1 << 3);
    }
    // Bit 4 = UVLO_ADC - keep disabled (floating ADCIN causes false trips)
    if (cfg.faultEnable.stallA) {
        alarmEn |= (1 << 5);
    }
    if (cfg.faultEnable.stallB) {
        alarmEn |= (1 << 6);
    }
    if (cfg.faultEnable.cmdErr) {
        alarmEn |= (1 << 7);
    }
    s_motor->setParam(PowerSTEP01::Reg::ALARM_EN, alarmEn);

    // Clear any latched fault bits from the register write sequence
    s_motor->getStatus();

    return true;
}

void MotorTask_Reinit()
{
    if (s_motor != nullptr) {
        // Reinitialize the motor driver with correct settings
        // This should be called after LCD_DISABLE to ensure proper SPI config
        s_motor->init();

        // Re-apply stored configuration
        MotorTask_ApplyConfig();
    }
}

} // namespace Tasks
