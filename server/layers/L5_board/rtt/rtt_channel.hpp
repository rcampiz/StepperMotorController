/**
 * @file rtt_channel.hpp
 * @brief SEGGER RTT byte channel — L5 board implementation of Transport::IByteChannel
 *
 * Moved from L4_drivers/rtt/rtt_driver.hpp (rule 3): host-command bytes are the one
 * L5 -> L1 exception, and both byte channels (UART, RTT) now live in L5.
 */
#pragma once

#include "harness/defs/harness_l5_l1_bytes.hpp"
#include "X_middlewares/SEGGER/RTT/SEGGER_RTT.h"

namespace Board {

class RttChannel : public Transport::IByteChannel {
public:
    explicit RttChannel(unsigned channel) : m_channel(channel) {}

    bool init() override { return true; }   // RTT auto-initialises on first use

    bool hasData() override { return SEGGER_RTT_HasData(m_channel) > 0; }

    size_t read(uint8_t* buffer, size_t maxLen) override {
        return SEGGER_RTT_Read(m_channel, buffer, maxLen);
    }

    size_t write(const uint8_t* data, size_t len) override {
        return SEGGER_RTT_Write(m_channel, data, len);
    }

private:
    unsigned m_channel;
};

} // namespace Board
