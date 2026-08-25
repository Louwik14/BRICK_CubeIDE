#ifndef FX_AUDIO_DRIFT_H
#define FX_AUDIO_DRIFT_H
#include <stdint.h>
#define FX_AUDIO_DRIFT_RING_SIZE 1024U
#define FX_AUDIO_DRIFT_FEEDBACK_MAX 0.95f
#define FX_AUDIO_DRIFT_DELAY_MIN_SAMPLES 4.8f
#define FX_AUDIO_DRIFT_DELAY_BASE_MAX_SAMPLES 384.0f
#define FX_AUDIO_DRIFT_DELAY_MOD_MAX_SAMPLES 741.12f
#define FX_AUDIO_DRIFT_DELAY_MOD_MAX_WHOLE_SAMPLES 741U
#define FX_AUDIO_DRIFT_DELAY_BASE_SPAN_SAMPLES \
    (FX_AUDIO_DRIFT_DELAY_BASE_MAX_SAMPLES-FX_AUDIO_DRIFT_DELAY_MIN_SAMPLES)
#define FX_AUDIO_DRIFT_DELAY_MOD_MAX_CONTROL \
    ((FX_AUDIO_DRIFT_DELAY_MOD_MAX_SAMPLES-FX_AUDIO_DRIFT_DELAY_MIN_SAMPLES) \
     / FX_AUDIO_DRIFT_DELAY_BASE_SPAN_SAMPLES)
typedef struct { float ring[FX_AUDIO_DRIFT_RING_SIZE]; } fx_audio_drift_history_t;
typedef struct { uint32_t write; float delay,delay_target,feedback; } fx_audio_drift_state_t;
_Static_assert(sizeof(fx_audio_drift_history_t)==4096U,"DRIFT history size changed");
_Static_assert(sizeof(fx_audio_drift_state_t)==16U,"DRIFT state size changed");
_Static_assert((FX_AUDIO_DRIFT_DELAY_MOD_MAX_WHOLE_SAMPLES + 1U) < FX_AUDIO_DRIFT_RING_SIZE,
               "DRIFT modulated delay and interpolation tap must fit the ring");
void fx_audio_drift_reset(fx_audio_drift_state_t*,fx_audio_drift_history_t*);
void fx_audio_drift_set_delay(fx_audio_drift_state_t*,float);
void fx_audio_drift_set_feedback(fx_audio_drift_state_t*,float);
float fx_audio_drift_process_mono_sample(fx_audio_drift_state_t*,fx_audio_drift_history_t*,float,float);
void fx_audio_drift_process_stereo(fx_audio_drift_state_t*,fx_audio_drift_history_t*,float*,float*,uint32_t);
void fx_audio_drift_process_dual_mono_stereo(fx_audio_drift_state_t*,fx_audio_drift_history_t*,fx_audio_drift_history_t*,float*,float*,uint32_t);
#endif
