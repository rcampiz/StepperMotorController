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
#include "services/tick_timer.hpp"
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

// Position + timestamp ring buffer for velocity calculation
// Uses hardware timer (TIM5 microseconds) for RTOS-independent accuracy
struct PosSample {
    int32_t count;
    uint32_t tickUs;  // Services::TickTimer_GetTick() — microseconds
};
static constexpr int POS_BUF_SIZE = 5;
static PosSample s_posBuf[POS_BUF_SIZE] = {};
static int s_posHead = 0;
static int s_posCount = 0;

// 64-bit accumulator for wrap-free position tracking
static int64_t s_accumulatedCount = 0;
static uint16_t s_lastRawCount = 0;

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
    s_state.revolutions = 0;
    s_state.indexPeriodUs = 0;
    s_accumulatedCount = 0;
    s_lastRawCount = static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
    s_posHead = 0;
    s_posCount = 0;

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
        // Read current raw count and hardware timestamp (RTOS-independent)
        uint16_t rawCount = static_cast<uint16_t>(TIM4->CNT & 0xFFFF);
        uint32_t tickUs = Services::TickTimer_GetTick();

        // Accumulate 64-bit position: delta handles 16-bit wrap correctly
        int16_t delta = static_cast<int16_t>(rawCount - s_lastRawCount);
        s_accumulatedCount += delta;
        s_lastRawCount = rawCount;

        // Use raw 16-bit count for velocity ring buffer (wrap-aware subtraction)
        int32_t count = static_cast<int32_t>(static_cast<int16_t>(rawCount));

        // Store position + timestamp in ring buffer
        s_posBuf[s_posHead] = {count, tickUs};
        s_posHead = (s_posHead + 1) % POS_BUF_SIZE;
        if (s_posCount < POS_BUF_SIZE) s_posCount++;

        // Compute velocity over full window using actual elapsed time
        // Uses uint16_t subtraction to handle TIM4 16-bit counter wrap
        int32_t velocity = 0;
        if (s_posCount >= 2) {
            int oldestIdx = (s_posCount < POS_BUF_SIZE) ? 0 : s_posHead;
            uint32_t dtUs = tickUs - s_posBuf[oldestIdx].tickUs;
            if (dtUs > 0) {
                int16_t delta = static_cast<int16_t>(
                    static_cast<uint16_t>(count) -
                    static_cast<uint16_t>(s_posBuf[oldestIdx].count));
                velocity = static_cast<int32_t>(delta) * 1000000 /
                           static_cast<int32_t>(dtUs);
            }
        }

        // Get index pulse status
        bool indexSeen = s_encoder.isIndexSeen();
        uint32_t indexTick = s_encoder.getIndexTick();

        // Get revolution data from encoder driver
        int32_t revolutions = s_encoder.getRevolutions();
        uint32_t indexPeriodUs = s_encoder.getIndexPeriodUs();

        // Update state (critical section for thread safety)
        taskENTER_CRITICAL();
        s_state.count = s_accumulatedCount;
        s_state.velocity = velocity;
        s_state.indexSeen = indexSeen;
        s_state.indexTick = indexTick;
        s_state.revolutions = revolutions;
        s_state.indexPeriodUs = indexPeriodUs;
        taskEXIT_CRITICAL();

        // Update telemetry
        Comms::EncoderTelemetry telem = {};
        telem.count = s_accumulatedCount;
        telem.velocity = velocity;
        telem.indexSeen = indexSeen;
        telem.indexTick = indexTick;
        telem.revolutions = revolutions;
        telem.indexPeriodUs = indexPeriodUs;
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
    state.revolutions = s_state.revolutions;
    state.indexPeriodUs = s_state.indexPeriodUs;
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
    s_accumulatedCount = 0;
    s_lastRawCount = 0;
    s_posHead = 0;
    s_posCount = 0;
    taskEXIT_CRITICAL();
}

void EncoderTask_IndexISR()
{
    // Called from EXTI4_IRQHandler
    uint32_t tickUs = Services::TickTimer_GetTick();
    s_encoder.indexISR(xTaskGetTickCountFromISR(), tickUs);
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
    m_revolutions = 0;
    m_lastIndexUs = 0;
    m_indexPeriodUs = 0;
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

    // Configure EZ (PC9) as input with pull-up for index pulse
    GPIOC->MODER &= ~(3U << (9 * 2));   // Input mode
    GPIOC->PUPDR &= ~(3U << (9 * 2));   // Clear pull
    GPIOC->PUPDR |=  (1U << (9 * 2));   // Pull-up

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

    // Clock stabilization delay (same pattern as initGPIO)
    volatile uint32_t dummy = RCC->APB2ENR;
    (void)dummy;

    // Map EXTI9 to PC9: EXTICR[2] bits 7:4 select source for EXTI9
    // 0x2 = Port C (RM0368 Table 40)
    SYSCFG->EXTICR[2] = (SYSCFG->EXTICR[2] & ~(0xFU << 4)) | (0x2U << 4);

    // Configure EXTI9 for falling edge (index pulse is active low)
    EXTI->IMR  |= (1U << 9);   // Unmask line 9
    EXTI->FTSR |= (1U << 9);   // Falling edge trigger

    // Enable EXTI9_5 interrupt in NVIC
    NVIC_SetPriority(EXTI9_5_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(EXTI9_5_IRQn);
}

void Encoder::disableIndexInterrupt()
{
    EXTI->IMR &= ~(1U << 9);  // Mask line 9
    NVIC_DisableIRQ(EXTI9_5_IRQn);
}

// ============================================================================
// EXTI9_5 interrupt handler (shared for EXTI lines 5-9)
// ============================================================================

extern "C" void EXTI9_5_IRQHandler(void)
{
    if (EXTI->PR & (1U << 9)) {
        EXTI->PR = (1U << 9);  // Clear pending (write-1-to-clear)
        Tasks::EncoderTask_IndexISR();
    }
}
