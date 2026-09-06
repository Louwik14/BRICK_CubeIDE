#ifndef BRICK6_AUDIO_PREPARED_STATE_H
#define BRICK6_AUDIO_PREPARED_STATE_H

#include <stdint.h>

#include "IPC/audio_wave_table_projection.h"
#include "IPC/control_audio_command.h"
#include "IPC/fm_dsp_projection.h"
#include "Mod/mod_matrix.h"
#include "Mod/mod_lfo_v1.h"
#include "Param/param_ids.h"
#include "Track/entity_types.h"

#define AUDIO_PREPARED_STATE_VERSION 2U

typedef struct
{
    uint8_t valid;
    uint8_t active;
    control_audio_program_descriptor_t program;
    uint8_t polyphony_voice_count;
    uint8_t muted;
    uint8_t midi_channel_1_16;
    uint8_t midi_source;
    uint8_t lfo_valid;
    float lfo_value[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT];
    uint8_t sampler_asset_valid;
    uint16_t sampler_asset;
    uint16_t looper_route_mask;
    uint8_t wave_selection_valid[2U];
    audio_wave_table_selection_t wave_selection[2U];
    uint8_t fm_base_valid;
    track_tone_fm_base_voice_t fm_base;
    uint8_t matrix_valid;
    track_mod_matrix_slot_t matrix[MOD_MATRIX_SLOT_COUNT];
    uint8_t multi_source[2U][2U];
    uint8_t slew_source[2U];
    float slew_amount[2U];
    uint8_t fx_filter_position;
    uint8_t fx_order;
    uint8_t fx_spatial_mode[2U];
    uint8_t track_value_valid[PARAM_COUNT];
    float track_value[PARAM_COUNT];
} audio_prepared_entity_state_t;

typedef struct
{
    volatile uint32_t generation;
    volatile uint8_t ready;
    uint8_t version;
    uint8_t reserved[2U];
    audio_prepared_entity_state_t entity[BRICK_ENTITY_CAPACITY];
    uint8_t global_value_valid[PARAM_COUNT];
    float global_value[PARAM_COUNT];
    uint8_t input_owner[ENTITY_TOPOLOGY_AUDIO_SOURCE_COUNT];
    uint8_t metronome_level;
    uint32_t tempo_bpm_milli;
    uint32_t transport_step_q16;
} audio_prepared_state_t;

extern audio_prepared_state_t g_audio_prepared_state;

#endif /* BRICK6_AUDIO_PREPARED_STATE_H */
