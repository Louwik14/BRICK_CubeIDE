// FmSnareModel.h
#pragma once
#include "DrumModel.h"
#include "core/DrumFm2OpCore.h"

class FmSnareModel : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;
    void saveParameters(std::ostream& os) const override {
        os << f_b << ' ' << d_b << ' ' << f_m << ' ' << I << ' ' << d_m << ' ' << Abrus << ' ' << dbrus << ' ' << fhp << '\n';
    }
    void loadParameters(std::istream& is) override {
        is >> f_b >> d_b >> f_m >> I >> d_m >> Abrus >> dbrus >> fhp;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: f_b = value; return true;
            case 1U: d_b = value; return true;
            case 2U: f_m = value; return true;
            case 3U: I = value; return true;
            case 4U: d_m = value; return true;
            case 5U: Abrus = value; return true;
            case 6U: dbrus = value; return true;
            case 7U: fhp = value; return true;
            default: return false;
        }
    }

private:
    // FM parameters
    float f_b = 200.0f;     // Carrier frequency
    float d_b = 0.4f;       // Amplitude envelope decay
    float f_m = 1500.0f;    // Modulator frequency
    float I = 15.0f;        // Modulation index
    float d_m = 0.1f;       // Modulator envelope decay

    // Noise and filter
    float Abrus = 0.5f;     // Noise level
    float dbrus = 0.3f;     // Noise envelope decay
    float fhp = 400.0f;     // High-pass filter cutoff (Hz)

    DrumFm2OpCore core_;
};
