/**
 * @file encoder_dispatch.cpp
 * @brief L3 IEncoderDispatcher implementation
 */

#include "encoder_dispatch.hpp"
#include "harness/pins/iencoder.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

bool EncoderDispatch::isEncoderAvailable() {
    return m_encoder != nullptr && m_encoder->isAvailable();
}

Harness::IEncoderDispatcher::EncoderSnapshot EncoderDispatch::getEncoderSnapshot() {
    if (m_encoder == nullptr) {
        return {0, 0, false, 0, 0, 0};
    }
    Harness::EncoderSnapshot st = m_encoder->getState();
    return {st.count, st.velocity, st.indexSeen,
            st.indexTick, st.revolutions, st.indexPeriodUs};
}

void EncoderDispatch::encoderResetCount() {
    if (m_encoder != nullptr) m_encoder->resetCount();
}

void EncoderDispatch::encFilterGetConfig(EncFilterParams& out) {
    m_encFilter->getConfig(out);
}

void EncoderDispatch::encFilterSetConfig(const EncFilterParams& params) {
    m_encFilter->setConfig(params);
}

void EncoderDispatch::encFilterSetLegacy(uint8_t type, uint8_t param) {
    m_encFilter->setLegacy(type, param);
}

void EncoderDispatch::configSetEncFilter(uint8_t type, uint8_t alpha) {
    motion.encoder.config.setEncFilter(type, alpha);
}

void EncoderDispatch::configSetEncSmaWindow(uint8_t window) {
    motion.encoder.config.getConfigMutable().encSmaWindow = window;
}

void EncoderDispatch::configSetEncFilterFull(uint8_t flags, uint8_t emaAlpha,
                                              uint8_t smaWindow, uint8_t measWindowMs,
                                              uint8_t rateDiv) {
    motion.encoder.config.setEncFilterFull(flags, emaAlpha, smaWindow,
                                            measWindowMs, rateDiv);
}

} // namespace Services
