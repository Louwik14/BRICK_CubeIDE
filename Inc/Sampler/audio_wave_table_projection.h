#ifndef AUDIO_WAVE_TABLE_PROJECTION_H
#define AUDIO_WAVE_TABLE_PROJECTION_H

#include <stdint.h>
#include "Sampler/wavetable_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint16_t wavetable_slot;
    uint16_t reserved;
    uint32_t generation;
} audio_wave_table_selection_t;

typedef struct
{
    uint32_t max_phase_increment;
    uint32_t cycle_sample_count;
    uint32_t sample_count;
    uint16_t cycle_magnitude;
    uint16_t flags;
    const float *data;
} audio_wavetable_band_t;

typedef struct
{
    uint32_t generation;
    uint32_t frame_count;
    uint32_t frame_sample_count;
    uint16_t wavetable_slot;
    uint16_t global_slot;
    uint16_t band_count;
    const float *base_data;
    audio_wavetable_band_t bands[WAVETABLE_MIPMAP_MAX_BANDS];
} audio_wavetable_descriptor_t;

#define AUDIO_WAVETABLE_VOICE_INSTANCE_COUNT (16U)
#define AUDIO_WAVETABLE_OSC_COUNT (2U)

_Static_assert(sizeof(audio_wavetable_band_t) == 20U,
               "Wavetable band layout changed");
_Static_assert(sizeof(audio_wavetable_descriptor_t) == 184U,
               "Wavetable descriptor layout changed");

_Static_assert(sizeof(audio_wave_table_selection_t) == 8U,
               "AUDIO wavetable selection must remain compact");

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_WAVE_TABLE_PROJECTION_H */
