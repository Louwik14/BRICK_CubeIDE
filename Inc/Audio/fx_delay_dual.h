#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FX_DELAY_DUAL_MODE_NORMAL = 0,
    FX_DELAY_DUAL_MODE_PINGPONG,
    FX_DELAY_DUAL_MODE_TAP,
    FX_DELAY_DUAL_MODE_CLASSIC_PINGPONG,
    FX_DELAY_DUAL_MODE_COUNT
} fx_delay_dual_mode_t;

void fx_delay_dual_global_init(float sample_rate);
void fx_delay_dual_global_clear(void);
void fx_delay_dual_global_set_mode(uint8_t mode);
void fx_delay_dual_global_set_time_l(float time_s);
void fx_delay_dual_global_set_time_r(float time_s);
void fx_delay_dual_global_set_feedback(float feedback);
void fx_delay_dual_global_set_filter_hz(float low_cut_hz, float high_cut_hz);
void fx_delay_dual_global_set_width(float width);
void fx_delay_dual_global_set_feedback_width(float width);
void fx_delay_dual_global_set_mod_depth(float depth);
void fx_delay_dual_global_set_mod_rate(float rate_hz);
void fx_delay_dual_global_set_reverb_send(float reverb_send);
void fx_delay_dual_global_set_volume(float volume);
uint8_t fx_delay_dual_global_is_active(void);
void fx_delay_dual_global_process_block(const float *in_l,
                                        const float *in_r,
                                        float *out_l,
                                        float *out_r,
                                        float *rev_l,
                                        float *rev_r,
                                        uint32_t frames);

#ifdef __cplusplus
}
#endif
