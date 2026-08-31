#pragma once

#include <stdint.h>

void sd_preview_audio_init(void);
uint8_t sd_preview_render_main(float *out_main_l, float *out_main_r,
                               uint32_t frames);
uint8_t sd_preview_audio_apply_active(uint8_t active);
uint8_t sd_preview_audio_apply_gain(uint32_t gain_bits);
