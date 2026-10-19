/**
 * @file fake_powerstep.cpp
 * @brief Fake powerSTEP01 physics (see fake_powerstep.hpp).
 */
#include "L4_drivers/devices/powerstep01_status.hpp"
#include "fake_powerstep.hpp"
#include "host_platform.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

using Drivers::MotorReg::ABS_POS;
using Drivers::MotorReg::SPEED;

namespace Sim {

namespace {
std::FILE* g_motionLog = nullptr;
std::mutex g_motionLogMutex;
}

void FakePowerStep::openMotionLog(const char* path) {
    g_motionLog = std::fopen(path, "w");
}

void FakePowerStep::logMotion(const char* what, long long value) {
    if (g_motionLog == nullptr) return;
    const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lk(g_motionLogMutex);
    std::fprintf(g_motionLog, "%lld %s %lld\n", ns, what, value);
    std::fflush(g_motionLog);
}

void FakePowerStep::resetRegisters() {
    // powerSTEP01 power-on defaults (voltage mode)
    for (auto& r : regs_) r = 0;
    regs_[Drivers::MotorReg::ACC] = 0x08A;
    regs_[Drivers::MotorReg::DEC] = 0x08A;
    regs_[Drivers::MotorReg::MAX_SPEED] = 0x041;
    regs_[Drivers::MotorReg::MIN_SPEED] = 0x000;
    regs_[Drivers::MotorReg::FS_SPD] = 0x027;
    regs_[Drivers::MotorReg::KVAL_HOLD] = 0x29;
    regs_[Drivers::MotorReg::KVAL_RUN] = 0x29;
    regs_[Drivers::MotorReg::KVAL_ACC] = 0x29;
    regs_[Drivers::MotorReg::KVAL_DEC] = 0x29;
    regs_[Drivers::MotorReg::INT_SPEED] = 0x0408;
    regs_[Drivers::MotorReg::OCD_TH] = 0x08;
    regs_[Drivers::MotorReg::STALL_TH] = 0x10;
    regs_[Drivers::MotorReg::ADC_OUT] = 2;       // a 12 V supply on the shield divider
    regs_[Drivers::MotorReg::STEP_MODE] = 0x07;
    regs_[Drivers::MotorReg::ALARM_EN] = 0xFF;
    regs_[Drivers::MotorReg::CONFIG] = 0x2C88;
}

void FakePowerStep::update() {
    const double now = nowSeconds();
    if (last_ < 0) last_ = now;
    double dt = now - last_;
    last_ = now;
    // integrate in small slices so profiles stay smooth even after a long gap
    while (dt > 0) {
        const double h = std::min(dt, 0.001);
        dt -= h;
        double want = 0.0;
        switch (mode_) {
        case Mode::Idle:
            v_ = 0.0;
            motStatus_ = 0;
            continue;
        case Mode::Run:
            want = targetV_;
            break;
        case Mode::Stopping:
            want = 0.0;
            break;
        case Mode::Position: {
            const double remainFs = (targetPos_ - pos_) / usPerFs();
            const double stopDist = v_ * v_ / (2.0 * dec());
            if (std::fabs(remainFs) <= 0.5 / usPerFs() && std::fabs(v_) < acc() * 0.002 + 1.0) {
                pos_ = targetPos_;
                v_ = 0.0;
                mode_ = Mode::Idle;
                motStatus_ = 0;
                continue;
            }
            const double dir = remainFs >= 0 ? 1.0 : -1.0;
            want = (std::fabs(remainFs) <= stopDist && v_ * dir > 0) ? 0.0 : dir * vmax();
            if (want == 0.0 && std::fabs(v_) < 1.0) want = dir * 1.0;   // creep the last bit
            break;
        }
        }
        const bool speedingUp = std::fabs(want) > std::fabs(v_) && want * v_ >= 0;
        const double a = speedingUp ? acc() : dec();
        if (std::fabs(want - v_) <= a * h) {
            v_ = want;
            motStatus_ = (v_ == 0.0) ? 0 : 3;
        } else {
            v_ += (want > v_ ? 1.0 : -1.0) * a * h;
            motStatus_ = speedingUp ? 1 : 2;
        }
        pos_ += v_ * h * usPerFs();
        if (mode_ == Mode::Stopping && v_ == 0.0) {
            mode_ = Mode::Idle;
            if (hiZAfterStop_) hiZ_ = true;
        }
    }
    // ABS_POS saturates/wraps at 22 bits like the chip
    const double span = 4194304.0;
    while (pos_ >= span / 2) pos_ -= span;
    while (pos_ < -span / 2) pos_ += span;
}

void FakePowerStep::init() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    resetRegisters();
    pos_ = v_ = targetV_ = targetPos_ = 0.0;
    mode_ = Mode::Idle;
    hiZ_ = true;
    last_ = nowSeconds();
}

