#ifndef FX_AUDIO_RING_H
#define FX_AUDIO_RING_H
#include <stdint.h>
#include "Audio/md_dsp.h"
typedef struct { md_phase_t carrier; float gain; uint8_t wave,model; } fx_audio_ring_state_t;
_Static_assert(sizeof(fx_audio_ring_state_t)==16U,"RING state size changed");
void fx_audio_ring_reset(fx_audio_ring_state_t*);void fx_audio_ring_prepare(fx_audio_ring_state_t*,float,float,float,float);
float fx_audio_ring_process_mono_sample(fx_audio_ring_state_t*,float);void fx_audio_ring_process_stereo_sample(fx_audio_ring_state_t*,float*,float*);
#endif
