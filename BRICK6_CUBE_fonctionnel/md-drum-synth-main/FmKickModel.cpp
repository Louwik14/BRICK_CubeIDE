#include "FmKickModel.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include <imgui.h>
#endif
#if MD_DRUM_HAS_DESKTOP_UI
#include "CustomControls.h"
#endif

const float FmKickModel::ratios[FmKickModel::num_ratios][2] = {
    // Integer multiples 2:1 to 40:1
    {2.0f, 1.0f}, {3.0f, 1.0f}, {4.0f, 1.0f}, {5.0f, 1.0f}, {6.0f, 1.0f}, {7.0f, 1.0f}, {8.0f, 1.0f}, {9.0f, 1.0f},
    {10.0f, 1.0f}, {11.0f, 1.0f}, {12.0f, 1.0f}, {13.0f, 1.0f}, {14.0f, 1.0f}, {15.0f, 1.0f}, {16.0f, 1.0f}, {17.0f, 1.0f},
    {18.0f, 1.0f}, {19.0f, 1.0f}, {20.0f, 1.0f}, {21.0f, 1.0f}, {22.0f, 1.0f}, {23.0f, 1.0f}, {24.0f, 1.0f}, {25.0f, 1.0f},
    {26.0f, 1.0f}, {27.0f, 1.0f}, {28.0f, 1.0f}, {29.0f, 1.0f}, {30.0f, 1.0f}, {31.0f, 1.0f}, {32.0f, 1.0f}, {33.0f, 1.0f},
    {34.0f, 1.0f}, {35.0f, 1.0f}, {36.0f, 1.0f}, {37.0f, 1.0f}, {38.0f, 1.0f}, {39.0f, 1.0f}, {40.0f, 1.0f},
    // Odd/weird ratios >1 and <=40 (25 more)
    {8.0f, 5.0f}, {11.0f, 3.0f}, {13.0f, 7.0f}, {17.0f, 5.0f}, {19.0f, 4.0f}, {23.0f, 7.0f}, {31.0f, 9.0f}, {37.0f, 8.0f},
    {15.0f, 4.0f}, {21.0f, 5.0f}, {25.0f, 6.0f}, {27.0f, 8.0f}, {35.0f, 9.0f}, {39.0f, 10.0f}, {29.0f, 7.0f}, {33.0f, 8.0f},
    {22.0f, 3.0f}, {34.0f, 5.0f}, {38.0f, 7.0f}, {5.0f, 2.0f}, {7.0f, 3.0f}, {9.0f, 2.0f}, {12.0f, 5.0f}, {14.0f, 3.0f},
    {16.0f, 5.0f}
};

void FmKickModel::Init() {
    core_.Init();
}

void FmKickModel::Trigger() {
    core_.Trigger(d_b, d_m, d_f);
}

float FmKickModel::Process() {
    float mod_freq = f_m;
    if (use_ratio_mode) {
        mod_freq = f_b * (ratios[ratio_index][0] / ratios[ratio_index][1]);
    }

    DrumFm2OpCore::FrameConfig cfg;
    cfg.carrier_freq_hz = f_b;
    cfg.mod_freq_hz = mod_freq;
    cfg.mod_index = I;
    cfg.feedback_amount = b_m;
    cfg.pitch_sweep_hz = A_f;
    cfg.mod_freq_tracks_pitch_sweep = mod_env_sync;

    return core_.ProcessSample(cfg);
}

void FmKickModel::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    // Info window
    if (ImGui::CollapsingHeader("FM Kick Model Info", ImGuiTreeNodeFlags_None)) {
        ImGui::TextWrapped(
            "The FM Kick Model synthesizes bass drum sounds using two-operator frequency modulation (FM). "
            "You can set the carrier (base) frequency and modulator frequency, or lock the modulator to common musical ratios for classic and metallic drum timbres. "
            "Envelope controls shape the amplitude, modulation index, and frequency sweep for punchy or soft attacks. "
            "Feedback and modulation index add grit and complexity. "
            "Enable 'Sync Modulator Freq Envelope to Carrier' to keep the modulator's pitch sweep in sync with the carrier for more cohesive FM drum sounds."
        );
    }

    // Carrier frequency (pitch of the drum)
    CustomControls::ParameterSlider("f_b (Base Frequency)", &f_b, 20.0f, 100.0f);

    // UI: Ratio mode toggle
    ImGui::Checkbox("Lock Modulator to Ratio", &use_ratio_mode);
    if (use_ratio_mode) {
        ImGui::SliderInt("Modulator Ratio Index", &ratio_index, 0, num_ratios - 1);
        if (ImGui::IsItemHovered()) {
            float num = ratios[ratio_index][0];
            float den = ratios[ratio_index][1];
            char buf[32];
            snprintf(buf, sizeof(buf), "Current Ratio: %.0f:%.0f (%.3fx)", num, den, num/den);
            ImGui::SetTooltip("%s", buf);
        }
    } else {
        // Modulator frequency (determines harmonic complexity)
        CustomControls::ParameterSlider("f_m (Modulator Freq)", &f_m, 50.0f, 2000.0f);
    }
    // New: Sync modulator freq envelope to carrier
    ImGui::Checkbox("Sync Modulator Freq Envelope to Carrier", &mod_env_sync);

    // Volume envelope decay (controls how long the drum rings out)
    CustomControls::ParameterSlider("d_b (Amp Decay)", &d_b, 0.01f, 2.0f);

    // Modulation index (depth of FM, sharpness of attack)
    CustomControls::ParameterSlider("I (Mod Index)", &I, 0.0f, 10.0f, 0.001f, 0.01f);

    // Modulator envelope decay (shorter = clickier attack)
    CustomControls::ParameterSlider("d_m (Mod Decay)", &d_m, 0.001f, 2.0f, 0.001f, 0.01f);

    // Feedback on the modulator (adds noise/grit to tone)
    CustomControls::ParameterSlider("b_m (Mod Feedback)", &b_m, .0f, 16.0f, 1, 2);

    // Frequency sweep amount (in Hz)
    CustomControls::ParameterSlider("A_f (Freq Sweep Amt)", &A_f, 0.0f, 1000.0f);

    // Frequency envelope decay (how fast pitch sweep drops)
    CustomControls::ParameterSlider("d_f (Freq Sweep Decay)", &d_f, 0.001f, 2.0f, 0.001f, 0.01f  );

#else
    /* Desktop UI disabled in embedded DSP builds. */
#endif
}
