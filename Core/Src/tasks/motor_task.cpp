/**
 * @file motor_task.cpp
 * @brief Motor control task implementation
 */

#include "tasks/motor_task.hpp"
#include <stdint.h>
#include "drivers/powerstep01.hpp"
#include "drivers/spi_bus.hpp"
#include "comms/telemetry.hpp"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

namespace Tasks {

// Command queue handle
QueueHandle_t g_motorCmdQueue = nullptr;

// Motor driver instance (created in init)
static SPIBus* s_spi = nullptr;
static PowerSTEP01* s_motor = nullptr;

bool MotorTask_Init(SPIBus& spi)
{
    // Create command queue
    g_motorCmdQueue = xQueueCreate(MOTOR_CMD_QUEUE_DEPTH, sizeof(MotorCommand));
    if (g_motorCmdQueue == nullptr) {
        return false;
    }

    // Store reference to shared SPI bus
    s_spi = &spi;

    // Initialize PowerSTEP01 motor driver
    s_motor = new PowerSTEP01(*s_spi);
    if (s_motor == nullptr) {
        return false;
    }
    s_motor->init();

    return true;
}

void vMotorTask(void* pvParameters)
{
    (void)pvParameters;

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
                    uint32_t steps = forward ? cmd.param1 : -cmd.param1;
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
            uint32_t speed = s_motor->getParam(PowerSTEP01::Reg::SPEED);

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

} // namespace Tasks
