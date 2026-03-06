/**
 * @file encoder_service_init.cpp
 * @brief Implements Services::initEncoderSubsystem() — L3 drives init cascade
 *
 * Layer-clean: only includes L3 + harness.
 * No L4, L5, F_platform/tasks, or F_platform/rtos includes.
 * All Platform deps injected as parameters.
 */

#include "L3_services/motion/encoder_service_init.hpp"

// L4 init through harness
#include "harness/pins/encoder_driver_init.hpp"

// Task init through harness
#include "harness/pins/encoder_task_init.hpp"

// L3 internal
#include "L3_services/motion/motion.hpp"

namespace Services {

bool initEncoderSubsystem(Harness::IClock& clock,
                          Harness::IScheduler& scheduler,
                          EncoderSubsystemResult& result)
{
    // Step 1: L4 driver init (through harness → L4 → L5)
    Drivers::EncoderDriverHandle* driverHandle = nullptr;
    if (!Drivers::initEncoderDrivers(driverHandle)) {
        return true;  // Encoder is optional — not available
    }

    // L3 filter config from flash
    const auto& encCfg = motion.encoder.config;
    motion.encoder.processor.setFilterConfig(
        encCfg.getEncFilterType(), encCfg.getEncFilterParam(),
        encCfg.getEncSmaWindow(),
        0, 0, 0, 0, 0, 0, 0);

    // Step 2: Task init (through harness → F_platform)
    Harness::EncoderTaskResult taskResult;
    result.encoderAvailable = Harness::initEncoderTask(
        *driverHandle,
        clock,
        motion.encoder.processor,
        motion.control.mode,
        encCfg.getEncMeasWindowMs(),
        encCfg.getEncSampleRateHz(),
        scheduler,
        taskResult);

    if (result.encoderAvailable) {
        result.encoder = taskResult.encoder;
        result.filterControl = taskResult.filterControl;
    }

    return true;  // Always succeeds — encoder is optional hardware
}

} // namespace Services
