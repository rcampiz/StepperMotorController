/**
 * @file traced_command_sink.hpp
 * @brief Traced wrapper for IMotorCommandSink (intra-L3 boundary)
 *
 * Logs motor commands sent from L3 services to the motor task.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "L3_services/dispatch/imotor_command_sink.hpp"
#include "F_util/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

class TracedMotorCommandSink : public Services::IMotorCommandSink {
public:
    explicit TracedMotorCommandSink(Services::IMotorCommandSink& real) : m_real(real) {}

    bool sendCommand(const Services::MotorCommand& cmd, uint32_t timeoutMs = 0) override {
        static const char* const names[] = {
            "Move", "GoTo", "Run", "SoftStop", "HardStop",
            "SoftHiZ", "HardHiZ", "GoHome", "GoMark", "ResetPos",
            "SetAccel", "SetDecel", "SetMaxSpd", "SetMark", "GetStatus"
        };
        uint8_t idx = static_cast<uint8_t>(cmd.type);
        const char* name = (idx < sizeof(names)/sizeof(names[0])) ? names[idx] : "?";
        ITrace::log(ITrace::L3_CMD_SINK, "[L3~L3]", "cmdSink", name);
        return m_real.sendCommand(cmd, timeoutMs);
    }

private:
    Services::IMotorCommandSink& m_real;
};

#endif // ENABLE_INTERFACE_TRACE
