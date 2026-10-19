/**
 * @file fake_stepdir.cpp
 * @brief The STEP/DIR board's fake hardware (see fake_stepdir.hpp).
 */
#include "fake_stepdir.hpp"

#include <chrono>

namespace Sim {

// --- Step pulse timer ------------------------------------------------------------------------

FakeStepPulseTimer::FakeStepPulseTimer(uint32_t tickHz, uint32_t minPeriodTicks)
    : tickHz_(tickHz != 0 ? tickHz : 1), minTicks_(minPeriodTicks != 0 ? minPeriodTicks : 1)
{
    // The "hardware" keeps time on its own: pulses happen even when nobody looks
    thread_ = std::thread([this]() {
        while (!quit_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            service();
        }
    });
}

FakeStepPulseTimer::~FakeStepPulseTimer()
{
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
}

void FakeStepPulseTimer::setHandler(PulseHandler handler, void* context)
{
    std::lock_guard<std::recursive_mutex> lk(m_);
    handler_ = handler;
    context_ = context;
}

void FakeStepPulseTimer::start(uint32_t firstPeriodTicks)
{
    std::lock_guard<std::recursive_mutex> lk(m_);
    next_ = nowSeconds() + static_cast<double>(firstPeriodTicks) / tickHz_;
    on_ = true;
}

void FakeStepPulseTimer::stop()
{
    std::lock_guard<std::recursive_mutex> lk(m_);
    on_ = false;
}

bool FakeStepPulseTimer::running() const
{
    std::lock_guard<std::recursive_mutex> lk(m_);
    return on_;
}

void FakeStepPulseTimer::service()
{
    std::lock_guard<std::recursive_mutex> lk(m_);
    serviceLocked();
}

void FakeStepPulseTimer::serviceLocked()
{
    if (!on_ || inService_) return;
    inService_ = true;
    const double now = nowSeconds();
    while (on_ && next_ <= now) {
        if (line_) line_();                      // the edge, then its interrupt
        const uint32_t period = handler_ != nullptr ? handler_(context_) : 0u;
        if (period == 0) {
            on_ = false;
            break;
        }
        next_ += static_cast<double>(period) / tickHz_;
    }
    inService_ = false;
}

void FakeStepPulseTimer::Mask::enter()
{
    t_.m_.lock();
    if (depth_++ == 0) t_.serviceLocked();       // pulses due before the mask went up
}

void FakeStepPulseTimer::Mask::exit()
{
    --depth_;
    t_.m_.unlock();
}

// --- The chip --------------------------------------------------------------------------------

void FakeStepDirChip::stepEdge()
{
    if (nEnable_.read()) return;                 // bridges off: the rotor does not follow
    const unsigned k = (mode0_.read() ? 1u : 0u) | (mode1_.read() ? 2u : 0u) | (mode2_.read() ? 4u : 0u);
    const long long inc = 32 >> (k > 5u ? 5u : k);   // 1/32 full steps per edge
    std::lock_guard<std::mutex> lk(m_);
    rotor32_ += dir_.read() ? inc : -inc;
}

double FakeStepDirChip::shaftFullSteps()
{
    timer_.service();
    std::lock_guard<std::mutex> lk(m_);
    return static_cast<double>(rotor32_) / 32.0;
}

// --- The board -------------------------------------------------------------------------------

namespace {
Drivers::StepDirDriver::Pins pinsOf(StepDirBoard& b)
{
    Drivers::StepDirDriver::Pins p;
    p.dir = &b.dir;
    p.nEnable = &b.nEnable;
    p.mode[0] = &b.mode0;
    p.mode[1] = &b.mode1;
    p.mode[2] = &b.mode2;
    p.nFault = &b.nFault;
    return p;
}
}

StepDirBoard::StepDirBoard() : driver(timer, pinsOf(*this), timer.isrMask())
{
    timer.connect([this]() { chip.stepEdge(); });
}

} // namespace Sim
