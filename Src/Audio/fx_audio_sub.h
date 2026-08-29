#ifndef FX_AUDIO_SUB_H
#define FX_AUDIO_SUB_H
#include <stdint.h>
typedef struct { float a,b,c,d; uint8_t was_negative,sub_octave; } fx_audio_sub_channel_t;
typedef struct { fx_audio_sub_channel_t left,right; float fundamental,sub,eq,one_minus_eq,dcblock,basstrim,wet,dry; uint8_t wet_only; } fx_audio_sub_state_t;
_Static_assert(sizeof(fx_audio_sub_state_t)==76U,"SUB state size changed");
void fx_audio_sub_reset(fx_audio_sub_state_t*);void fx_audio_sub_prepare(fx_audio_sub_state_t*,float,float,float,float);
float fx_audio_sub_process_mono_sample(fx_audio_sub_state_t*,float);void fx_audio_sub_process_stereo_sample(fx_audio_sub_state_t*,float*,float*);
float fx_audio_sub_light_process_mono_sample(fx_audio_sub_state_t*,float);void fx_audio_sub_light_process_stereo_sample(fx_audio_sub_state_t*,float*,float*);
#endif
