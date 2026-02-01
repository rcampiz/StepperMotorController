/**
 * @file motor_task.cpp
 * @brief Motor control task implementation
 */

#include "tasks/motor_task.hpp"
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
static PowerSTEP01* s_motor = nullptr;
static SPIBus* s_spi = nullptr;

bool MotorTask_Init()
{
    // Create command queue
    g_motorCmdQueue = xQueueCreate(MOTOR_CMD_QUEUE_DEPTH, sizeof(MotorCommand));
    if (g_motorCmdQueue == nullptr) {
        return false;
    }

    // TODO: Initialize SPI bus and motor driver
    // s_spi = new SPIBus(SPI1, SPIBus::Prescaler::Div16);
    // s_motor = new PowerSTEP01(*s_spi);
    // s_motor->init();

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
                case MotorCmdType::Move:
                    // TODO: s_motor->move(cmd.param1 >= 0, abs(cmd.param1));
                    break;

                case MotorCmdType::GoTo:
                    // TODO: s_motor->goTo(cmd.param1);
                    break;

                case MotorCmdType::Run:
                    // TODO: s_motor->run(cmd.param2 != 0, cmd.param1);
                    break;

                case MotorCmdType::SoftStop:
                    // TODO: s_motor->softStop();
                    break;

                case MotorCmdType::HardStop:
                    // TODO: s_motor->hardStop();
                    break;

                case MotorCmdType::SoftHiZ:
                    // TODO: s_motor->softHiZ();
                    break;

                case MotorCmdType::HardHiZ:
                    // TODO: s_motor->hardHiZ();
                    break;

                case MotorCmdType::GoHome:
                    // TODO: s_motor->goHome();
                    break;

                case MotorCmdType::GoMark:
                    // TODO: s_motor->goMark();
                    break;

                case MotorCmdType::ResetPos:
                    // TODO: s_motor->resetPos();
                    break;

                case MotorCmdType::SetAccel:
                    // TODO: s_motor->setParam(PowerSTEP01::Reg::ACC, cmd.param1);
                    break;

                case MotorCmdType::SetDecel:
                    // TODO: s_motor->setParam(PowerSTEP01::Reg::DEC, cmd.param1);
                    break;

                case MotorCmdType::SetMaxSpeed:
                    // TODO: s_motor->setParam(PowerSTEP01::Reg::MAX_SPEED, cmd.param1);
                    break;

                case MotorCmdType::SetMark:
                    // TODO: s_motor->setParam(PowerSTEP01::Reg::MARK, cmd.param1);
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

            // TODO: Read motor status and update telemetry
            // PowerSTEP01::Status status = s_motor->getStatus();
            // int32_t position = s_motor->getParam(PowerSTEP01::Reg::ABS_POS);
            // uint32_t speed = s_motor->getParam(PowerSTEP01::Reg::SPEED);

            Comms::MotorTelemetry telem = {};
            // telem.position = position;
            // telem.speed = speed;
            // telem.statusReg = status.raw;
            // telem.busy = status.busy();
            // telem.hiZ = status.hiZ();
            // telem.stalled = status.stallA() || status.stallB();

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
