#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include "fx_reverb_drumboy.h"

struct fx_reverb_t {
    fx_reverb_drumboy_t model;
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
void fx_reverb_set_width(fx_reverb_t *rev, float width);
void fx_reverb_set_bypass(fx_reverb_t *rev, uint8_t bypass);

typedef enum
{
    FX_REVERB_GLOBAL_TYPE_MONO = 0,   /* Drumboy */
    FX_REVERB_GLOBAL_TYPE_STEREO = 1, /* legacy compat tombstone */
} fx_reverb_global_type_t;

void fx_reverb_global_init(float sample_rate);
void fx_reverb_global_set_type(fx_reverb_global_type_t type);
void fx_reverb_global_set_wet(float wet);
void fx_reverb_global_set_size(float size);
void fx_reverb_global_set_decay(float decay);
void fx_reverb_global_set_predelay(float predelay_s);
void fx_reverb_global_set_surround(float surround_s);
uint8_t fx_reverb_global_is_active(void);
void fx_reverb_global_process_block(float *in_l,
                                    float *in_r,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames);

#ifdef __cplusplus
}
#endif
