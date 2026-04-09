// FmClapModel.h
#pragma once
#include "DrumModel.h"

class FmClapModel : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;

    void saveParameters(std::ostream& os) const override {
        os << f_b << ' ' << f_m << ' ' << I << ' ' << d_m << ' '
           << d1 << ' ' << d2 << ' ' << clap_count << ' '
           << clap_interval << ' ' << fhp << ' ' << bm << '\n';
    }

    void loadParameters(std::istream& is) override {
        is >> f_b >> f_m >> I >> d_m >> d1 >> d2 >> clap_count >> clap_interval >> fhp >> bm;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: f_b = value; return true;
            case 1U: f_m = value; return true;
            case 2U: I = value; return true;
            case 3U: d_m = value; return true;
            case 4U: d1 = value; return true;
            case 5U: d2 = value; return true;
            case 6U: clap_count = static_cast<int>(value); return true;
            case 7U: clap_interval = value; return true;
            case 8U: fhp = value; return true;
            case 9U: bm = value; return true;
            default: return false;
        }
    }

private:
    float f_b = 800.0f, f_m = 800.0f, I = 40.0f, d_m = 0.05f;
    float d1 = 0.02f, d2 = 0.3f;
    int clap_count = 3, clap_stage = 0;
    float clap_interval = 0.012f; // seconds between claps
    float clap_timer = 0.0f;
    float fhp = 400.0f;
    float bm = 0.9f; // now user-controllable mod feedback

    float mod_phase = 0.0f, car_phase = 0.0f, prev_mod = 0.0f, t = 0.0f;
    float y_prev = 0.0f, x_prev = 0.0f;
    bool active = false;
};
