/**
 * @file encoder_task.hpp
 * @brief Encoder task — thin sheet: wait 10 ms, call Services::IEncoderService::sample()
 *
 * All sampling/filtering logic lives in L3 EncoderService (H_TASKS_L3).
 */
#ifndef ENCODER_TASK_HPP
#define ENCODER_TASK_HPP

#include <stdint.h>

namespace Platform { class IClock; }
namespace Services { class IEncoderService; }

namespace Scheduler {

constexpr uint32_t ENCODER_TASK_STACK_SIZE = 256;  // words; static chain ~350 B incl. FPU frame, 128 left ~40 words
constexpr uint32_t ENCODER_TASK_PRIORITY = 2;
constexpr uint32_t ENCODER_SAMPLE_PERIOD_MS = 10; // 100 Hz task wake

bool EncoderTask_Init(Services::IEncoderService& service, Platform::IClock& clock);
void vEncoderTask(void* pvParameters);

} // namespace Scheduler

#endif // ENCODER_TASK_HPP
