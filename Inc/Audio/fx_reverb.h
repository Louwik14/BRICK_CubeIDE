#pragma once

#include <stdint.h>

#ifdef __cplusplus

struct fx_reverb_t {
    uint8_t bypass;
    float wet;
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
    FX_REVERB_GLOBAL_TYPE_REVB = 0,
    FX_REVERB_GLOBAL_TYPE_MONO = FX_REVERB_GLOBAL_TYPE_REVB,
    FX_REVERB_GLOBAL_TYPE_STEREO = FX_REVERB_GLOBAL_TYPE_REVB,
} fx_reverb_global_type_t;

void fx_reverb_global_init(float sample_rate);
void fx_reverb_global_set_type(fx_reverb_global_type_t type);
void fx_reverb_global_set_wet(float wet);
void fx_reverb_global_set_size(float size);
void fx_reverb_global_set_decay(float decay);
void fx_reverb_global_set_predelay(float predelay_s);
void fx_reverb_global_set_surround(float surround_s);
void fx_reverb_global_set_lpf(float lpf);
uint8_t fx_reverb_global_is_active(void);
uint32_t fx_reverb_global_get_last_cycles(void);
uint32_t fx_reverb_global_get_max_cycles(void);
void fx_reverb_global_process_block(float *in_l,
                                    float *in_r,
                                    float *out_l,
                                    float *out_r,
                                    uint32_t frames);

#ifdef __cplusplus
}
#endif
