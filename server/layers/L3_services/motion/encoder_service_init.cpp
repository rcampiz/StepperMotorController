/**
 * @file encoder_service_init.cpp
 * @brief Implements Services::initEncoderSubsystem() — L3 drives the encoder init cascade
 */

#include "L3_services/motion/encoder_service_init.hpp"
#include "harness/defs/harness_l3_l4.hpp"
#include "L3_services/motion/encoder_service/encoder_service.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

bool initEncoderSubsystem(Platform::IClock& clock,
                          Platform::ICriticalSection& critical,
                          Platform::IMicrosecondClock& usClock,
                          EncoderSubsystemResult& result)
{
    const Harness::L3L4Harness& h = Harness::connectL3L4(clock);
    if (h.encoderHw == nullptr) {
        return true;  // Encoder is optional — not available
    }

    const auto& encCfg = motion.encoder.config;
    const Harness::EncoderFilterConfig f = encCfg.getEncFilterConfig();
    motion.encoder.processor.setFilterConfig(
        f.filterFlags, f.emaAlpha, f.smaWindow, f.padeGainPct, f.padeMaxCorr,
        f.biquadCutoffHz, f.notchCenterHz, f.notchQ10, f.holtAlpha, f.holtBeta);

    if (!g_encoderService.init(*h.encoderHw, critical, usClock,
                               motion.encoder.processor, motion.control.mode,
                               encCfg.getEncMeasWindowMs(),
                               encCfg.getEncSampleRateHz())) {
        return true;  // hardware not ready: stay OPEN_LOOP
    }

    // The motor task reads the encoder at the instant it reads the motor position (slip,
    // following error: a matched pair, not two values up to 50 ms apart)
    motion.stepper.controller.setEncoder(&g_encoderService);

    result.encoderAvailable = true;
    result.encoderService = &g_encoderService;
    result.encoder = &g_encoderService;
    result.filterControl = &g_encoderService;
    return true;
}

} // namespace Services
