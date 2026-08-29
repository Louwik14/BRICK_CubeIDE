#include "Audio/audio_diag_capture.h"
#if defined(BRICK6_AUDIO_DIAG_CAPTURE)
#include "Platform/memory_layout.h"

volatile audio_diag_control_t g_audio_diag_control = {0U, 0U, 0U, 0U};
volatile audio_diag_status_t g_audio_diag_status = {0U, 0U, 0U};
AUDIO_LUT_D2 volatile float g_audio_diag_reference[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile float g_audio_diag_engine[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile float g_audio_diag_post_track_l[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile float g_audio_diag_post_track_r[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile float g_audio_diag_pre_pcm_l[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile float g_audio_diag_pre_pcm_r[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile int32_t g_audio_diag_tx_pcm_l[AUDIO_DIAG_CAPTURE_SAMPLES];
AUDIO_LUT_D2 volatile int32_t g_audio_diag_tx_pcm_r[AUDIO_DIAG_CAPTURE_SAMPLES];

static uint32_t s_block_base, s_block_frames, s_engine_written, s_sine_phase;
static const float s_sine_1khz[48] = {
 0.000000000f, 0.016315245f, 0.032352381f, 0.047835430f, 0.062500000f, 0.076095217f,
 0.088388348f, 0.099183058f, 0.108253175f, 0.115484942f, 0.120740727f, 0.123930484f,
 0.125000000f, 0.123930484f, 0.120740727f, 0.115484942f, 0.108253175f, 0.099183058f,
 0.088388348f, 0.076095217f, 0.062500000f, 0.047835430f, 0.032352381f, 0.016315245f,
 0.000000000f,-0.016315245f,-0.032352381f,-0.047835430f,-0.062500000f,-0.076095217f,
-0.088388348f,-0.099183058f,-0.108253175f,-0.115484942f,-0.120740727f,-0.123930484f,
-0.125000000f,-0.123930484f,-0.120740727f,-0.115484942f,-0.108253175f,-0.099183058f,
-0.088388348f,-0.076095217f,-0.062500000f,-0.047835430f,-0.032352381f,-0.016315245f };

static uint32_t bounded(uint32_t frames) {
    const uint32_t left = AUDIO_DIAG_CAPTURE_SAMPLES - s_block_base;
    return frames < left ? frames : left;
}
void audio_diag_capture_begin_block(uint32_t frames) {
    if (g_audio_diag_control.command && g_audio_diag_status.state != 1U) {
        g_audio_diag_control.command = 0U; g_audio_diag_status.samples = 0U;
        g_audio_diag_status.engine_tap_seen = 0U; g_audio_diag_status.state = 1U; s_sine_phase = 0U;
    }
    s_block_base = g_audio_diag_status.samples; s_block_frames = bounded(frames); s_engine_written = 0U;
}
void audio_diag_capture_engine_mono(uint32_t track, uint32_t voice, float *samples, uint32_t frames) {
    if (g_audio_diag_status.state != 1U || !samples || track != g_audio_diag_control.track
            || voice != g_audio_diag_control.voice || s_engine_written) return;
    const uint32_t count = frames < s_block_frames ? frames : s_block_frames;
    for (uint32_t i = 0; i < count; ++i) {
        float ref = 0.0f;
        if (g_audio_diag_control.source == AUDIO_DIAG_SOURCE_SINE_1KHZ) {
            ref = s_sine_1khz[(s_sine_phase + i) % 48U]; samples[i] = ref;
        }
        g_audio_diag_reference[s_block_base+i] = ref; g_audio_diag_engine[s_block_base+i] = samples[i];
    }
    s_sine_phase = (s_sine_phase + count) % 48U; s_engine_written = 1U; g_audio_diag_status.engine_tap_seen = 1U;
}
static void copy_stereo(volatile float *dl, volatile float *dr, const float *sl, const float *sr, uint32_t frames) {
    if (g_audio_diag_status.state != 1U || !sl || !sr) return;
    const uint32_t count = frames < s_block_frames ? frames : s_block_frames;
    for (uint32_t i=0; i<count; ++i) { dl[s_block_base+i]=sl[i]; dr[s_block_base+i]=sr[i]; }
}
void audio_diag_capture_post_track(const float *l,const float *r,uint32_t n){copy_stereo(g_audio_diag_post_track_l,g_audio_diag_post_track_r,l,r,n);}
void audio_diag_capture_pre_pcm(const float *l,const float *r,uint32_t n){copy_stereo(g_audio_diag_pre_pcm_l,g_audio_diag_pre_pcm_r,l,r,n);}
void audio_diag_capture_tx_pcm(const int32_t *tx,uint32_t frames,uint32_t stride) {
    if (g_audio_diag_status.state != 1U || !tx || stride < 2U) return;
    const uint32_t count=frames<s_block_frames?frames:s_block_frames;
    for(uint32_t i=0;i<count;++i){g_audio_diag_tx_pcm_l[s_block_base+i]=tx[i*stride];g_audio_diag_tx_pcm_r[s_block_base+i]=tx[i*stride+1U];}
}
void audio_diag_capture_end_block(uint32_t frames) {
    if (g_audio_diag_status.state != 1U) return;
    const uint32_t count = frames < s_block_frames ? frames : s_block_frames;
    g_audio_diag_status.samples = s_block_base + count;
    if (g_audio_diag_status.samples >= AUDIO_DIAG_CAPTURE_SAMPLES)
        g_audio_diag_status.state = 2U;
}
#endif