void FakePowerStep::setStandby(bool standby) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("standby", standby ? 1 : 0);
    if (standby) {
        update();
        v_ = 0.0;
        mode_ = Mode::Idle;
        hiZ_ = true;
        resetRegisters();                       // STBY/RESET low resets every register
    }
}

void FakePowerStep::run(bool forward, uint32_t speed) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("run", forward ? static_cast<long long>(speed) : -static_cast<long long>(speed));
    update();
    const double fs = std::min(speed / SPEED_LSB, vmax());
    targetV_ = forward ? fs : -fs;
    mode_ = Mode::Run;
    hiZ_ = false;
    hiZAfterStop_ = false;
}

void FakePowerStep::move(bool forward, uint32_t steps) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("move", forward ? static_cast<long long>(steps) : -static_cast<long long>(steps));
    update();
    targetPos_ = pos_ + (forward ? 1.0 : -1.0) * static_cast<double>(steps);
    mode_ = Mode::Position;
    hiZ_ = false;
    hiZAfterStop_ = false;
}

void FakePowerStep::goTo(int32_t position) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("goto", position);
    update();
    int32_t p = position & 0x3FFFFF;
    if (p & 0x200000) p -= 0x400000;
    targetPos_ = p;
    mode_ = Mode::Position;
    hiZ_ = false;
    hiZAfterStop_ = false;
}

void FakePowerStep::softStop() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("softstop", 0);
    update();
    if (mode_ != Mode::Idle) mode_ = Mode::Stopping;
    hiZ_ = false;            // as hardStop: SoftStop from Hi-Z energises the bridges
    hiZAfterStop_ = false;
}

void FakePowerStep::hardStop() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("hardstop", 0);
    update();
    v_ = 0.0;
    mode_ = Mode::Idle;
    hiZ_ = false;            // a stop command exits Hi-Z (datasheet: the bridges hold the motor)
}

void FakePowerStep::softHiZ() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("softhiz", 0);
    update();
    if (mode_ == Mode::Idle) {
        hiZ_ = true;
    } else {
        mode_ = Mode::Stopping;
        hiZAfterStop_ = true;
    }
}

void FakePowerStep::hardHiZ() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    logMotion("hardhiz", 0);
    update();
    v_ = 0.0;
    mode_ = Mode::Idle;
    hiZ_ = true;
}

void FakePowerStep::goHome() { goTo(0); }

void FakePowerStep::goMark() {
    goTo(static_cast<int32_t>(regs_[Drivers::MotorReg::MARK]));
}

void FakePowerStep::resetPos() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    pos_ = 0.0;
    if (mode_ == Mode::Position) targetPos_ = 0.0;
}

uint32_t FakePowerStep::getParam(uint8_t reg) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    if (reg == ABS_POS) {
        return static_cast<uint32_t>(static_cast<int32_t>(std::lround(pos_))) & 0x3FFFFF;
    }
    if (reg == SPEED) {
        return static_cast<uint32_t>(std::lround(std::fabs(v_) * SPEED_LSB)) & 0xFFFFF;
    }
    return reg < 32 ? regs_[reg] : 0;
}

void FakePowerStep::setParam(uint8_t reg, uint32_t value) {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    if (reg == ABS_POS) {
        int32_t p = value & 0x3FFFFF;
        if (p & 0x200000) p -= 0x400000;
        pos_ = p;
        return;
    }
    if (reg == Drivers::MotorReg::STEP_MODE && !hiZ_) {
        // STEP_MODE is writable only with the bridges in Hi-Z (datasheet "WH" register);
        // measured on hardware 2026-10-17: the write is silently ignored otherwise.
        logMotion("stepmode-ignored", static_cast<long long>(value));
        return;
    }
    if (reg < 32) regs_[reg] = value;
}

Drivers::MotorStatus FakePowerStep::getStatus() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    uint16_t s = 0;
    if (hiZ_) s |= 1u << 0;
    if (mode_ == Mode::Idle) s |= 1u << 1;            // BUSY is active low
    if (v_ > 0 || (v_ == 0 && targetV_ >= 0)) s |= 1u << 4;
    s |= static_cast<uint16_t>((motStatus_ & 3) << 5);
    s |= (1u << 9) | (1u << 13) | (1u << 14) | (1u << 15);   // no UVLO/OCD/stall
    return Drivers::decodePowerStepStatus(s);
}

bool FakePowerStep::isBusy() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    return mode_ != Mode::Idle;
}

double FakePowerStep::positionMicrosteps() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    update();
    return pos_;
}

int FakePowerStep::microstepsPerFullStep() {
    std::lock_guard<std::recursive_mutex> lk(m_);
    return usPerFs();
}

} // namespace Sim
