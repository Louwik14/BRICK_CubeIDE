#include "fx_reverb.h"
#include "audio_float.h"
#include "fx_reverb_revb.h"
#include "Storage/memory_layout.h"

static inline float clamp01(float v)
{
    return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

typedef struct
{
    volatile uint8_t backend_valid;
    float sample_rate;
    float wet;
    float room_size;
    float damping;
    float width;
    float hpf;
    float lpf;
} fx_reverb_global_state_t;

static fx_reverb_global_state_t g_reverb_global = {
    .backend_valid = 0U,
    .sample_rate = 48000.0f,
    .wet = 0.0f,
    .room_size = 0.6f,
    .damping = 0.72f,
    .width = 1.0f,
    .hpf = 0.0f,
    .lpf = 1.0f,
};

AUDIO_HOT ALIGN32 static float g_reverb_global_mono[AUDIO_BLOCK_SIZE];

static void apply_params(void)
{
    if(g_reverb_global.backend_valid == 0U)
        return;
    fx_reverb_revb_global_set_wet(g_reverb_global.wet);
    fx_reverb_revb_global_set_room_size(g_reverb_global.room_size);
    fx_reverb_revb_global_set_damping(g_reverb_global.damping);
    fx_reverb_revb_global_set_width(g_reverb_global.width);
    fx_reverb_revb_global_set_hpf(g_reverb_global.hpf);
    fx_reverb_revb_global_set_lpf(g_reverb_global.lpf);
}

void fx_reverb_global_init(float sample_rate)
{
    g_reverb_global.sample_rate = (sample_rate > 0.0f) ? sample_rate : 48000.0f;
    fx_reverb_revb_global_init(g_reverb_global.sample_rate);
    g_reverb_global.backend_valid = 1U;
    apply_params();
}

void fx_reverb_global_set_wet(float wet)
{
    g_reverb_global.wet = clamp01(wet);
    fx_reverb_revb_global_set_wet(g_reverb_global.wet);
}

void fx_reverb_global_set_room_size(float value) { g_reverb_global.room_size = clamp01(value); apply_params(); }
void fx_reverb_global_set_damping(float value) { g_reverb_global.damping = clamp01(value); apply_params(); }
void fx_reverb_global_set_width(float value) { g_reverb_global.width = clamp01(value); apply_params(); }
void fx_reverb_global_set_hpf(float value) { g_reverb_global.hpf = clamp01(value); apply_params(); }
void fx_reverb_global_set_lpf(float value) { g_reverb_global.lpf = clamp01(value); apply_params(); }
void fx_reverb_global_set_delay_mode(uint8_t tbd) { fx_reverb_revb_global_set_delay_mode(tbd); }

uint8_t fx_reverb_global_is_active(void)
{
    return (g_reverb_global.wet > 0.0f) ? 1U : 0U;
}

void fx_reverb_global_process_block(float *in_l, float *in_r, float *out_l, float *out_r, uint32_t frames)
{
    if((in_l == 0) || (in_r == 0) || (out_l == 0) || (out_r == 0))
        return;
    if(g_reverb_global.backend_valid == 0U)
    {
        volatile float *zero_l = out_l;
        volatile float *zero_r = out_r;
        for(uint32_t i = 0U; i < frames; ++i)
        {
            zero_l[i] = 0.0f;
            zero_r[i] = 0.0f;
        }
        return;
    }
    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;
    for(uint32_t i = 0U; i < frames; ++i)
        g_reverb_global_mono[i] = 0.5f * (in_l[i] + in_r[i]);
    fx_reverb_revb_global_process_send_mono_to_stereo_wet(g_reverb_global_mono, out_l, out_r, frames);
}
