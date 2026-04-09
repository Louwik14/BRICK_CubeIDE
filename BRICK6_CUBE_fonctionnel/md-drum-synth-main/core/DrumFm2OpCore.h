#pragma once

#include "plaits/dsp/fm/operator.h"

class DrumFm2OpCore {
public:
    struct FrameConfig {
        float carrier_freq_hz = 50.0f;
        float mod_freq_hz = 180.0f;
        float mod_index = 20.0f;
        float feedback_amount = 0.0f;
        float pitch_sweep_hz = 0.0f;
        bool mod_freq_tracks_pitch_sweep = false;
    };

    static constexpr float kSampleRate = 48000.0f;

    void Init() {
        t_ = 0.0f;
        amp_env_ = 1.0f;
        mod_env_ = 1.0f;
        freq_env_ = 1.0f;
        amp_decay_const_ = 0.0f;
        mod_decay_const_ = 0.0f;
        freq_decay_const_ = 0.0f;
        ops_[0].Reset();
        ops_[1].Reset();
        fb_state_[0] = 0.0f;
        fb_state_[1] = 0.0f;
    }

    void Trigger(float amp_decay_s, float mod_decay_s, float freq_decay_s) {
        Init();

        const float dt = 1.0f / kSampleRate;
        amp_decay_const_ = 1.0f - (dt / amp_decay_s);
        mod_decay_const_ = 1.0f - (dt / mod_decay_s);
        freq_decay_const_ = 1.0f - (dt / freq_decay_s);

        if (amp_decay_const_ < 0.0f) amp_decay_const_ = 0.0f;
        if (mod_decay_const_ < 0.0f) mod_decay_const_ = 0.0f;
        if (freq_decay_const_ < 0.0f) freq_decay_const_ = 0.0f;
    }

    float ProcessSample(const FrameConfig& cfg) {
        const float dt = 1.0f / kSampleRate;
        t_ += dt;

        amp_env_ *= amp_decay_const_;
        mod_env_ *= mod_decay_const_;
        freq_env_ *= freq_decay_const_;

        const float freq_env_scaled = cfg.pitch_sweep_hz * freq_env_;
        const float mod_freq_hz = cfg.mod_freq_hz + (cfg.mod_freq_tracks_pitch_sweep ? freq_env_scaled : 0.0f);

        float f[2];
        float a[2];
        f[0] = mod_freq_hz / kSampleRate;
        f[1] = (cfg.carrier_freq_hz + freq_env_scaled) / kSampleRate;
        a[0] = cfg.mod_index * mod_env_;
        a[1] = amp_env_;

        float out = 0.0f;
        const int fb_amt = static_cast<int>(cfg.feedback_amount);
        plaits::fm::RenderOperators<2, 0, false>(
            ops_, f, a, fb_state_, fb_amt, nullptr, &out, 1);
        return out;
    }

private:
    float amp_env_ = 1.0f;
    float mod_env_ = 1.0f;
    float freq_env_ = 1.0f;
    float amp_decay_const_ = 0.0f;
    float mod_decay_const_ = 0.0f;
    float freq_decay_const_ = 0.0f;

    plaits::fm::Operator ops_[2];
    float fb_state_[2] = {0.0f, 0.0f};
    float t_ = 0.0f;
};
