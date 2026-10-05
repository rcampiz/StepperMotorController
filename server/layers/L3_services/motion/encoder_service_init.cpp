/**
 * @file encoder_service_init.cpp
 * @brief Implements Services::initEncoderSubsystem() — L3 drives the encoder init cascade
 *
 * Hardware arrives through H_L3_L4 (nullptr when absent: the system stays OPEN_LOOP).
 * The Encoder task is created through the task init contract and calls
 * EncoderService::sample() (H_TASKS_L3).
 */

#include "L3_services/motion/encoder_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "harness/pins/encoder_task_init.hpp"
#include "L3_services/motion/encoder_service/encoder_service.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

bool initEncoderSubsystem(Platform::IClock& clock,
                          Platform::IScheduler& scheduler,
                          Platform::ICriticalSection& critical,
                          Platform::IMicrosecondClock& usClock,
                          EncoderSubsystemResult& result)
{
    const Harness::L3L4Harness& h = Harness::connectL3L4(clock);
    if (h.encoderHw == nullptr) {
        return true;  // Encoder is optional — not available
    }

    const auto& encCfg = motion.encoder.config;
    motion.encoder.processor.setFilterConfig(
        encCfg.getEncFilterType(), encCfg.getEncFilterParam(),
        encCfg.getEncSmaWindow(),
        0, 0, 0, 0, 0, 0, 0);

    if (!g_encoderService.init(*h.encoderHw, critical, usClock,
                               motion.encoder.processor, motion.control.mode,
                               encCfg.getEncMeasWindowMs(),
                               encCfg.getEncSampleRateHz())) {
        return true;  // hardware not ready: stay OPEN_LOOP
    }

    if (!Harness::initEncoderTask(g_encoderService, clock, scheduler)) {
        return false;
    }

    result.encoderAvailable = true;
    result.encoder = &g_encoderService;
    result.filterControl = &g_encoderService;
    return true;
}

} // namespace Services
