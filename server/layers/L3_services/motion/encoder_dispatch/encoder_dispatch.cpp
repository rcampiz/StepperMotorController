/**
 * @file encoder_dispatch.cpp
 * @brief L3 IEncoderDispatcher implementation
 */

#include "encoder_dispatch.hpp"
#include "L3_services/boundary/iencoder.hpp"
#include "L3_services/motion/motion.hpp"

namespace Services {

bool EncoderDispatch::isEncoderAvailable() {
    return m_encoder != nullptr && m_encoder->isAvailable();
}

Services::IEncoderDispatcher::EncoderSnapshot EncoderDispatch::getEncoderSnapshot() {
    if (m_encoder == nullptr) {
        return {0, 0, false, 0, 0, 0};
    }
    Services::EncoderSnapshot st = m_encoder->getState();
    return {st.count, st.velocity, st.indexSeen,
            st.indexTick, st.revolutions, st.indexPeriodUs};
}

void EncoderDispatch::encoderResetCount() {
    if (m_encoder != nullptr) m_encoder->resetCount();
}

bool EncoderDispatch::captureFreeze(uint32_t& samples, uint16_t& rateHz, bool& sync) {
    if (m_encoder == nullptr) return false;
    Services::IEncoder::CaptureInfo info;
    if (!m_encoder->captureFreeze(info)) return false;
    samples = info.samples;
    rateHz = info.rateHz;
    sync = info.sync;
    return true;
}

bool EncoderDispatch::captureRead(uint32_t i, uint16_t& enc, uint16_t& sync) {
    return m_encoder != nullptr && m_encoder->captureRead(i, enc, sync);
}

void EncoderDispatch::captureResume() {
    if (m_encoder != nullptr) m_encoder->captureResume();
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

void EncoderDispatch::configSetEncFilterFull(const EncFilterParams& cfg) {
    motion.encoder.config.setEncFilterFull(cfg);
}

} // namespace Services
