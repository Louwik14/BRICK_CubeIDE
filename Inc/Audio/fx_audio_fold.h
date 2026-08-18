#ifndef FX_AUDIO_FOLD_H
#define FX_AUDIO_FOLD_H
#include <stdint.h>
typedef struct { float scale, bias, xmod; } fx_audio_fold_state_t;
_Static_assert(sizeof(fx_audio_fold_state_t)==12U,"Fold float state size changed");
void fx_audio_fold_reset(fx_audio_fold_state_t *state);
void fx_audio_fold_prepare(fx_audio_fold_state_t *state,float fold,float bias,float xmod);
float fx_audio_fold_process_mono_sample(const fx_audio_fold_state_t *state,float sample);
void fx_audio_fold_process_stereo(const fx_audio_fold_state_t *state,float *left,float *right,uint32_t frames);
#endif
