/**
 * @file encoder_task.cpp
 * @brief Encoder reading task implementation
 *
 * Encoder is OPTIONAL - system operates in OPEN_LOOP mode if encoder
 * initialization fails or encoder hardware is not present.
 */

#include "tasks/encoder_task.hpp"
#include "drivers/encoder.hpp"
#include "services/control_mode.hpp"
#include "comms/telemetry.hpp"
#include "stm32f401xe.h"
#include "core_cm4.h"
#include "FreeRTOS.h"
#include "task.h"

namespace Tasks {

// Encoder driver instance
static Encoder s_encoder;

// Encoder state (protected by critical section)
static volatile EncoderState s_state = {};

// Previous count for velocity calculation
static int32_t s_prevCount = 0;

// Moving average filter for velocity (reduces noise from step jitter)
static constexpr int VELOCITY_FILTER_SIZE = 4;
static int32_t s_velocityBuf[VELOCITY_FILTER_SIZE] = {};
static int s_velocityIdx = 0;

// Task handle for self-suspension
static TaskHandle_t s_taskHandle = nullptr;

bool EncoderTask_Init()
{
    // Mark encoder as initializing
    Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::INITIALIZING);

    // Try to initialize encoder hardware
    if (!s_encoder.init()) {
        // Encoder init failed - this is OK, system will run in OPEN_LOOP
        Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::NOT_PRESENT);
        return false;
    }

    // Initialize state
    s_state.count = 0;
    s_state.velocity = 0;
    s_state.indexSeen = false;
    s_state.indexTick = 0;
    s_prevCount = 0;

    // Encoder ready
    Services::g_controlMode.setEncoderStatus(Services::EncoderStatus::READY);
    return true;
}

void vEncoderTask(void* pvParameters)
{
    (void)pvParameters;

    // Store task handle for potential suspension
    s_taskHandle = xTaskGetCurrentTaskHandle();

    // Check if encoder is available
    if (!s_encoder.isReady()) {
        // Encoder not available - suspend this task indefinitely
        // System continues in OPEN_LOOP mode
        vTaskSuspend(nullptr);
        return;  // Never reached
    }

    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true) {
        // Read current count from encoder
        int32_t count = s_encoder.getCount();

        // Calculate velocity (counts per second)
        int32_t delta = count - s_prevCount;
        int32_t rawVelocity = (delta * 1000) / static_cast<int32_t>(ENCODER_SAMPLE_PERIOD_MS);
        s_prevCount = count;

        // 4-sample moving average to smooth step jitter
        s_velocityBuf[s_velocityIdx] = rawVelocity;
        s_velocityIdx = (s_velocityIdx + 1) % VELOCITY_FILTER_SIZE;
        int32_t velocity = 0;
        for (int i = 0; i < VELOCITY_FILTER_SIZE; i++) {
            velocity += s_velocityBuf[i];
        }
        velocity /= VELOCITY_FILTER_SIZE;

        // Get index pulse status
        bool indexSeen = s_encoder.isIndexSeen();
        uint32_t indexTick = s_encoder.getIndexTick();

        // Update state (critical section for thread safety)
        taskENTER_CRITICAL();
        s_state.count = count;
        s_state.velocity = velocity;
        s_state.indexSeen = indexSeen;
        s_state.indexTick = indexTick;
        taskEXIT_CRITICAL();

        // Update telemetry
        Comms::EncoderTelemetry telem = {};
        telem.count = count;
        telem.velocity = velocity;
        telem.indexSeen = indexSeen;
        telem.indexTick = indexTick;
        Comms::g_telemetry.updateEncoder(telem);

        // Wait for next sample period
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(ENCODER_SAMPLE_PERIOD_MS));
    }
}

EncoderState EncoderTask_GetState()
{
    EncoderState state;
    taskENTER_CRITICAL();
    state.count = s_state.count;
    state.velocity = s_state.velocity;
    state.indexSeen = s_state.indexSeen;
    state.indexTick = s_state.indexTick;
    taskEXIT_CRITICAL();
    return state;
}

int32_t EncoderTask_GetCount()
{
    if (!s_encoder.isReady()) {
        return 0;
    }
    return s_encoder.getCount();
}

void EncoderTask_ClearIndexFlag()
{
    s_encoder.clearIndexFlag();
    taskENTER_CRITICAL();
    s_state.indexSeen = false;
    taskEXIT_CRITICAL();
}

void EncoderTask_ResetCount()
{
    s_encoder.reset();
    taskENTER_CRITICAL();
    s_state.count = 0;
    s_prevCount = 0;
    taskEXIT_CRITICAL();
}

void EncoderTask_IndexISR()
{
    // Called from EXTI4_IRQHandler
    s_encoder.indexISR(xTaskGetTickCountFromISR());
}

