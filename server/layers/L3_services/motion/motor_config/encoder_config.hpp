/**
 * @file encoder_config.hpp
 * @brief Encoder configuration view
 *
 * Header-only view into MotorConfigManager exposing only encoder-domain fields:
 * PPR, filter type/param, SMA window, measurement window, sample rate.
 */

#pragma once

#include "L3_services/motion/motor_config/motor_config.hpp"

namespace Services {

class EncoderConfig {
public:
    explicit EncoderConfig(MotorConfigManager& mgr) : m_mgr(mgr) {}

    // Encoder physical parameters
    uint16_t getEncoderPPR() const { return m_mgr.getEncoderPPR(); }
    void setEncoderPPR(uint16_t ppr) { m_mgr.setEncoderPPR(ppr); }

    // Filter settings
    uint8_t getEncFilterType() const { return m_mgr.getEncFilterType(); }
    uint8_t getEncFilterParam() const { return m_mgr.getEncFilterParam(); }
    uint8_t getEncSmaWindow() const { return m_mgr.getEncSmaWindow(); }
    uint8_t getEncMeasWindowMs() const { return m_mgr.getEncMeasWindowMs(); }
    uint16_t getEncSampleRateHz() const { return m_mgr.getEncSampleRateHz(); }

    void setEncFilter(uint8_t type, uint8_t param) {
        m_mgr.setEncFilter(type, param);
    }
    void setEncFilterFull(uint8_t flags, uint8_t emaAlpha, uint8_t smaWindow,
                          uint8_t measWindowMs, uint8_t sampleRateDiv100) {
        m_mgr.setEncFilterFull(flags, emaAlpha, smaWindow, measWindowMs, sampleRateDiv100);
    }

    // Direct mutable access for fields not covered by setters
    MotorConfig& getConfigMutable() { return m_mgr.getConfigMutable(); }

private:
    MotorConfigManager& m_mgr;
};

} // namespace Services
