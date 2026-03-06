/**
 * @file encoder.cpp
 * @brief Encoder::init() definition
 *
 * Moved from wiring/wire_encoder.cpp to proper L4 location.
 */

#include "L4_drivers/devices/encoder.hpp"

namespace Drivers {

bool Encoder::init()
{
    m_status = Status::INITIALIZING;
    m_indexSeen = false;
    m_indexTick = 0;
    m_revolutions = 0;
    m_lastIndexUs = 0;
    m_indexPeriodUs = 0;
    m_status = Status::READY;
    return true;
}

} // namespace Drivers
