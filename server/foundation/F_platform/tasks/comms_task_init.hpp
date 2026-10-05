/**
 * @file comms_task_init.hpp
 * @brief Init contract: create the Comms task bound to an L3 ICommsService
 */
#pragma once

namespace Platform { class IClock; class IScheduler; class ITaskNotifier; }
namespace Services { class ICommsService; }

namespace Harness {

bool initCommsTask(Services::ICommsService& service,
                   Platform::IClock& clock,
                   Platform::IScheduler& scheduler,
                   Platform::ITaskNotifier* rxWake);

} // namespace Harness
