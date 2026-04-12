// FmTomModel.h
#pragma once
#include "DrumModel.h"

class FmTomModel : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;
    void saveParameters(std::ostream& os) const override {
        os << f_b << ' ' << d_b << ' ' << f_m << ' ' << I << ' ' << d_m << ' ' << A_f << ' ' << d_f << ' ' << start_phase << '\n';
    }
    void loadParameters(std::istream& is) override {
        is >> f_b >> d_b >> f_m >> I >> d_m >> A_f >> d_f >> start_phase;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: f_b = value; return true;
            case 1U: d_b = value; return true;
            case 2U: f_m = value; return true;
            case 3U: I = value; return true;
            case 4U: d_m = value; return true;
            case 5U: A_f = value; return true;
            case 6U: d_f = value; return true;
            case 7U: start_phase = value; return true;
            default: return false;
        }
    }

private:
    float f_b = 150.0f, d_b = 0.7f, f_m = 300.0f, I = 15.0f, d_m = 0.2f;
    float A_f = 30.0f, d_f = 0.1f, start_phase = 3.14159f / 2.0f;
    float mod_phase = 0.0f, car_phase = 0.0f, prev_mod = 0.0f, t = 0.0f;
};
