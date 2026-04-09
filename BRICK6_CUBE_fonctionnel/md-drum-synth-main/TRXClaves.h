#pragma once
#include "DrumModel.h"

class TRXClaves : public DrumModel {
public:
    void Init() override;
    void Trigger() override;
    float Process() override;
    void RenderControls() override;

    void saveParameters(std::ostream& os) const override {
        os << pitch << ' ' << interval << ' ' << decay
           << ' ' << balance << ' ' << clip << '\n';
    }

    void loadParameters(std::istream& is) override {
        is >> pitch >> interval >> decay >> balance >> clip;
    }

    bool SetParamByIndex(uint8_t index, float value) override {
        switch (index) {
            case 0U: pitch = value; return true;
            case 1U: interval = value; return true;
            case 2U: decay = value; return true;
            case 3U: balance = value; return true;
            case 4U: clip = value; return true;
            default: return false;
        }
    }

private:
    float pitch = 600.0f;     // Base pitch (Hz)
    float interval = 200.0f;  // Interval between the two oscillators
    float decay = 0.1f;       // Envelope decay
    float balance = 0.5f;     // Balance between osc1 and osc2
    float clip = 0.2f;        // Clipping/saturation

    float phase1 = 0.0f;
    float phase2 = 0.0f;
    float env = 0.0f;
    float t = 0.0f;

    float sine(float x);
};
