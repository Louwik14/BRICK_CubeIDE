#pragma once

#include <stdint.h>
#include "Storage/wav_parser.h"

float wav_audio_codec_pcm16_to_float(const uint8_t *p);
float wav_audio_codec_pcm24_to_float(const uint8_t *p);
float wav_audio_codec_pcm32_to_float(const uint8_t *p);
float wav_audio_codec_float32_to_float(const uint8_t *p);
typedef void (*wav_audio_codec_decode_block_fn)(const uint8_t *src,
                                                float *dst_interleaved,
                                                uint32_t frame_count);
typedef wav_audio_codec_decode_block_fn wav_audio_codec_decode_mono_block_fn;
wav_audio_codec_decode_block_fn wav_audio_codec_select_pcm_decode_block(uint16_t channels,
                                                                        uint16_t bits_per_sample);
wav_audio_codec_decode_block_fn wav_audio_codec_select_decode_block(
    wav_sample_encoding_t encoding, uint16_t channels, uint16_t bits_per_sample);
wav_audio_codec_decode_mono_block_fn wav_audio_codec_select_pcm_decode_mono_block(
    uint16_t bits_per_sample);
void wav_audio_codec_decode_stereo_frame(const uint8_t *frame,
                                         wav_sample_encoding_t encoding,
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
