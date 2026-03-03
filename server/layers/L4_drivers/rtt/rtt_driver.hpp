/**
 * @file rtt_driver.hpp
 * @brief SEGGER RTT channel driver
 *
 * Wraps the SEGGER RTT vendor library behind the IRttChannel interface
 * so that L1 transport code never includes vendor headers directly.
 *
 * Header-only — the SEGGER RTT functions are provided by the
 * pre-compiled RTT library linked into the firmware.
 */

#ifndef RTT_DRIVER_HPP
#define RTT_DRIVER_HPP

#include "F_platform/hal/irtt_channel.hpp"
#include "X_middlewares/SEGGER/RTT/SEGGER_RTT.h"

class RttDriver : public IRttChannel {
public:
    explicit RttDriver(unsigned channel) : m_channel(channel) {}

    bool init() override {
        // RTT is auto-initialized by SEGGER_RTT_Init() on first use
        return true;
    }

    bool hasData() override {
        return SEGGER_RTT_HasData(m_channel) > 0;
    }

    size_t read(void* buffer, size_t maxLen) override {
        return SEGGER_RTT_Read(m_channel, buffer, maxLen);
    }

    size_t write(const void* data, size_t len) override {
        return SEGGER_RTT_Write(m_channel, data, len);
    }

private:
    unsigned m_channel;
};

#endif // RTT_DRIVER_HPP
