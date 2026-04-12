#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include "fx_Daisy_comp_core.h"

struct fx_daisy_comp_t {
    daisysp::Compressor core;
    float attack_s;
    float release_s;
    uint8_t auto_makeup;
    float manual_makeup_db;
    float mix; // NEW
};

extern "C" {
#else
typedef struct fx_daisy_comp_t fx_daisy_comp_t;
#endif

fx_daisy_comp_t *fx_daisy_comp_get_instance(void);
void fx_daisy_comp_init(fx_daisy_comp_t *comp, float sample_rate);
void fx_daisy_comp_set_threshold_db(fx_daisy_comp_t *comp, float threshold_db);
void fx_daisy_comp_set_ratio(fx_daisy_comp_t *comp, float ratio);
void fx_daisy_comp_set_attack_s(fx_daisy_comp_t *comp, float attack_s);
void fx_daisy_comp_set_release_s(fx_daisy_comp_t *comp, float release_s);
void fx_daisy_comp_set_makeup_db(fx_daisy_comp_t *comp, float makeup_db);
void fx_daisy_comp_set_auto_makeup(fx_daisy_comp_t *comp, uint8_t enabled);
void fx_daisy_comp_set_mix(fx_daisy_comp_t *comp, float mix); // NEW

void fx_daisy_comp_process_block(fx_daisy_comp_t *comp,
                                 float *left,
                                 float *right,
                                 uint32_t frames);

#ifdef __cplusplus
}
#endif
