#ifndef FX_AUDIO_DRIVE_H
#define FX_AUDIO_DRIVE_H
#include <stdint.h>
typedef struct { float pre_gain, post_gain, input_gain, level_gain; } fx_audio_drive_state_t;
_Static_assert(sizeof(fx_audio_drive_state_t)==16U,"Drive Daisy state size changed");
void fx_audio_drive_reset(fx_audio_drive_state_t *state);
void fx_audio_drive_prepare(fx_audio_drive_state_t *state, float drive, float input, float level);
float fx_audio_drive_process_sample(const fx_audio_drive_state_t *state, float sample);
void fx_audio_drive_process_stereo(fx_audio_drive_state_t *state, float *left, float *right, uint32_t frames);
#endif
