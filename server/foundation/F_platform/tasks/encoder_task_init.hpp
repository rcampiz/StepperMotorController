/**
 * @file encoder_task_init.hpp
 * @brief Harness init contract — encoder task init + RTOS task creation
 *
 * Takes opaque EncoderDriverHandle from L4 init. L3 ferries the handle
 * from L4 to Platform without seeing inside it.
 *
 * Declared in harness so L3 can init the encoder task without
 * including L4 or F_platform/tasks headers.
 */

#pragma once

#include <stdint.h>

namespace Drivers { struct EncoderDriverHandle; }

namespace Platform { class IClock; class IScheduler; }

namespace Scheduler { class IEncoder; class IEncoderFilterControl; }
namespace Services { class IEncoderProcessing; class IEncoderStatusSink; }

namespace Harness {

/**
 * @brief Result of encoder task initialization
 */
struct EncoderTaskResult {
    Scheduler::IEncoder* encoder = nullptr;                ///< Encoder data access (valid after init)
    Scheduler::IEncoderFilterControl* filterControl = nullptr; ///< Filter control (valid after init)
};

/**
 * @brief Initialize encoder task and create RTOS task
 *
 * Opens opaque driver handle, calls Scheduler::EncoderTask_Init,
 * creates RTOS task. Returns harness interfaces for dispatch wiring.
 *
 * @param driverHandle Opaque handle from L4 encoder driver init
 * @param clock        System clock (injected from Platform)
 * @param processor    Encoder signal processing service (L3)
 * @param statusSink   Encoder status reporting sink (L3)
 * @param measWindowMs Measurement window in ms (0 = default)
 * @param sampleRateHz DMA sample rate in Hz (0 = default)
 * @param scheduler    Scheduler for RTOS task creation
 * @param result       Output: encoder interfaces (populated on success)
 * @return true if task created successfully
 */
bool initEncoderTask(Drivers::EncoderDriverHandle& driverHandle,
                     Platform::IClock& clock,
                     Services::IEncoderProcessing& processor,
                     Services::IEncoderStatusSink& statusSink,
                     uint8_t measWindowMs,
                     uint16_t sampleRateHz,
                     Platform::IScheduler& scheduler,
                     EncoderTaskResult& result);

} // namespace Harness
