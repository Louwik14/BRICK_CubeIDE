#ifndef FX_AUDIO_VIBE_H
#define FX_AUDIO_VIBE_H
#include <stdint.h>
#define FX_AUDIO_VIBE_RING_SIZE 1024U
typedef struct { float l, r; } fx_audio_vibe_frame_t;
typedef struct { fx_audio_vibe_frame_t ring[FX_AUDIO_VIBE_RING_SIZE]; } fx_audio_vibe_history_t;
typedef struct { uint32_t write, phase, phase_increment; int32_t depth, offset; } fx_audio_vibe_state_t;
_Static_assert(sizeof(fx_audio_vibe_history_t)==8192U,"VIBE history size changed");
_Static_assert(sizeof(fx_audio_vibe_state_t)==20U,"VIBE state size changed");
void fx_audio_vibe_reset(fx_audio_vibe_state_t*,fx_audio_vibe_history_t*);
void fx_audio_vibe_prepare(fx_audio_vibe_state_t*,float rate_hz,float depth,float delay);
float fx_audio_vibe_process_wet_mono_sample(fx_audio_vibe_state_t*,fx_audio_vibe_history_t*,float);
void fx_audio_vibe_process_wet_stereo_sample(fx_audio_vibe_state_t*,fx_audio_vibe_history_t*,float*,float*);
void fx_audio_vibe_process_wet_stereo(fx_audio_vibe_state_t*,fx_audio_vibe_history_t*,float*,float*,uint32_t);
#endif
