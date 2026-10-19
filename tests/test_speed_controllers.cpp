/**
 * @file test_speed_controllers.cpp
 * @brief Driver boundary step 2: the speed trim and sysid speak full steps/s and command the
 *        same physical speeds as the raw-register versions they replaced.
 *
 * The old controllers produced powerSTEP01 SPEED register values; the new ones produce full
 * steps/s and the driver converts (Drivers::PowerStepUnits). The reference below is the old
 * speed-trim update copied from the pre-step-2 source, fed the same inputs.
 */
#include "minitest.hpp"
#include "L3_services/motion/speed_trim_controller/speed_trim_controller.hpp"
#include "L3_services/motion/sysid/sysid.hpp"
#include "L4_drivers/devices/powerstep01_units.hpp"
#include <math.h>
#include <stdint.h>

namespace {

namespace PU = Drivers::PowerStepUnits;

uint32_t oldSpeedRaw(uint32_t sps) {               // motion_service / sysid before step 2
    return static_cast<uint32_t>((static_cast<uint64_t>(sps) * 1048576ULL) / 15625ULL);
}

/// The pre-step-2 SpeedTrimController::update (raw SPEED in, raw SPEED out), verbatim maths
struct OldTrim {
    Services::SpeedTrimConfig cfg{};
    float integral = 0.0f;
    int32_t setpointTps = 0;
    uint32_t baseSpeedRaw = 0;

    struct Out { uint32_t finalSpeedRaw; int32_t trimTps; float pTerm, iTerm; bool frozen; };

    Out update(int32_t encVelTps, VelocityQuality q, uint16_t fpr, uint16_t ppr,
               uint32_t maxSpeedRaw, float dtSec) {
        Out o{baseSpeedRaw, 0, 0.0f, 0.0f, false};
        float velError = static_cast<float>(setpointTps - encVelTps);
        bool freeze = (q == VelocityQuality::STALE || q == VelocityQuality::INVALID)
                   || (setpointTps > -50 && setpointTps < 50);
        o.frozen = freeze;
        if (!freeze) {
            float absError = velError < 0.0f ? -velError : velError;
            if (absError >= cfg.deadband) {
                integral += velError * dtSec;
                if (integral > cfg.integralLimit)  integral = cfg.integralLimit;
                if (integral < -cfg.integralLimit) integral = -cfg.integralLimit;
            }
        }
        float pTerm = cfg.kp * velError;
        float iTerm = cfg.ki * integral;
        float trimTps = pTerm + iTerm;
        if (trimTps > cfg.outputLimit)  trimTps = cfg.outputLimit;
        if (trimTps < -cfg.outputLimit) trimTps = -cfg.outputLimit;
        o.pTerm = pTerm; o.iTerm = iTerm; o.trimTps = static_cast<int32_t>(trimTps);
        float trimSps = trimTps * static_cast<float>(fpr) / static_cast<float>(ppr);
        float baseSpsF = static_cast<float>(baseSpeedRaw) * 15625.0f / 1048576.0f;
        float maxTrimSps = baseSpsF * static_cast<float>(cfg.trimMaxPercent) / 100.0f;
        if (maxTrimSps < 1.0f) maxTrimSps = 1.0f;
        if (trimSps > maxTrimSps)  trimSps = maxTrimSps;
        if (trimSps < -maxTrimSps) trimSps = -maxTrimSps;
        float finalSpsF = baseSpsF + trimSps;
        if (finalSpsF < 0.0f) finalSpsF = 0.0f;
        float maxSpsF = static_cast<float>(maxSpeedRaw) * 15625.0f / 1024.0f;
        if (finalSpsF > maxSpsF) finalSpsF = maxSpsF;
        o.finalSpeedRaw = static_cast<uint32_t>((finalSpsF * 1048576.0f / 15625.0f) + 0.5f);
        return o;
    }
};

} // namespace

TEST(speed_trim_commands_the_same_physical_speed_as_the_raw_version) {
    // Bench-like setup: 200 full steps/rev, 4000-count encoder, RUN 1000 steps/s (20000 tps)
    Services::SpeedTrimConfig cfg{};
    cfg.kp = 0.4f; cfg.ki = 2.0f; cfg.outputLimit = 500.0f; cfg.integralLimit = 400.0f;
    cfg.deadband = 50.0f; cfg.trimMaxPercent = 8;
    const uint16_t fpr = 200, ppr = 4000;
    const uint32_t runSps = 1000;
    const int32_t setpointTps = static_cast<int32_t>(runSps * ppr / fpr);

    Services::SpeedTrimController trim;
    trim.configure(cfg);
    trim.beginTrim(setpointTps, true, static_cast<float>(runSps));
    OldTrim old;
    old.cfg = cfg; old.cfg.deadband = 50.0f;
    old.setpointTps = setpointTps;
    old.baseSpeedRaw = oldSpeedRaw(runSps);

    // A slipping motor, then overshoot, a bad sample, and a low MAX_SPEED that clamps
    const int32_t vel[] = {19000, 19200, 19350, 19500, 19700, 19900, 20100, 20400, 20600,
                           20300, 20000, 19950, 19000, 18000, 18500, 21000, 22000, 20000};
    int maxLsbDiff = 0, telemetryMismatch = 0;
    float maxFspsDiff = 0.0f;
    for (unsigned i = 0; i < sizeof(vel) / sizeof(vel[0]); ++i) {
        const auto q = (i == 7) ? VelocityQuality::STALE : VelocityQuality::GOOD;
        const uint32_t maxSpeedReg = (i >= 15) ? 66u : 0x3FFu;     // 66 x 15.26 = 1007 steps/s
        Services::SpeedTrimInput in{};
        in.encVelTps = vel[i]; in.velQuality = q; in.forward = true;
        in.fullStepsPerRev = fpr; in.encoderPPR = ppr; in.dtSec = 0.050f;
        in.maxSpeedFsps = PU::maxSpeedRegToFsps(maxSpeedReg);   // what getMaxSpeed() returns
        const Services::SpeedTrimResult n = trim.update(in);
        const OldTrim::Out o = old.update(vel[i], q, fpr, ppr, maxSpeedReg, 0.050f);

        // Telemetry in encoder units is untouched by the change
        if (n.trimTps != o.trimTps || n.pTerm != o.pTerm || n.iTerm != o.iTerm || n.frozen != o.frozen) {
            ++telemetryMismatch;
        }
        // The speed the chip receives: within one SPEED LSB (old rounded, the driver truncates)
        const int d = static_cast<int>(PU::fspsToSpeedReg(n.finalSpeedFsps)) - static_cast<int>(o.finalSpeedRaw);
        if (d > maxLsbDiff || -d > maxLsbDiff) maxLsbDiff = d < 0 ? -d : d;
        const float df = fabsf(n.finalSpeedFsps - PU::speedRegToFsps(o.finalSpeedRaw));
        if (df > maxFspsDiff) maxFspsDiff = df;
        EXPECT_TRUE(n.active);
        EXPECT_TRUE(n.baseSpeedFsps == 1000.0f);
    }
    EXPECT_EQ(telemetryMismatch, 0);
    EXPECT_TRUE(maxLsbDiff <= 1);
    EXPECT_TRUE(maxFspsDiff < 0.03f);                 // < 2 LSB of 0.0149 steps/s
}

