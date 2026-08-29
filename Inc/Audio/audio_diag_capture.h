#pragma once
#include <stdint.h>

#define AUDIO_DIAG_CAPTURE_SAMPLES 512U

typedef enum { AUDIO_DIAG_SOURCE_PRISM = 0U, AUDIO_DIAG_SOURCE_SINE_1KHZ = 1U } audio_diag_source_t;
typedef struct {
    volatile uint32_t command; /* Write 1 from GDB to arm. */
    volatile uint32_t source;
    volatile uint32_t track;
    volatile uint32_t voice;
} audio_diag_control_t;
typedef struct {
    volatile uint32_t state; /* 0 idle, 1 capturing, 2 frozen. */
    volatile uint32_t samples;
    volatile uint32_t engine_tap_seen;
} audio_diag_status_t;

#if defined(BRICK6_AUDIO_DIAG_CAPTURE)
extern volatile audio_diag_control_t g_audio_diag_control;
extern volatile audio_diag_status_t g_audio_diag_status;
extern volatile float g_audio_diag_reference[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile float g_audio_diag_engine[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile float g_audio_diag_post_track_l[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile float g_audio_diag_post_track_r[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile float g_audio_diag_pre_pcm_l[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile float g_audio_diag_pre_pcm_r[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile int32_t g_audio_diag_tx_pcm_l[AUDIO_DIAG_CAPTURE_SAMPLES];
extern volatile int32_t g_audio_diag_tx_pcm_r[AUDIO_DIAG_CAPTURE_SAMPLES];
void audio_diag_capture_begin_block(uint32_t frames);
void audio_diag_capture_engine_mono(uint32_t track, uint32_t voice, float *samples, uint32_t frames);
void audio_diag_capture_post_track(const float *left, const float *right, uint32_t frames);
void audio_diag_capture_pre_pcm(const float *left, const float *right, uint32_t frames);
void audio_diag_capture_tx_pcm(const int32_t *tx, uint32_t frames, uint32_t words_per_frame);
void audio_diag_capture_end_block(uint32_t frames);
#else
#define audio_diag_capture_begin_block(frames) ((void)(frames))
#define audio_diag_capture_engine_mono(track, voice, samples, frames) ((void)(track), (void)(voice), (void)(samples), (void)(frames))
#define audio_diag_capture_post_track(left, right, frames) ((void)(left), (void)(right), (void)(frames))
#define audio_diag_capture_pre_pcm(left, right, frames) ((void)(left), (void)(right), (void)(frames))
#define audio_diag_capture_tx_pcm(tx, frames, words) ((void)(tx), (void)(frames), (void)(words))
#define audio_diag_capture_end_block(frames) ((void)(frames))
#endif
