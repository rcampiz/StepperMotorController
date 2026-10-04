/**
 * @file command_processor_adapter.hpp
 * @brief Harness adapter: Protocol::ICommandProcessor --> Services::ICommandProcessor
 *
 * Provider is Protocol::ICommandProcessor (L2 command parser).
 * Consumer is Services::ICommandProcessor (L3 dispatch layer).
 */

#pragma once

#include "L2_protocol/boundary/icommand_processor.hpp"
#include "L3_services/boundary/icommand_processor.hpp"
#include "L3_services/boundary/idispatch_stats.hpp"
#include "harness/pins/idispatch_stats.hpp"

namespace Harness {

class CommandProcessorAdapter : public Services::ICommandProcessor {
public:
    explicit CommandProcessorAdapter(Protocol::ICommandProcessor& provider) : m_provider(provider) {}

    void process() override {
        m_provider.process();
    }

    void checkBaudRevert() override {
        m_provider.checkBaudRevert();
    }

    void formatEvent(Transport::ITransport& transport, const AsyncEvent& evt, uint32_t seq, uint32_t ts_ms) override {
        m_provider.formatEvent(transport, evt, seq, ts_ms);
    }

    void getDispatchStats(Services::DispatchStats& out) const override {
        Scheduler::DispatchStats tmp;
        m_provider.getDispatchStats(tmp);
        __builtin_memcpy(&out, &tmp, sizeof(out));
    }

private:
    Protocol::ICommandProcessor& m_provider;
};

} // namespace Harness