TEST(speed_trim_clamps_to_the_percentage_cap_and_the_drivers_ceiling) {
    Services::SpeedTrimConfig cfg{};
    cfg.kp = 10.0f; cfg.ki = 0.0f; cfg.outputLimit = 5000.0f; cfg.trimMaxPercent = 5;
    Services::SpeedTrimController trim;
    trim.configure(cfg);
    trim.beginTrim(10000, true, 500.0f);              // 500 steps/s, 10000 tps
    Services::SpeedTrimInput in{};
    in.encVelTps = 5000; in.velQuality = VelocityQuality::GOOD; in.forward = true;
    in.fullStepsPerRev = 200; in.encoderPPR = 4000; in.dtSec = 0.05f;
    in.maxSpeedFsps = PU::maxSpeedRegToFsps(0x3FFu);
    Services::SpeedTrimResult r = trim.update(in);
    EXPECT_TRUE(r.finalSpeedFsps == 525.0f);          // capped at +5 %
    in.maxSpeedFsps = PU::maxSpeedRegToFsps(33u);     // 503.5 steps/s ceiling
    r = trim.update(in);
    EXPECT_TRUE(r.finalSpeedFsps == PU::maxSpeedRegToFsps(33u));
    in.encVelTps = 20000;                             // too fast: trims down, never below 0
    in.maxSpeedFsps = PU::maxSpeedRegToFsps(0x3FFu);
    r = trim.update(in);
    EXPECT_TRUE(r.finalSpeedFsps == 475.0f);
}

TEST(sysid_commands_whole_steps_per_second_that_encode_like_the_old_raw_speeds) {
    Services::SysIdRunner sysid;
    Services::SysIdConfig cfg{};
    cfg.type = Services::SysIdTestType::RAMP;
    cfg.startSpeed_sps = 100; cfg.targetSpeed_sps = 1100;
    cfg.forward = false; cfg.settleDuration_ms = 100; cfg.activeDuration_ms = 1000;
    EXPECT_TRUE(sysid.start(cfg));
    int ticks = 0, active = 0, mismatches = 0;
    float last = -1.0f;
    for (; ticks < 100 && sysid.getPhase() != Services::SysIdPhase::DONE; ++ticks) {
        const Services::SysIdRunner::TickResult r = sysid.tick(0, 0, 200, 4000);
        EXPECT_FALSE(r.forward);
        if (sysid.getPhase() == Services::SysIdPhase::ACTIVE || (r.active && r.speedFsps > 0.0f)) {
            ++active;
            const auto sps = static_cast<uint32_t>(r.speedFsps);
            if (static_cast<float>(sps) != r.speedFsps) ++mismatches;              // whole steps/s
            if (PU::fspsToSpeedReg(r.speedFsps) != oldSpeedRaw(sps)) ++mismatches;  // same register
            if (r.speedFsps < last) ++mismatches;                                   // a rising ramp
            last = r.speedFsps;
        }
    }
    EXPECT_EQ(mismatches, 0);
    EXPECT_TRUE(active >= 20);
    EXPECT_TRUE(sysid.getPhase() == Services::SysIdPhase::DONE);

    // STEP: the target itself, and 0 (stop) in the settle phases
    Services::SysIdRunner step;
    Services::SysIdConfig sc{};
    sc.type = Services::SysIdTestType::STEP; sc.targetSpeed_sps = 800; sc.forward = true;
    sc.settleDuration_ms = 50; sc.activeDuration_ms = 100;
    EXPECT_TRUE(step.start(sc));
    Services::SysIdRunner::TickResult r = step.tick(0, 0, 200, 4000);   // pre-settle
    EXPECT_TRUE(r.active && r.speedFsps == 0.0f);
    r = step.tick(0, 0, 200, 4000);                                       // active
    EXPECT_TRUE(r.active && r.speedFsps == 800.0f);
    EXPECT_EQ(PU::fspsToSpeedReg(r.speedFsps), oldSpeedRaw(800));       // 53687, as before
}
