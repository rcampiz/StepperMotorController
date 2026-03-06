/**
 * @file icritical_section.hpp
 * @brief Abstract critical section interface
 *
 * Decouples task code from FreeRTOS taskENTER_CRITICAL/taskEXIT_CRITICAL.
 * Implemented by Platform::FreeRTOSCriticalSection.
 */

#pragma once

namespace Harness {

class ICriticalSection {
public:
    virtual ~ICriticalSection() = default;

    virtual void enter() = 0;
    virtual void exit() = 0;
};

} // namespace Harness
