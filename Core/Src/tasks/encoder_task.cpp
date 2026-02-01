/**
 * @file encoder_task.cpp
 * @brief Encoder reading task implementation
 */

#include "tasks/encoder_task.hpp"
#include "board/board_pins.hpp"
#include "comms/telemetry.hpp"
#include "FreeRTOS.h"
#include "task.h"

namespace Tasks {

// Encoder state (protected by critical section)
static volatile EncoderState s_state = {};

// Previous count for velocity calculation
static int32_t s_prevCount = 0;

bool EncoderTask_Init()
{
    // Enable GPIO and timer clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOCEN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Configure EA (PA0) and EB (PA1) as alternate function for TIM2
    // PA0 = TIM2_CH1, PA1 = TIM2_CH2, both AF1
    GPIOA->MODER &= ~(GPIO_MODER_MODER0 | GPIO_MODER_MODER1);
    GPIOA->MODER |= (0x2 << GPIO_MODER_MODER0_Pos) | (0x2 << GPIO_MODER_MODER1_Pos);
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFRL0 | GPIO_AFRL_AFRL1);
    GPIOA->AFR[0] |= (Pins::Encoder::EA_AF << GPIO_AFRL_AFRL0_Pos) |
                     (Pins::Encoder::EB_AF << GPIO_AFRL_AFRL1_Pos);

    // Configure TIM2 in encoder mode
    TIM2->CR1 = 0;
    TIM2->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  // Encoder mode 3 (count on both edges)
    TIM2->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // IC1 = TI1, IC2 = TI2
    TIM2->CCER = 0;  // Rising edge, non-inverted
    TIM2->ARR = 0xFFFFFFFF;  // Full 32-bit range
    TIM2->CNT = 0;
    TIM2->CR1 = TIM_CR1_CEN;  // Enable counter

    // Configure EZ (PC4) as input with pull-up for index pulse
    GPIOC->MODER &= ~GPIO_MODER_MODER4;  // Input
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR4;
    GPIOC->PUPDR |= GPIO_PUPDR_PUPDR4_0;  // Pull-up

    // TODO: Configure EXTI4 for index pulse interrupt
    // SYSCFG->EXTICR[1] |= SYSCFG_EXTICR2_EXTI4_PC;
    // EXTI->IMR |= EXTI_IMR_MR4;
    // EXTI->FTSR |= EXTI_FTSR_TR4;  // Falling edge (active low)
    // NVIC_EnableIRQ(EXTI4_IRQn);

    // Initialize state
    s_state.count = 0;
    s_state.velocity = 0;
    s_state.indexSeen = false;
    s_state.indexTick = 0;
    s_prevCount = 0;

    return true;
}

void vEncoderTask(void* pvParameters)
{
    (void)pvParameters;

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        // Read current count
        int32_t count = static_cast<int32_t>(TIM2->CNT);

        // Calculate velocity (counts per second)
        int32_t delta = count - s_prevCount;
        int32_t velocity = (delta * 1000) / ENCODER_SAMPLE_PERIOD_MS;
        s_prevCount = count;

        // Update state (critical section for thread safety)
        taskENTER_CRITICAL();
        s_state.count = count;
        s_state.velocity = velocity;
        taskEXIT_CRITICAL();

        // Update telemetry
        Comms::EncoderTelemetry telem = {};
        telem.count = count;
        telem.velocity = velocity;
        telem.indexSeen = s_state.indexSeen;
        telem.indexTick = s_state.indexTick;
        Comms::g_telemetry.updateEncoder(telem);

        // Wait for next sample period
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ENCODER_SAMPLE_PERIOD_MS));
    }
}

EncoderState EncoderTask_GetState()
{
    EncoderState state;
    taskENTER_CRITICAL();
    state = s_state;
    taskEXIT_CRITICAL();
    return state;
}

int32_t EncoderTask_GetCount()
{
    return static_cast<int32_t>(TIM2->CNT);
}

void EncoderTask_ClearIndexFlag()
{
    taskENTER_CRITICAL();
    s_state.indexSeen = false;
    taskEXIT_CRITICAL();
}

void EncoderTask_ResetCount()
{
    TIM2->CNT = 0;
    taskENTER_CRITICAL();
    s_state.count = 0;
    s_prevCount = 0;
    taskEXIT_CRITICAL();
}

void EncoderTask_IndexISR()
{
    // Called from EXTI4_IRQHandler
    s_state.indexSeen = true;
    s_state.indexTick = xTaskGetTickCountFromISR();
}

} // namespace Tasks

// EXTI4 interrupt handler (place in stm32f4xx_it.c or here)
extern "C" void EXTI4_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR4) {
        EXTI->PR = EXTI_PR_PR4;  // Clear pending
        Tasks::EncoderTask_IndexISR();
    }
}
