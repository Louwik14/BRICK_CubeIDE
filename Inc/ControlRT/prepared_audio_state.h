#ifndef PREPARED_AUDIO_STATE_H
#define PREPARED_AUDIO_STATE_H

#include <stdint.h>

#include "ControlRT/control_audio_command.h"
#include "Mod/mod_destination_contract.h"
#include "Mod/mod_env3_control.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Param/param_filter.h"
#include "Param/param_global_control.h"
#include "Sampler/audio_wave_table_projection.h"
#include "Track/audio_fx_control_state.h"
#include "Track/fm_control_state.h"
#include "Track/mixer_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/tone_program_control.h"
#include "Track/vca_control_state.h"

#define PREPARED_AUDIO_SLOT_COUNT 1U
#define PREPARED_AUDIO_INVALID_SLOT UINT8_MAX
#define PREPARED_AUDIO_PARAM_MASK_WORDS ((PARAM_COUNT + 31U) / 32U)

typedef enum
{
    PREPARED_AUDIO_PRODUCT_NONE = 0U,
    PREPARED_AUDIO_PRODUCT_TONE,
    PREPARED_AUDIO_PRODUCT_FM
} prepared_audio_product_kind_t;

typedef enum
{
    PREPARED_AUDIO_RESOURCE_NONE = 0U,
    PREPARED_AUDIO_RESOURCE_SAMPLER,
    PREPARED_AUDIO_RESOURCE_WAVETABLE
} prepared_audio_resource_kind_t;

typedef struct
{
    uint8_t source;
    uint8_t enabled;
    mod_destination_address_t destination;
    float depth;
} prepared_audio_mod_route_t;

typedef struct
{
    mod_lfo_control_bank_t lfo;
    mod_env3_control_state_t envelope;
    uint8_t multi_source[2U][2U];
    uint8_t slew_source[2U];
    float slew_amount[2U];
    prepared_audio_mod_route_t route[8U];
} prepared_audio_mod_state_t;

typedef struct
{
    uint8_t kind;
    uint8_t present;
    uint16_t sampler_runtime;
    audio_wave_table_selection_t wavetable[2U];
} prepared_audio_resource_state_t;

typedef struct
{
    control_audio_program_descriptor_t program;
    uint8_t active;
    uint8_t topology_role;
    uint8_t product_kind;
    uint8_t modulation_present;
    uint8_t midi_channel;
    uint8_t midi_source;
    uint8_t ui_family;
    uint8_t reserved;
    union
    {
        tone_program_control_t tone;
        fm_control_state_t fm;
    } product;
    param_filter_control_state_t filter;
    vca_control_state_t vca;
    mixer_control_state_t mixer;
    audio_fx_control_state_t audio_fx;
    polyphony_control_state_t polyphony;
    prepared_audio_mod_state_t modulation;
    prepared_audio_resource_state_t resource;
} prepared_audio_entity_state_t;

typedef struct
{
    prepared_audio_entity_state_t entity[BRICK_ENTITY_CAPACITY];
    param_global_control_state_t global;
    uint32_t tempo_milli_bpm;
    uint32_t step_q16;
    uint16_t active_mask;
    uint16_t rebind_candidate_mask;
    uint32_t temp_clear_mask[BRICK_ENTITY_CAPACITY]
                            [PREPARED_AUDIO_PARAM_MASK_WORDS];
    uint8_t input_owner[2U];
    uint8_t metronome_level;
    uint8_t reserved[1U];
} prepared_audio_state_t;

typedef struct
{
    volatile uint32_t generation;
    volatile uint8_t reserved;
    volatile uint8_t ready;
    uint8_t transition;
    uint8_t reserved_byte;
    prepared_audio_state_t state;
} prepared_audio_slot_t;

_Static_assert(sizeof(prepared_audio_state_t) == 9052U,
               "Prepared AUDIO state size changed");
_Static_assert(sizeof(prepared_audio_slot_t) == 9060U,
               "Prepared AUDIO slot size changed");

extern prepared_audio_slot_t g_prepared_audio_slots[PREPARED_AUDIO_SLOT_COUNT];

uint8_t prepared_audio_control_reserve(uint8_t *out_slot,
                                       uint32_t *out_generation,
                                       prepared_audio_state_t **out_state);
void prepared_audio_control_abort(uint8_t slot, uint32_t generation);
uint8_t prepared_audio_control_preflight(uint8_t slot, uint32_t generation);
uint8_t prepared_audio_control_begin_install(uint8_t slot,
                                             uint32_t generation);
void prepared_audio_control_end_install(void);
uint8_t prepared_audio_control_publish(uint8_t slot, uint32_t generation,
                                       control_audio_state_transition_kind_t transition);

#endif
