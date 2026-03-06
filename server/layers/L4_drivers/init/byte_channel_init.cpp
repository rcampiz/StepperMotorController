/**
 * @file byte_channel_init.cpp
 * @brief Implements Drivers::initByteChannel()
 *
 * VCP_UART path: cascades to L5 comms_board (through harness) for UartHardware.
 * RTT path: constructs L4 internal RttDriver.
 */

#include "L4_drivers/init/byte_channel_init.hpp"

// L5 board init through harness (for UART path)
#include "harness/pins/comms_board.hpp"

// L4 internal (for RTT path)
#include "L4_drivers/rtt/rtt_driver.hpp"

bool Drivers::initByteChannel(Drivers::ByteChannelResult& out, Drivers::ChannelType type)
{
    switch (type) {
    case ChannelType::VCP_UART: {
        Harness::CommsBoardHw hw;
        if (!Harness::initCommsBoard(hw)) {
            return false;
        }
        out.channel = hw.channel;
    } break;

    case ChannelType::RTT: {
        static Drivers::RttDriver rttHw(0);
        out.channel = &rttHw;
    } break;
    }

    return out.channel != nullptr;
}
