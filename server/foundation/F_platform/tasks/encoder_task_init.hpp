/**
 * @file encoder_task_init.hpp
 * @brief Init contract: create the Encoder task bound to an L3 IEncoderService
 */
#pragma once

namespace Platform { class IClock; class IScheduler; }
namespace Services { class IEncoderService; }

namespace Harness {

bool initEncoderTask(Services::IEncoderService& service,
                     Platform::IClock& clock,
                     Platform::IScheduler& scheduler);

} // namespace Harness
