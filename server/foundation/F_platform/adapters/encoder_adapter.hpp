/**
 * @file encoder_adapter.hpp
 * @brief Foundation adapter: EncoderTask APIs -> IEncoder
 *
 * Thin wrapper delegating IEncoder calls to EncoderTask_* free functions.
 * Header-only — all methods are trivial one-liners.
 */

#pragma once

#include "L3_services/motion/iencoder.hpp"
#include "F_platform/tasks/encoder_task.hpp"

class EncoderAdapter : public Services::IEncoder {
public:
    Services::EncoderSnapshot getState() override {
        Tasks::EncoderState st = Tasks::EncoderTask_GetState();
        Services::EncoderSnapshot snap{};
        snap.count           = st.count;
        snap.velocity        = st.velocity;
        snap.indexSeen       = st.indexSeen;
        snap.indexTick       = st.indexTick;
        snap.revolutions     = st.revolutions;
        snap.indexPeriodUs   = st.indexPeriodUs;
        snap.velocityQuality = st.velocityQuality;
        return snap;
    }

    int32_t getCount() override {
        return Tasks::EncoderTask_GetCount();
    }

    void resetCount() override {
        Tasks::EncoderTask_ResetCount();
    }

    void clearIndexFlag() override {
        Tasks::EncoderTask_ClearIndexFlag();
    }

    bool isAvailable() override {
        return Tasks::EncoderTask_IsAvailable();
    }
};
