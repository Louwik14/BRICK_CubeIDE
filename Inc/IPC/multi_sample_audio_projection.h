#pragma once

#include <stdint.h>

#include "Sampler/sample_audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Pointer-free, AUDIO-owned view published only when an instrument becomes
 * READY.  Paths and mutable loader/catalogue state deliberately stay out. */
typedef struct
{
    uint16_t multi_sample_id;
    uint16_t zone_id;
    uint16_t instrument_id;
    uint8_t root_note;
    int8_t pitch_semitones;
    uint8_t vel_low;
    uint8_t vel_high;
    uint8_t velocity_layer_count_for_note;
    uint8_t zone_is_single_velocity_layer;
    uint32_t total_frames;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint32_t registration_epoch;
    uint32_t loop_begin;
    uint32_t loop_end;
    sample_audio_format_t format;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint16_t stride_floats;
    uint32_t frames_per_page;
    uint8_t has_loop;
} multi_sample_audio_source_t;

_Static_assert(sizeof(multi_sample_audio_source_t) == 60U,
               "Multi AUDIO source ABI changed");

#ifdef __cplusplus
}
#endif
