#pragma once

#include <stdint.h>

float wav_audio_codec_pcm16_to_float(const uint8_t *p);
float wav_audio_codec_pcm24_to_float(const uint8_t *p);
float wav_audio_codec_pcm32_to_float(const uint8_t *p);
void wav_audio_codec_decode_stereo_frame(const uint8_t *frame,
                                         uint16_t channels,
                                         uint16_t bits_per_sample,
                                         float *out_left,
                                         float *out_right);
void wav_audio_codec_resample_linear(float prev_left,
                                     float prev_right,
                                     float curr_left,
                                     float curr_right,
                                     float frac,
                                     float *out_left,
                                     float *out_right);
