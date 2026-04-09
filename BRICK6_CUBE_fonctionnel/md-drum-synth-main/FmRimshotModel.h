// FmRimshotModel.h
#pragma once
#include "DrumModel.h"

class FmRimshotModel : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;

    void saveParameters(std::ostream& os) const override {
        os << f_bB << ' ' << d_bB << ' ' << I_B << ' '
           << f_bA << ' ' << d_bA << ' ' << I_A << ' '
           << A_A << ' ' << d_m << ' ' << f_hp << '\n';
    }

    void loadParameters(std::istream& is) override {
        is >> f_bB >> d_bB >> I_B >> f_bA >> d_bA >> I_A >> A_A >> d_m >> f_hp;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: f_bB = value; return true;
            case 1U: d_bB = value; return true;
            case 2U: I_B = value; return true;
            case 3U: f_bA = value; return true;
            case 4U: d_bA = value; return true;
            case 5U: I_A = value; return true;
            case 6U: A_A = value; return true;
            case 7U: d_m = value; return true;
            case 8U: f_hp = value; return true;
            default: return false;
        }
    }

private:
    float f_bB = 600.0f, d_bB = 0.05f, I_B = 15.0f;
    float f_bA = 200.0f, d_bA = 0.25f, I_A = 10.0f;
    float A_A = 0.4f;
    float d_m = 0.05f;
    float f_hp = 400.0f;

    float mod_phase = 0.0f, carB_phase = 0.0f, carA_phase = 0.0f, prev_mod = 0.0f, t = 0.0f;
    float x_prev = 0.0f, y_prev = 0.0f;
};
