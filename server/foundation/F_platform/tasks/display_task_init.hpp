/**
 * @file display_task_init.hpp
 * @brief Init contract: create the Display task bound to an L3 IDisplayService
 */
#pragma once

namespace Platform { class IClock; class IScheduler; }
namespace Services { class IDisplayService; }

namespace Scheduler {

bool initDisplayTask(Services::IDisplayService& service,
                     Platform::IClock& clock,
                     Platform::IScheduler& scheduler);

} // namespace Scheduler
