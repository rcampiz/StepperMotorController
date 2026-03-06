/**
 * @file motion.hpp
 * @brief Umbrella header — all motion services + hierarchical MotionGroup
 */

#ifndef L3_MOTION_HPP
#define L3_MOTION_HPP

#include "L3_services/motion/motion_service/motion_service.hpp"
#include "L3_services/motion/motor_config/motor_config.hpp"
#include "L3_services/motion/motor_config/stepper_config.hpp"
#include "L3_services/motion/motor_config/encoder_config.hpp"
#include "L3_services/motion/motor_config/control_config.hpp"
#include "L3_services/motion/control_mode/control_mode.hpp"
#include "L3_services/motion/motor_event_service/motor_event_service.hpp"
#include "L3_services/motion/following_supervisor/following_supervisor.hpp"
#include "L3_services/motion/speed_trim_controller/speed_trim_controller.hpp"
#include "L3_services/motion/sysid/sysid.hpp"
#include "L3_services/motion/encoder_processor/encoder_processor.hpp"
#include "L3_services/motion/motor_controller/motor_controller.hpp"

namespace Services {

struct StepperGroup {
    MotorController&   controller;
    StepperConfig&     config;
    MotorEventManager& event;
};

struct EncoderGroup {
    EncoderProcessor& processor;
    EncoderConfig&    config;
};

struct ControlGroup {
    ControlModeManager&   mode;
    FollowingSupervisor&  supervisor;
    SpeedTrimController&  trim;
    SysIdRunner&          sysId;
    ControlConfig&        config;
};

/**
 * @brief Hierarchical grouping of all motion services.
 *
 * Three sub-groups (stepper, encoder, control) plus a config store
 * for flash persistence operations (init/save/load/reset).
 */
struct MotionGroup {
    StepperGroup        stepper;
    EncoderGroup        encoder;
    ControlGroup        control;
    MotorConfigManager& configStore;
};

extern MotionGroup motion;

} // namespace Services

#endif // L3_MOTION_HPP
