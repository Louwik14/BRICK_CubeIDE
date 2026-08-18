#ifndef FX_AUDIO_POINT_H
#define FX_AUDIO_POINT_H

#include <stdint.h>

typedef struct
{
    float nib_a;
    float nob_a;
    float nib_b;
    float nob_b;
} fx_audio_point_channel_t;

typedef struct
{
    fx_audio_point_channel_t left;
    fx_audio_point_channel_t right;
    float gain_trim;
    float nib_attack;
    float nob_attack;
    float nib_decay;
    float nob_decay;
    uint8_t flip;
    uint8_t needs_warm_start;
} fx_audio_point_state_t;

_Static_assert(sizeof(fx_audio_point_channel_t) == 16U,
               "Point channel state size changed");
_Static_assert(sizeof(fx_audio_point_state_t) == 56U,
               "Point state size changed");

void fx_audio_point_reset(fx_audio_point_state_t *state);
void fx_audio_point_prepare(fx_audio_point_state_t *state,
                            float amount,
                            float point,
                            float speed,
                            float sample_rate);
float fx_audio_point_process_mono_sample(fx_audio_point_state_t *state,
                                         float sample);
void fx_audio_point_process_stereo_sample(fx_audio_point_state_t *state,
                                          float *left,
                                          float *right);
void fx_audio_point_process_mono(fx_audio_point_state_t *state,
                                 float *buffer,
                                 uint32_t frames);
void fx_audio_point_process_stereo(fx_audio_point_state_t *state,
                                   float *left,
                                   float *right,
                                   uint32_t frames);

#endif /* FX_AUDIO_POINT_H */
