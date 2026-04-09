#pragma once
#include "DrumModel.h"
#include "plaits/dsp/fm/operator.h"

class FmKickModel : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;
    void saveParameters(std::ostream& os) const override {
        os << f_b << ' ' << d_b << ' ' << f_m << ' ' << I << ' ' << d_m << ' ' << b_m << ' ' << A_f << ' ' << d_f << ' ' << use_ratio_mode << ' ' << ratio_index << ' ' << mod_env_sync << '\n';
    }
    void loadParameters(std::istream& is) override {
        is >> f_b >> d_b >> f_m >> I >> d_m >> b_m >> A_f >> d_f >> use_ratio_mode >> ratio_index >> mod_env_sync;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: f_b = value; return true;
            case 1U: d_b = value; return true;
            case 2U: f_m = value; return true;
            case 3U: I = value; return true;
            case 4U: d_m = value; return true;
            case 5U: b_m = value; return true;
            case 6U: A_f = value; return true;
            case 7U: d_f = value; return true;
            case 8U: use_ratio_mode = (value != 0.0f); return true;
            case 9U: ratio_index = static_cast<int>(value); return true;
            case 10U: mod_env_sync = (value != 0.0f); return true;
            default: return false;
        }
    }

private:
    float f_b = 50.0f, d_b = 0.5f, f_m = 180.0f, I = 20.0f;
    float d_m = 0.15f, b_m = 0.5f, A_f = 60.0f, d_f = 0.1f;

    // Ratio mode for modulator frequency
    bool use_ratio_mode = false;
    int ratio_index = 0; // Index into ratio array
    static constexpr int num_ratios = 64;
    static const float ratios[num_ratios][2];

    // Iterative decay state
    float amp_env = 1.0f, mod_env = 1.0f, freq_env = 1.0f;
    float amp_decay_const = 0.0f, mod_decay_const = 0.0f, freq_decay_const = 0.0f;

    // Plaits FM operator state
    plaits::fm::Operator ops[2]; // [0]=modulator, [1]=carrier
    float fb_state[2] = {0.0f, 0.0f};
    float t = 0.0f;

    bool mod_env_sync = false; // New: sync modulator freq envelope to carrier
};
