/**
 * @file motion.cpp
 * @brief MotionGroup definition — binds references to existing service globals
 */

#include "L3_services/motion/motion.hpp"

namespace Services {

// Config views (file-scope statics — initialized before MotionGroup)
static StepperConfig  s_stepperConfig(g_motorConfig);
static EncoderConfig  s_encoderConfig(g_motorConfig);
static ControlConfig  s_controlConfig(g_motorConfig);

MotionGroup motion{
    // stepper
    { g_motorController, s_stepperConfig, g_motorEvent },
    // encoder
    { g_encoderProcessor, s_encoderConfig },
    // control
    { g_controlMode, g_supervisor, g_speedTrim, g_sysId, s_controlConfig },
    // configStore
    g_motorConfig
};

} // namespace Services
