/**
 * @file queue_adapter.hpp
 * @brief Harness adapter: Platform::IQueue<T,D> --> Services::IQueue<T,D>
 *
 * Templated forwarder — both interfaces are structurally identical.
 * Unlike non-template adapters, these cannot use the HARNESS() macro.
 * Create static instances manually at wiring points.
 */

#pragma once

#include "F_platform/boundary/iqueue.hpp"
#include "L3_services/boundary/iqueue.hpp"

namespace Harness {

template<typename T, size_t Depth>
class QueueAdapter : public Services::IQueue<T, Depth> {
public:
    explicit QueueAdapter(Platform::IQueue<T, Depth>& provider) : m_provider(provider) {}

    bool send(const T& item, uint32_t timeoutMs = 0) override {
        return m_provider.send(item, timeoutMs);
    }

    bool receive(T& item, uint32_t timeoutMs = 0) override {
        return m_provider.receive(item, timeoutMs);
    }

    size_t available() const override { return m_provider.available(); }
    size_t freeSlots() const override { return Depth - m_provider.available(); }
    void reset() override { m_provider.reset(); }

private:
    Platform::IQueue<T, Depth>& m_provider;
};

} // namespace Harness