bool EncoderTask_IsAvailable()
{
    return s_encoder.isReady();
}

} // namespace Tasks

// ============================================================================
// Encoder driver implementation (init functions)
// ============================================================================

bool Encoder::init()
{
    m_status = Status::INITIALIZING;

    if (!initGPIO()) {
        m_status = Status::FAULT;
        return false;
    }

    if (!initTimer()) {
        m_status = Status::FAULT;
        return false;
    }

    // Enable index pulse interrupt (EXTI4 on PC4)
    enableIndexInterrupt();

    m_indexSeen = false;
    m_indexTick = 0;
    m_status = Status::READY;
    return true;
}

bool Encoder::initGPIO()
{
    // Enable GPIO clocks
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->AHB1ENR;
    (void)dummy;

    // Configure EA (PB6) and EB (PB7) as alternate function for TIM4
    // PB6 = TIM4_CH1, PB7 = TIM4_CH2, both AF2
    GPIOB->MODER &= ~(GPIO_MODER_MODER6 | GPIO_MODER_MODER7);
    GPIOB->MODER |= (0x2 << GPIO_MODER_MODER6_Pos) | (0x2 << GPIO_MODER_MODER7_Pos);
    GPIOB->AFR[0] &= ~(GPIO_AFRL_AFRL6 | GPIO_AFRL_AFRL7);
    GPIOB->AFR[0] |= (Pins::Encoder::EA_AF << GPIO_AFRL_AFRL6_Pos) |
                     (Pins::Encoder::EB_AF << GPIO_AFRL_AFRL7_Pos);

    // Configure EZ (PC4) as input with pull-up for index pulse
    GPIOC->MODER &= ~GPIO_MODER_MODER4;  // Input
    GPIOC->PUPDR &= ~GPIO_PUPDR_PUPDR4;
    GPIOC->PUPDR |= GPIO_PUPDR_PUPDR4_0;  // Pull-up

    // Enable AM26LV32EIDR line receiver: G (PC3) = HIGH, nG (PC2) = LOW
    // PC2 (nG): output, drive low
    GPIOC->MODER &= ~GPIO_MODER_MODER2;
    GPIOC->MODER |= (0x1 << GPIO_MODER_MODER2_Pos);  // Output
    GPIOC->BSRR = (1U << 18);  // Reset PC2 (low)

    // PC3 (G): output, drive high
    GPIOC->MODER &= ~GPIO_MODER_MODER3;
    GPIOC->MODER |= (0x1 << GPIO_MODER_MODER3_Pos);  // Output
    GPIOC->BSRR = (1U << 3);   // Set PC3 (high)

    return true;
}

bool Encoder::initTimer()
{
    // Enable TIM4 clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM4EN;

    // Brief delay for clock to stabilize
    volatile uint32_t dummy = RCC->APB1ENR;
    (void)dummy;

    // Configure TIM4 in encoder mode (16-bit timer, ARR max 0xFFFF)
    TIM4->CR1 = 0;  // Stop timer during config
    TIM4->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;  // Encoder mode 3 (count on both edges)
    TIM4->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_CC2S_0;  // IC1 = TI1, IC2 = TI2
    TIM4->CCER = 0;  // Rising edge, non-inverted
    TIM4->ARR = 0xFFFF;  // Full 16-bit range
    TIM4->CNT = 0;  // Reset counter
    TIM4->CR1 = TIM_CR1_CEN;  // Enable counter

    return true;
}

void Encoder::enableIndexInterrupt()
{
    // Enable SYSCFG clock for EXTI
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;

    // Map EXTI4 to PC4
    SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI4;
    SYSCFG->EXTICR[1] |= SYSCFG_EXTICR2_EXTI4_PC;

    // Configure EXTI4 for falling edge (index pulse is active low)
    EXTI->IMR |= EXTI_IMR_MR4;   // Unmask interrupt
    EXTI->FTSR |= EXTI_FTSR_TR4; // Falling edge trigger

    // Enable EXTI4 interrupt in NVIC
    NVIC_SetPriority(EXTI4_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(EXTI4_IRQn);
}

void Encoder::disableIndexInterrupt()
{
    EXTI->IMR &= ~EXTI_IMR_MR4;  // Mask interrupt
    NVIC_DisableIRQ(EXTI4_IRQn);
}

// ============================================================================
// EXTI4 interrupt handler
// ============================================================================

extern "C" void EXTI4_IRQHandler(void)
{
    if (EXTI->PR & EXTI_PR_PR4) {
        EXTI->PR = EXTI_PR_PR4;  // Clear pending
        Tasks::EncoderTask_IndexISR();
    }
}
