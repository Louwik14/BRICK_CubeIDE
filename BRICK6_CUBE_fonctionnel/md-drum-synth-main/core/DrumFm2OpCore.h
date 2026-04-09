#pragma once

#include <cstdint>
#include <cstdlib>

#include "plaits/dsp/fm/operator.h"

class DrumFm2OpCore {
public:
    struct TriggerConfig {
        float amp_decay_s = 0.5f;
        float mod_decay_s = 0.15f;
        float freq_decay_s = 0.1f;
        bool set_initial_phase = false;
        float initial_phase_radians = 0.0f;
        bool use_noise_env = false;
        float noise_decay_s = 0.3f;
    };

    struct FrameConfig {
        float carrier_freq_hz = 50.0f;
        float mod_freq_hz = 180.0f;
        float mod_index = 20.0f;
        float feedback_amount = 0.0f;
        float pitch_sweep_hz = 0.0f;
        bool mod_freq_tracks_pitch_sweep = false;
        bool use_noise_hp = false;
        float noise_level = 0.0f;
        float hp_cutoff_hz = 400.0f;
        bool post_amp_env_gain = false;
    };

    static constexpr float kSampleRate = 48000.0f;
    static constexpr float kTwoPi = 6.28318530717958647692f;

    void Init() {
        t_ = 0.0f;
        amp_env_ = 1.0f;
        mod_env_ = 1.0f;
        freq_env_ = 1.0f;
        noise_env_ = 1.0f;
        amp_decay_const_ = 0.0f;
        mod_decay_const_ = 0.0f;
        freq_decay_const_ = 0.0f;
        noise_decay_const_ = 0.0f;
        ops_[0].Reset();
        ops_[1].Reset();
        fb_state_[0] = 0.0f;
        fb_state_[1] = 0.0f;
        hp_x_ = 0.0f;
        hp_y_ = 0.0f;
    }

    void Trigger(float amp_decay_s, float mod_decay_s, float freq_decay_s) {
        TriggerConfig cfg;
        cfg.amp_decay_s = amp_decay_s;
        cfg.mod_decay_s = mod_decay_s;
        cfg.freq_decay_s = freq_decay_s;
        Trigger(cfg);
    }

    void Trigger(const TriggerConfig& cfg) {
        Init();

        const float dt = 1.0f / kSampleRate;
        amp_decay_const_ = 1.0f - (dt / cfg.amp_decay_s);
        mod_decay_const_ = 1.0f - (dt / cfg.mod_decay_s);
        freq_decay_const_ = 1.0f - (dt / cfg.freq_decay_s);
        if (cfg.use_noise_env) {
            noise_decay_const_ = 1.0f - (dt / cfg.noise_decay_s);
        } else {
            noise_decay_const_ = 0.0f;
        }

        if (amp_decay_const_ < 0.0f) amp_decay_const_ = 0.0f;
        if (mod_decay_const_ < 0.0f) mod_decay_const_ = 0.0f;
        if (freq_decay_const_ < 0.0f) freq_decay_const_ = 0.0f;
        if (noise_decay_const_ < 0.0f) noise_decay_const_ = 0.0f;

        if (cfg.set_initial_phase) {
            const float normalized = cfg.initial_phase_radians / kTwoPi;
            const uint32_t phase = static_cast<uint32_t>(normalized * 4294967296.0f);
            ops_[0].phase = phase;
            ops_[1].phase = phase;
        }
    }

    float ProcessSample(const FrameConfig& cfg) {
        const float dt = 1.0f / kSampleRate;
        t_ += dt;

        amp_env_ *= amp_decay_const_;
        mod_env_ *= mod_decay_const_;
        freq_env_ *= freq_decay_const_;
        noise_env_ *= noise_decay_const_;

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

        if (cfg.use_noise_hp) {
            const float white = ((float(std::rand()) / RAND_MAX) * 2.0f - 1.0f) * cfg.noise_level * noise_env_;
            const float x = out + white;
            const float dt = 1.0f / kSampleRate;
            const float alpha = 1.0f / (1.0f + kTwoPi * cfg.hp_cutoff_hz * dt);
            const float y = alpha * (hp_y_ + x - hp_x_);
            hp_y_ = x;
            hp_x_ = y;
            out = y;
        }

        if (cfg.post_amp_env_gain) {
            out *= amp_env_;
        }

        return out;
    }

private:
    float amp_env_ = 1.0f;
    float mod_env_ = 1.0f;
    float freq_env_ = 1.0f;
    float amp_decay_const_ = 0.0f;
    float mod_decay_const_ = 0.0f;
    float freq_decay_const_ = 0.0f;
    float noise_env_ = 1.0f;
    float noise_decay_const_ = 0.0f;

    plaits::fm::Operator ops_[2];
    float fb_state_[2] = {0.0f, 0.0f};
    float t_ = 0.0f;
    float hp_x_ = 0.0f;
    float hp_y_ = 0.0f;
};
