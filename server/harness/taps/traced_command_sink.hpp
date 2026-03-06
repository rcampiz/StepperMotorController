/**
 * @file traced_command_sink.hpp
 * @brief Traced wrapper for IMotorCommandSink (intra-L3 boundary)
 *
 * Logs motor commands sent from L3 services to the motor task.
 * Only compiled when ENABLE_INTERFACE_TRACE is defined.
 */

#pragma once

#include "harness/pins/imotor_command_sink.hpp"
#include "harness/trace/interface_trace.hpp"

#ifdef ENABLE_INTERFACE_TRACE

namespace Harness {

class TracedMotorCommandSink : public Harness::IMotorCommandSink {
public:
    explicit TracedMotorCommandSink(Harness::IMotorCommandSink& real) : m_real(real) {}

    bool sendCommand(const Harness::MotorCommand& cmd, uint32_t timeoutMs = 0) override {
        static const char* const names[] = {
            "Move", "GoTo", "Run", "SoftStop", "HardStop",
            "SoftHiZ", "HardHiZ", "GoHome", "GoMark", "ResetPos",
            "SetAccel", "SetDecel", "SetMaxSpd", "SetMark", "GetStatus"
        };
        auto idx = static_cast<uint8_t>(cmd.type);
        const char* name = (idx < sizeof(names)/sizeof(names[0])) ? names[idx] : "?";
        Harness::ITrace::log(Harness::ITrace::L3_CMD_SINK, "[L3~L3]", "cmdSink", name);
        return m_real.sendCommand(cmd, timeoutMs);
    }

private:
    Harness::IMotorCommandSink& m_real;
};

} // namespace Harness

#endif // ENABLE_INTERFACE_TRACE
