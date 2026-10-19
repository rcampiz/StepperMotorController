/**
 * @file task_stacks.hpp
 * @brief Every task's stack size, in one place (words of 4 bytes).
 *
 * The Platform's static stack pool (FreeRTOSScheduler::STACK_POOL_WORDS) is sized from the
 * total here plus a small spare, so changing (or reverting) a size is a one-file edit.
 * configCHECK_FOR_STACK_OVERFLOW stays 2 (FreeRTOSConfig.h): an overflow is caught at the
 * next context switch. Check peaks on the board with DIAG:TASKS? (high-water marks) and
 * DIAG:MEM? (pool use) after any change.
 *
 * Sizing rule: at least 2x the worst case, where the worst case is the larger of the peak
 * measured on the board and the deepest static call path (GCC -fstack-usage /
 * -fcallgraph-info=su, virtual hops resolved by hand, newlib snprintf frames read from the
 * disassembly, plus the 104 B FPU exception frame).
 *
 *  Task     size   board peak (2026-10, debug)  static worst (debug / release)
 *  Motor     512   ~239 words                   ~246 / ~258 words: DRV diagnostics job
 *                                                (getDebugInfo > driver diagnostics >
 *                                                snprintf), periodicUpdate > traced getStatus
 *                                                > SPI. Was 1024.
 *  Comms    2048   ~609 words                   ~970 / ~1185 words: UI:FLASH:SHOW with a
 *                                                JSON reply (cmdFlashShow 1.2 KB chunk buffers
 *                                                > respondOk > respondJsonOk 1 KB line >
 *                                                snprintf). Not shrunk: 1280 would leave
 *                                                1.3x / 1.1x on that path.
 *  Encoder   256   ~100 words                   (unchanged)
 *  Display   640   ~299 words                   (unchanged)
 *
 * Previous sizes, to revert: Motor 1024, Comms 2048, Encoder 256, Display 640.
 */
#ifndef TASK_STACKS_HPP
#define TASK_STACKS_HPP

#include <stdint.h>

namespace Scheduler {

constexpr uint32_t MOTOR_TASK_STACK_SIZE   = 512;
constexpr uint32_t COMMS_TASK_STACK_SIZE   = 2048;
constexpr uint32_t ENCODER_TASK_STACK_SIZE = 256;
constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 640;

/// What the four tasks take from the Platform's stack pool
constexpr uint32_t TASK_STACKS_TOTAL_WORDS =
    MOTOR_TASK_STACK_SIZE + COMMS_TASK_STACK_SIZE + ENCODER_TASK_STACK_SIZE + DISPLAY_TASK_STACK_SIZE;

} // namespace Scheduler

#endif // TASK_STACKS_HPP
