#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include "../freeverb-main/Components/revmodel.hpp"

struct fx_reverb_t {
    revmodel model;
    uint8_t bypass;
};

extern "C" {
#else
typedef struct fx_reverb_t fx_reverb_t;
#endif

void fx_reverb_init(fx_reverb_t *rev, float sample_rate);

void fx_reverb_process_block(fx_reverb_t *rev,
                             float *in_l,
                             float *in_r,
                             float *out_l,
                             float *out_r,
                             uint32_t frames);

void fx_reverb_set_wet(fx_reverb_t *rev, float wet);
void fx_reverb_set_room_size(fx_reverb_t *rev, float room);
void fx_reverb_set_damping(fx_reverb_t *rev, float damp);
void fx_reverb_set_bypass(fx_reverb_t *rev, uint8_t bypass);

fx_reverb_t *fx_reverb_get_instance(void);

#ifdef __cplusplus
}
#endif
