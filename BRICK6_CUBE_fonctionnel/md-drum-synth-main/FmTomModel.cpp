// FmTomModel.cpp
#include "FmTomModel.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include "CustomControls.h"
#endif
#if MD_DRUM_HAS_DESKTOP_UI
#include <imgui.h>
#endif

constexpr float PI = 3.14159265f;

void FmTomModel::Init() {
    core_.Init();
}

void FmTomModel::Trigger() {
    DrumFm2OpCore::TriggerConfig trig;
    trig.amp_decay_s = d_b;
    trig.mod_decay_s = d_m;
    trig.freq_decay_s = d_f;
    trig.set_initial_phase = true;
    trig.initial_phase_radians = start_phase;
    core_.Trigger(trig);
}

float FmTomModel::Process() {
    DrumFm2OpCore::FrameConfig cfg;
    cfg.carrier_freq_hz = f_b;
    cfg.mod_freq_hz = f_m;
    cfg.mod_index = I;
    cfg.feedback_amount = 1.0f;
    cfg.pitch_sweep_hz = A_f;
    return core_.ProcessSample(cfg);
}

void FmTomModel::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    CustomControls::ParameterSlider("f_b (Base Frequency)", &f_b, 80.0f, 400.0f);
    CustomControls::ParameterSlider("d_b (Amp Decay)", &d_b, 0.01f, 2.0f);
    CustomControls::ParameterSlider("f_m (Modulator Freq)", &f_m, 100.0f, 2000.0f);
    CustomControls::ParameterSlider("I (Mod Index)", &I, 0.0f, 50.0f);
    CustomControls::ParameterSlider("d_m (Mod Decay)", &d_m, 0.01f, 1.0f);
    CustomControls::ParameterSlider("A_f (Freq Sweep Amt)", &A_f, 0.0f, 100.0f);
    CustomControls::ParameterSlider("d_f (Freq Sweep Decay)", &d_f, 0.01f, 1.0f);
    CustomControls::ParameterSlider("Start Phase", &start_phase, 0.0f, PI);

#else
    /* Desktop UI disabled in embedded DSP builds. */
#endif
}
