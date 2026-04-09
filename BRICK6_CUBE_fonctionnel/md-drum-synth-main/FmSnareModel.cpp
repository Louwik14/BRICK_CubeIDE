// FmSnareModel.cpp
#include "FmSnareModel.h"
#include "DrumUiAbstraction.h"
#if MD_DRUM_HAS_DESKTOP_UI
#include "CustomControls.h"
#endif
#if MD_DRUM_HAS_DESKTOP_UI
#include <imgui.h>
#endif

void FmSnareModel::Init() {
    core_.Init();
}

void FmSnareModel::Trigger() {
    DrumFm2OpCore::TriggerConfig trig;
    trig.amp_decay_s = d_b;
    trig.mod_decay_s = d_m;
    trig.freq_decay_s = 1.0f;
    trig.use_noise_env = true;
    trig.noise_decay_s = dbrus;
    core_.Trigger(trig);
}

float FmSnareModel::Process() {
    DrumFm2OpCore::FrameConfig cfg;
    cfg.carrier_freq_hz = f_b;
    cfg.mod_freq_hz = f_m;
    cfg.mod_index = I;
    cfg.feedback_amount = 0.0f;
    cfg.pitch_sweep_hz = 0.0f;
    cfg.use_noise_hp = true;
    cfg.noise_level = Abrus;
    cfg.hp_cutoff_hz = fhp;
    cfg.post_amp_env_gain = true;
    return core_.ProcessSample(cfg);
}

void FmSnareModel::RenderControls() {
#if MD_DRUM_HAS_DESKTOP_UI

    CustomControls::ParameterSlider("f_b (Tone Freq)", &f_b, 100.0f, 400.0f);
    CustomControls::ParameterSlider("d_b (Tone Decay)", &d_b, 0.01f, 1.0f);
    CustomControls::ParameterSlider("f_m (Mod Freq)", &f_m, 500.0f, 3000.0f);
    CustomControls::ParameterSlider("I (Mod Index)", &I, 0.0f, 50.0f);
    CustomControls::ParameterSlider("d_m (Mod Decay)", &d_m, 0.01f, 1.0f);
    CustomControls::ParameterSlider("Abrus (Noise Level)", &Abrus, 0.0f, 1.0f);
    CustomControls::ParameterSlider("dbrus (Noise Decay)", &dbrus, 0.01f, 1.0f);
    CustomControls::ParameterSlider("fhp (HPF Cutoff)", &fhp, 20.0f, 2000.0f);

#else
    /* Desktop UI disabled in embedded DSP builds. */
#endif
}
