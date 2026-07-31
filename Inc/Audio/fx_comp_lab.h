#pragma once

#include <stdint.h>

#ifdef __cplusplus
struct fx_comp_lab_t {
    float sample_rate;
    float attack_s;
    float release_s;
    float manual_makeup_db;
    float threshold_db;
    float ratio;
    float knee_db;
    float hpf_l;
    float hpf_r;
    float hpf_x_l;
    float hpf_x_r;
    float feedback_l[64];
    float feedback_r[64];
    float deluge_mean;
    float deluge_rms_log;
    float deluge_env;
    float deluge_gain;
    float brick_env;
    float brick_gain;
    float transition;
    float transition_old_gain;
    uint8_t model;
    uint8_t sidechain;
    uint8_t amount;
    uint8_t detector_rms;
    uint8_t deluge_saturation;
};

extern "C" {
#else
typedef struct fx_comp_lab_t fx_comp_lab_t;
#endif

fx_comp_lab_t *fx_comp_lab_get_instance(void);
void fx_comp_lab_init(fx_comp_lab_t *comp, float sample_rate);
void fx_comp_lab_set_model(fx_comp_lab_t *comp, uint8_t model);
void fx_comp_lab_set_sidechain(fx_comp_lab_t *comp, uint8_t sidechain);
uint8_t fx_comp_lab_get_sidechain(const fx_comp_lab_t *comp);
void fx_comp_lab_set_amount(fx_comp_lab_t *comp, uint8_t amount);
void fx_comp_lab_set_detector_rms(fx_comp_lab_t *comp, uint8_t rms);
void fx_comp_lab_set_deluge_saturation(fx_comp_lab_t *comp, uint8_t enabled);

void fx_comp_lab_process_block(fx_comp_lab_t *comp,
                                 float *left,
                                 float *right,
                                 const float *key_left,
                                 const float *key_right,
                                 uint32_t frames);

#ifdef __cplusplus
}
#endif
