#pragma once

#include <stdint.h>

#ifdef __cplusplus
struct fx_comp_lab_t {
    float sample_rate;
    float attack_s;
    float release_s;
    float manual_makeup_db;
    float mix;
    float threshold_db;
    float ratio;
    float knee_db;
    float sc_hpf_hz;
    float hpf_l;
    float hpf_r;
    float hpf_x_l;
    float hpf_x_r;
    float deluge_mean;
    float deluge_rms_log;
    float deluge_env;
    float deluge_gain;
    float brick_env;
    float brick_gain;
    float transition;
    float transition_old_gain;
    float cached_makeup;
    float cached_hpf_a;
    float cached_deluge_attack_coeff;
    float cached_deluge_release_coeff;
    float cached_brick_attack_coeff;
    float cached_brick_release_coeff;
    uint32_t cached_coeff_frames;
    uint8_t coeff_dirty;
    uint8_t model;
    uint8_t detector_rms;
    uint8_t deluge_saturation;
};

extern "C" {
#else
typedef struct fx_comp_lab_t fx_comp_lab_t;
#endif

fx_comp_lab_t *fx_comp_lab_get_instance(void);
void fx_comp_lab_init(fx_comp_lab_t *comp, float sample_rate);
void fx_comp_lab_set_threshold_db(fx_comp_lab_t *comp, float threshold_db);
void fx_comp_lab_set_ratio(fx_comp_lab_t *comp, float ratio);
void fx_comp_lab_set_attack_s(fx_comp_lab_t *comp, float attack_s);
void fx_comp_lab_set_release_s(fx_comp_lab_t *comp, float release_s);
void fx_comp_lab_set_makeup_db(fx_comp_lab_t *comp, float makeup_db);
void fx_comp_lab_set_mix(fx_comp_lab_t *comp, float mix);
void fx_comp_lab_set_model(fx_comp_lab_t *comp, uint8_t model);
void fx_comp_lab_set_sc_hpf_hz(fx_comp_lab_t *comp, float hz);
void fx_comp_lab_set_detector_rms(fx_comp_lab_t *comp, uint8_t rms);
void fx_comp_lab_set_knee_db(fx_comp_lab_t *comp, float db);
void fx_comp_lab_set_deluge_saturation(fx_comp_lab_t *comp, uint8_t enabled);

void fx_comp_lab_process_block(fx_comp_lab_t *comp,
                                 float *left,
                                 float *right,
                                 uint32_t frames);

#ifdef __cplusplus
}
#endif
