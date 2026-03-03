/**
 * @file sd_encoder.cpp
 * @brief ServiceDispatcher — IEncoderDispatcher implementation
 */

#include "service_dispatcher.hpp"
#include "F_platform/hal/iencoder.hpp"
#include "F_platform/tasks/encoder_task.hpp"
#include "L3_services/motion/motor_config.hpp"

bool ServiceDispatcher::isEncoderAvailable() {
    return m_encoder != nullptr && m_encoder->isAvailable();
}

Comms::IEncoderDispatcher::EncoderSnapshot ServiceDispatcher::getEncoderSnapshot() {
    if (m_encoder == nullptr) {
        return {0, 0, false, 0, 0, 0};
    }
    Services::EncoderSnapshot st = m_encoder->getState();
    return {st.count, st.velocity, st.indexSeen,
            st.indexTick, st.revolutions, st.indexPeriodUs};
}

void ServiceDispatcher::encoderResetCount() {
    if (m_encoder != nullptr) m_encoder->resetCount();
}

void ServiceDispatcher::encFilterGetConfig(EncFilterParams& out) {
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);
    out.filterFlags   = cfg.filterFlags;
    out.emaAlpha      = cfg.emaAlpha;
    out.smaWindow     = cfg.smaWindow;
    out.measWindowMs  = cfg.measWindowMs;
    out.sampleRateHz  = cfg.sampleRateHz;
    out.padeGainPct   = cfg.padeGainPct;
    out.padeMaxCorr   = cfg.padeMaxCorr;
    out.biquadCutoffHz = cfg.biquadCutoffHz;
    out.notchCenterHz = cfg.notchCenterHz;
    out.notchQ10      = cfg.notchQ10;
    out.holtAlpha     = cfg.holtAlpha;
    out.holtBeta      = cfg.holtBeta;
}

void ServiceDispatcher::encFilterSetConfig(const EncFilterParams& params) {
    Tasks::EncoderFilterConfig cfg;
    Tasks::EncoderTask_GetFilterConfig(cfg);  // preserve unset fields
    cfg.filterFlags   = params.filterFlags;
    cfg.emaAlpha      = params.emaAlpha;
    cfg.smaWindow     = params.smaWindow;
    cfg.measWindowMs  = params.measWindowMs;
    cfg.sampleRateHz  = params.sampleRateHz;
    cfg.padeGainPct   = params.padeGainPct;
    cfg.padeMaxCorr   = params.padeMaxCorr;
    cfg.biquadCutoffHz = params.biquadCutoffHz;
    cfg.notchCenterHz = params.notchCenterHz;
    cfg.notchQ10      = params.notchQ10;
    cfg.holtAlpha     = params.holtAlpha;
    cfg.holtBeta      = params.holtBeta;
    Tasks::EncoderTask_SetFilterConfig(cfg);
}

void ServiceDispatcher::encFilterSetLegacy(uint8_t type, uint8_t param) {
    Tasks::EncoderTask_SetFilter(type, param);
}

void ServiceDispatcher::configSetEncFilter(uint8_t type, uint8_t alpha) {
    Services::g_motorConfig.setEncFilter(type, alpha);
}

void ServiceDispatcher::configSetEncSmaWindow(uint8_t window) {
    Services::g_motorConfig.getConfigMutable().encSmaWindow = window;
}

void ServiceDispatcher::configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                                uint8_t smaWindow, uint8_t measWindowMs,
                                                uint8_t rateDiv) {
    Services::g_motorConfig.setEncFilterFull(flags, emaAlpha, smaWindow,
                                              measWindowMs, rateDiv);
}
