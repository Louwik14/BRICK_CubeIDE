#include "Track/track_runtime.h"
#include "Track/entity_topology.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "Track/control_music_output.h"
#include "IPC/live_clock_control.h"
#include "Platform/brick_build_config.h"
#include "Track/track_input_ownership.h"
#include "Track/track_state.h"
#include "Param/param_registry_runtime_state.h"
#include "IPC/live_parameter_audio_publication.h"
#include "Seq/seq_model.h"
#include "stm32h7xx_hal.h"
#include "main.h"

#define TRACK_RUNTIME_FLAG_CAN_FILTER  (1U << 0)
#define TRACK_RUNTIME_FLAG_CAN_SYNTH   (1U << 1)
#define TRACK_RUNTIME_FLAG_CAN_PLAY    (1U << 2)
#define TRACK_RUNTIME_GROUP_BUS_TRACK BRICK_ENTITY_CAPACITY
SEQ_STATE_D2 static track_runtime_ctx_t g_track_runtime_ctx[SEQ_LANE_CAPACITY];
static uint32_t g_track_runtime_revision = 0U;
static uint32_t g_track_runtime_track_revision[SEQ_LANE_CAPACITY];
static uint8_t g_track_runtime_program_topology_flags[BRICK_ENTITY_CAPACITY];

static uint8_t track_runtime_is_group_fx_level_param(param_id_t param)
{
    return (uint8_t)(((param == PARAM_GROUP_FX_A_LEVEL)
                      || (param == PARAM_GROUP_FX_B_LEVEL)) ? 1U : 0U);
}

static uint8_t track_runtime_ctx_entity(
    const track_runtime_ctx_t *ctx,
    brick_entity_id_t *out_entity);

static uint8_t track_runtime_ctx_is_active(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_OFF)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_OTHER))
        return 0U;
    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
        return 1U;
    if (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP)
        return 1U;
    return (track_runtime_choose_engine(
        (track_runtime_family_t)ctx->family,
        (track_runtime_type_t)ctx->type) != TRACK_RUNTIME_ENGINE_NONE);
}

static uint8_t track_runtime_publish_program(brick_entity_id_t entity_id,
                                             const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;
    uint64_t due_sample = 0U;
    (void)live_clock_read_audio_sample(&due_sample);
    due_sample = control_music_output_first_unpublished_sample(due_sample);
    entity_topology_descriptor_t topology;
    uint8_t topology_flags = 0U;
    if (entity_topology_get(entity_id, &topology) != 0U)
    {
        if (topology.role == ENTITY_ROLE_GROUP_MASTER)
            topology_flags |= CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER;
        else if (topology.role == ENTITY_ROLE_GROUP_CHILD)
            topology_flags |= CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD;
    }
    const control_audio_program_descriptor_t descriptor = {
        .engine = (uint8_t)track_runtime_choose_engine(
            (track_runtime_family_t)ctx->family,
            (track_runtime_type_t)ctx->type),
        .family = ctx->family,
        .type = ctx->type,
        .flags = (uint8_t)(ctx->flags | topology_flags)
    };
    /* PROGRAM only changes the renderer.  The NOTE ledger remains authoritative
     * even while the selected renderer cannot render a live output. */
    if (control_audio_publish_program(entity_id,
                                      control_audio_program_pack(&descriptor),
                                      due_sample) == 0U)
    {
        /* A legal structural change is covered by the FIFO dimensioning
         * contract.  Refusal is an invariant failure, not a runtime mode. */
        Error_Handler();
        return 0U;
    }
    float tone_values[SEQ_PARAM_TONE_SLOT_COUNT];
    for (uint8_t slot = 0U; slot < SEQ_PARAM_TONE_SLOT_COUNT; ++slot)
    {
        if (param_registry_control_tone_get(
                entity_id, slot, &tone_values[slot]) == 0U)
        {
            Error_Handler();
            return 0U;
        }
    }
    if (!live_parameter_audio_publication_submit_tone_state(
            entity_id, tone_values))
    {
        Error_Handler();
        return 0U;
    }
    control_music_output_set_multi(entity_id,
        (uint8_t)((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)));
    return 1U;
}

static uint8_t track_runtime_topology_flags(brick_entity_id_t entity_id)
{
    entity_topology_descriptor_t topology;
    if (entity_topology_get(entity_id, &topology) == 0U)
        return 0U;
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
        return CONTROL_AUDIO_PROGRAM_FLAG_GROUP_MASTER;
    if (topology.role == ENTITY_ROLE_GROUP_CHILD)
        return CONTROL_AUDIO_PROGRAM_FLAG_GROUP_CHILD;
    return 0U;
}

static uint8_t track_runtime_publish_midi_config(
    brick_entity_id_t entity_id, const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (entity_id >= BRICK_ENTITY_CAPACITY))
        return 0U;
    uint64_t due_sample = 0U;
    (void)live_clock_read_audio_sample(&due_sample);
    due_sample = control_music_output_first_unpublished_sample(due_sample);
    const uint32_t packed = (uint32_t)ctx->midi_channel_1_16
        | ((uint32_t)ctx->midi_source << 8);
    return control_audio_publish_param(entity_id,
        CONTROL_AUDIO_PARAM_MIDI_CONFIG, packed, 0U, due_sample);
}

track_runtime_family_t track_runtime_family_from_ui(track_family_t family)
{
    if (family == TRACK_FAMILY_OFF)
    {
        return TRACK_RUNTIME_FAMILY_OFF;
    }

    if (family == TRACK_FAMILY_SYNTH)
    {
        return TRACK_RUNTIME_FAMILY_SYNTH;
    }
    if (family == TRACK_FAMILY_SAMPLER)
    {
        return TRACK_RUNTIME_FAMILY_SAMPLER;
    }
    if (family == TRACK_FAMILY_DRUM)
    {
        return TRACK_RUNTIME_FAMILY_DRUM;
    }
    if (family == TRACK_FAMILY_MIDI)
    {
        return TRACK_RUNTIME_FAMILY_MIDI;
    }
    if (family == TRACK_FAMILY_EXTERNAL)
    {
        return TRACK_RUNTIME_FAMILY_EXTERNAL;
    }

    return TRACK_RUNTIME_FAMILY_OTHER;
}

track_runtime_type_t track_runtime_type_from_ui(track_type_t type)
{
    switch (type)
    {
        case TRACK_TYPE_NONE:
            return TRACK_RUNTIME_TYPE_NONE;

        case TRACK_TYPE_RAM:
            return TRACK_RUNTIME_TYPE_RAM;
        case TRACK_TYPE_STREAM:
            return TRACK_RUNTIME_TYPE_STREAM;
        case TRACK_TYPE_PRISM:
            return TRACK_RUNTIME_TYPE_PRISM;
        case TRACK_TYPE_WAVE:
            return TRACK_RUNTIME_TYPE_WAVE;

        case TRACK_TYPE_DRUM_MD:
            return TRACK_RUNTIME_TYPE_DRUM_MD;
        case TRACK_TYPE_MIDI:
            return TRACK_RUNTIME_TYPE_MIDI;
        case TRACK_TYPE_DRUM_BD_ANALOG:
            return TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG;
        case TRACK_TYPE_LOOPER:
            return TRACK_RUNTIME_TYPE_LOOPER;
        case TRACK_TYPE_MULTI:
            return TRACK_RUNTIME_TYPE_MULTI;
        case TRACK_TYPE_GROUP:
            return TRACK_RUNTIME_TYPE_GROUP;
        case TRACK_TYPE_STACK:
            return TRACK_RUNTIME_TYPE_STACK;
        case TRACK_TYPE_FM:
            return TRACK_RUNTIME_TYPE_FM;
        case TRACK_TYPE_EXTERNAL:
            return TRACK_RUNTIME_TYPE_EXTERNAL;

        default:
            return TRACK_RUNTIME_TYPE_OTHER;
    }
}

track_runtime_engine_t track_runtime_choose_engine(
    track_runtime_family_t family, track_runtime_type_t type)
{
    if ((family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (type == TRACK_RUNTIME_TYPE_EXTERNAL))
        return TRACK_RUNTIME_ENGINE_AUDIO_TRACK;
    if (family == TRACK_RUNTIME_FAMILY_DRUM)
        return ((type == TRACK_RUNTIME_TYPE_DRUM_MD)
                || (type == TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG))
            ? TRACK_RUNTIME_ENGINE_DRUM : TRACK_RUNTIME_ENGINE_NONE;
    if (family == TRACK_RUNTIME_FAMILY_SAMPLER)
    {
        if (type == TRACK_RUNTIME_TYPE_LOOPER) return TRACK_RUNTIME_ENGINE_LOOPER;
        if ((type == TRACK_RUNTIME_TYPE_RAM) || (type == TRACK_RUNTIME_TYPE_STREAM)
                || (type == TRACK_RUNTIME_TYPE_MULTI))
            return TRACK_RUNTIME_ENGINE_SAMPLER;
    }
    if (family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        if (type == TRACK_RUNTIME_TYPE_PRISM) return TRACK_RUNTIME_ENGINE_PRISM;
        if (type == TRACK_RUNTIME_TYPE_STACK) return TRACK_RUNTIME_ENGINE_STACK;
        if (type == TRACK_RUNTIME_TYPE_WAVE) return TRACK_RUNTIME_ENGINE_WAVE;
        if (type == TRACK_RUNTIME_TYPE_FM) return TRACK_RUNTIME_ENGINE_FM;
    }
    return TRACK_RUNTIME_ENGINE_NONE;
}

static uint8_t track_runtime_logical_equal(const track_runtime_ctx_t *left,
                                           const track_runtime_ctx_t *right)
{
    return (uint8_t)((left != NULL) && (right != NULL)
            && (left->family == right->family)
            && (left->type == right->type)
            && (left->flags == right->flags));
}

uint8_t track_runtime_compute_flags(track_runtime_family_t family,
                                           track_runtime_type_t type)
{
    uint8_t flags = 0U;

    /* GROUP has no note engine, but owns the post-sum filter/MIX bus. */
    if (type == TRACK_RUNTIME_TYPE_GROUP)
    {
        return TRACK_RUNTIME_FLAG_CAN_FILTER;
    }

    if ((family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            || (family == TRACK_RUNTIME_FAMILY_SYNTH)
            || (family == TRACK_RUNTIME_FAMILY_SAMPLER)
            || (family == TRACK_RUNTIME_FAMILY_DRUM))
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_FILTER;
    }

    if ((family == TRACK_RUNTIME_FAMILY_SYNTH)
            || (family == TRACK_RUNTIME_FAMILY_DRUM))
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_SYNTH;
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }

    if ((family == TRACK_RUNTIME_FAMILY_SAMPLER)
            || (type == TRACK_RUNTIME_TYPE_RAM))
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }

    if (family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }
    if (family == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }

    return flags;
}

static uint8_t track_runtime_param_is_clip_only(param_id_t param)
{
    return (uint8_t)((param == PARAM_SAMPLER_CLIP_SOURCE_BPM)
                     || (param == PARAM_SAMPLER_CLIP_SYNC_LENGTH)
                     || (param == PARAM_SAMPLER_CLIP_PITCH)
                     || (param == PARAM_SAMPLER_CLIP_PLAY_MODE)
                     || (param == PARAM_SAMPLER_CLIP_LOOP)
                     || (param == PARAM_SAMPLER_CLIP_STRETCH_MODE)
                     || (param == PARAM_SAMPLER_CLIP_GRAIN)
                     || (param == PARAM_SAMPLER_CLIP_HOP)
                     || (param == PARAM_SAMPLER_CLIP_SEARCH));
}

static uint8_t track_runtime_param_is_looper_only(param_id_t param)
{
    return (uint8_t)((param == PARAM_LOOPER_ARM)
                     || (param == PARAM_LOOPER_LEN)
                     || (param == PARAM_LOOPER_PLAY)
                     || (param == PARAM_LOOPER_XFADE)
                     || (param == PARAM_LOOPER_STRETCH)
                     || (param == PARAM_LOOPER_PITCH)
                     || (param == PARAM_LOOPER_GRAIN));
}

static uint8_t track_runtime_param_is_vca(param_id_t param)
{
    return (uint8_t)((param == PARAM_VCA_ATTACK)
                     || (param == PARAM_VCA_DECAY)
                     || (param == PARAM_VCA_SUSTAIN)
                     || (param == PARAM_VCA_RELEASE)
                     || (param == PARAM_FILTER_MODE)
                     || (param == PARAM_ENV_RETRIG_VCA));
}

static uint8_t track_runtime_ctx_is_sampler_clip_or_looper(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    return (uint8_t)(((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                      && ((ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_STREAM)
                          || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))) ? 1U : 0U);
}

static const param_id_t g_track_runtime_tone_slots_prism[] = {
    PARAM_PRISM_OSC1_PARAM1,
    PARAM_PRISM_OSC1_PARAM2,
    PARAM_PRISM_OSC1_AMOD,
    PARAM_PRISM_OSC1_MODEL,
    PARAM_PRISM_VOLUME,
    PARAM_PRISM_BALANCE,
    PARAM_PRISM_TUNE,
    PARAM_PRISM_DETUNE,
    PARAM_PRISM_DRIFT,
    PARAM_PRISM_PITCH_MOD1,
    PARAM_PRISM_PHASE1_RESET,
    PARAM_PRISM_OSC2_PARAM1,
    PARAM_PRISM_OSC2_PARAM2,
    PARAM_PRISM_OSC2_AMOD,
    PARAM_PRISM_OSC2_MODEL,
    PARAM_PRISM_PITCH_MOD2
};

static const param_id_t g_track_runtime_tone_slots_stack[] = {
    PARAM_STACK_OSC1_LEVEL,
    PARAM_STACK_OSC2_LEVEL,
    PARAM_STACK_OSC3_LEVEL,
    PARAM_STACK_NOISE_LEVEL,
    PARAM_STACK_OSC1_MODEL,
    PARAM_STACK_OSC1_TUNE,
    PARAM_STACK_OSC1_TIMBRE,
    PARAM_STACK_OSC1_COLOR,
    PARAM_STACK_OSC2_MODEL,
    PARAM_STACK_OSC2_TUNE,
    PARAM_STACK_OSC2_TIMBRE,
    PARAM_STACK_OSC2_COLOR,
    PARAM_STACK_OSC3_MODEL,
    PARAM_STACK_OSC3_TUNE,
    PARAM_STACK_OSC3_TIMBRE,
    PARAM_STACK_OSC3_COLOR,
    PARAM_STACK_OSC_DETUNE,
    PARAM_STACK_PHASE_RESET
};

static const param_id_t g_track_runtime_tone_slots_wave[] = {
    PARAM_WAVE_OSC1_TABLE,
    PARAM_WAVE_OSC1_POS,
    PARAM_WAVE_OSC1_START,
    PARAM_WAVE_OSC1_LEN,
    PARAM_WAVE_OSC2_TABLE,
    PARAM_WAVE_OSC2_POS,
    PARAM_WAVE_OSC2_START,
    PARAM_WAVE_OSC2_LEN,
    PARAM_WAVE_VOLUME,
    PARAM_WAVE_BALANCE,
    PARAM_WAVE_TUNE,
    PARAM_WAVE_DETUNE
};

static const param_id_t g_track_runtime_tone_slots_fm[] = {
    PARAM_FM_RATIO,
    PARAM_FM_ALGORITHM,
    PARAM_FM_FEEDBACK,
    PARAM_FM_SYNC,
    PARAM_FM_BRIGHT,
    PARAM_FM_BODY,
    PARAM_FM_DETAIL,
    PARAM_FM_METAL,
    PARAM_FM_ENV_ATTACK,
    PARAM_FM_ENV_DECAY,
    PARAM_FM_ENV_SUSTAIN,
    PARAM_FM_ENV_RELEASE,
    PARAM_FM_PLAY_VEL,
    PARAM_FM_PLAY_KEY,
    PARAM_FM_PLAY_PITCH_ENV,
    PARAM_FM_PLAY_PITCH_TIME,
    PARAM_FM_OPERATOR_SELECT,
    PARAM_FM_TRANSPOSE,
    PARAM_FM_PITCH_R1,
    PARAM_FM_PITCH_R2,
    PARAM_FM_PITCH_R3,
    PARAM_FM_PITCH_R4,
    PARAM_FM_PITCH_L1,
    PARAM_FM_PITCH_L2,
    PARAM_FM_PITCH_L3,
    PARAM_FM_PITCH_L4
};

static const param_id_t g_track_runtime_tone_slots_sampler[] = {
    PARAM_SAMPLER_SAMPLE,
    PARAM_SAMPLER_GAIN,
    PARAM_SAMPLER_START,
    PARAM_SAMPLER_LENGTH,
    PARAM_SAMPLER_MODE,
    PARAM_SAMPLER_TUNE,
    PARAM_SAMPLER_LOOP_START,
    PARAM_SAMPLER_SLICE_COUNT
};

static const param_id_t g_track_runtime_tone_slots_clip[] = {
    PARAM_SAMPLER_SAMPLE,
    PARAM_SAMPLER_GAIN,
    PARAM_SAMPLER_CLIP_SOURCE_BPM,
    PARAM_SAMPLER_CLIP_PLAY_MODE,
    PARAM_SAMPLER_CLIP_LOOP,
    PARAM_SAMPLER_CLIP_STRETCH_MODE,
    PARAM_SAMPLER_CLIP_PITCH,
    PARAM_SAMPLER_CLIP_SYNC_LENGTH,
    PARAM_SAMPLER_CLIP_GRAIN,
    PARAM_SAMPLER_CLIP_HOP,
    PARAM_SAMPLER_CLIP_SEARCH
};

static const param_id_t g_track_runtime_tone_slots_looper[] = {
    PARAM_LOOPER_ARM,
    PARAM_LOOPER_LEN,
    PARAM_LOOPER_PLAY,
    PARAM_LOOPER_XFADE,
    PARAM_LOOPER_STRETCH,
    PARAM_LOOPER_PITCH,
    PARAM_LOOPER_GRAIN
};

static const param_id_t g_track_runtime_tone_slots_multi[] = {
    PARAM_SAMPLER_SAMPLE,
    PARAM_SAMPLER_GAIN,
    PARAM_SAMPLER_MULTI_LOOP
};

static const param_id_t g_track_runtime_tone_slots_midi[] = {
    PARAM_MIDI_PROGRAM,
    PARAM_MIDI_CC1_1, PARAM_MIDI_CC1_2, PARAM_MIDI_CC1_3, PARAM_MIDI_CC1_4,
    PARAM_MIDI_CC2_1, PARAM_MIDI_CC2_2, PARAM_MIDI_CC2_3, PARAM_MIDI_CC2_4,
    PARAM_MIDI_CC3_1, PARAM_MIDI_CC3_2, PARAM_MIDI_CC3_3, PARAM_MIDI_CC3_4
};

static const param_id_t g_track_runtime_tone_slots_drum_bd_analog[] = {
    PARAM_DRUM_TRX_BD_PITCH,
    PARAM_DRUM_TRX_BD_DECAY,
    PARAM_DRUM_TRX_BD_HARMONICS,
    PARAM_DRUM_TRX_BD_PITCH_SWEEP
};

static const param_id_t g_track_runtime_tone_slots_drum_md[] = {
    PARAM_DRUM_MD_MODEL,
    PARAM_DRUM_MD_P1, PARAM_DRUM_MD_P2, PARAM_DRUM_MD_P3, PARAM_DRUM_MD_P4,
    PARAM_DRUM_MD_P5, PARAM_DRUM_MD_P6, PARAM_DRUM_MD_P7, PARAM_DRUM_MD_P8
};

/* Deliberately minimal until the GROUP TONE product surface is completed. */
static const param_id_t g_track_runtime_tone_slots_group[] = {
    PARAM_FILTER_MORPH,
    PARAM_FILTER_CUTOFF,
    PARAM_FILTER_RESONANCE,
    PARAM_FILTER_KEYTRK
};

_Static_assert((sizeof(g_track_runtime_tone_slots_prism) / sizeof(g_track_runtime_tone_slots_prism[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "PRISM TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_stack) / sizeof(g_track_runtime_tone_slots_stack[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "STACK TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_wave) / sizeof(g_track_runtime_tone_slots_wave[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "WAVE TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_fm) / sizeof(g_track_runtime_tone_slots_fm[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "FM TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_sampler) / sizeof(g_track_runtime_tone_slots_sampler[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "SAMPLER TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_clip) / sizeof(g_track_runtime_tone_slots_clip[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "STREAM TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_looper) / sizeof(g_track_runtime_tone_slots_looper[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "LOOPER TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_multi) / sizeof(g_track_runtime_tone_slots_multi[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "MULTI TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_midi) / sizeof(g_track_runtime_tone_slots_midi[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "MIDI TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_drum_bd_analog) / sizeof(g_track_runtime_tone_slots_drum_bd_analog[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "DRUM BD TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_drum_md) / sizeof(g_track_runtime_tone_slots_drum_md[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "DRUM MD TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_group) / sizeof(g_track_runtime_tone_slots_group[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "GROUP TONE slots exceed compact capacity");

static uint8_t track_runtime_tone_table_for_type(track_runtime_type_t type,
                                                 const param_id_t **out_table,
                                                 uint8_t *out_count)
{
    if ((out_table == NULL) || (out_count == NULL))
    {
        return 0U;
    }

    switch (type)
    {
        case TRACK_RUNTIME_TYPE_PRISM:
            *out_table = g_track_runtime_tone_slots_prism;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_prism) / sizeof(g_track_runtime_tone_slots_prism[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_STACK:
            *out_table = g_track_runtime_tone_slots_stack;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_stack) / sizeof(g_track_runtime_tone_slots_stack[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_WAVE:
            *out_table = g_track_runtime_tone_slots_wave;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_wave) / sizeof(g_track_runtime_tone_slots_wave[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_FM:
            *out_table = g_track_runtime_tone_slots_fm;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_fm) / sizeof(g_track_runtime_tone_slots_fm[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_RAM:
            *out_table = g_track_runtime_tone_slots_sampler;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_sampler) / sizeof(g_track_runtime_tone_slots_sampler[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_STREAM:
            *out_table = g_track_runtime_tone_slots_clip;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_clip) / sizeof(g_track_runtime_tone_slots_clip[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_LOOPER:
            *out_table = g_track_runtime_tone_slots_looper;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_looper) / sizeof(g_track_runtime_tone_slots_looper[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_MULTI:
            *out_table = g_track_runtime_tone_slots_multi;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_multi) / sizeof(g_track_runtime_tone_slots_multi[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_MIDI:
            *out_table = g_track_runtime_tone_slots_midi;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_midi) / sizeof(g_track_runtime_tone_slots_midi[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_EXTERNAL:
            *out_table = g_track_runtime_tone_slots_midi;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_midi) / sizeof(g_track_runtime_tone_slots_midi[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            *out_table = g_track_runtime_tone_slots_drum_bd_analog;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_drum_bd_analog) / sizeof(g_track_runtime_tone_slots_drum_bd_analog[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_DRUM_MD:
            *out_table = g_track_runtime_tone_slots_drum_md;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_drum_md) / sizeof(g_track_runtime_tone_slots_drum_md[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_GROUP:
            *out_table = g_track_runtime_tone_slots_group;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_group) / sizeof(g_track_runtime_tone_slots_group[0]));
            return 1U;

        default:
            return 0U;
    }
}

static uint8_t track_runtime_tone_drum_range(track_runtime_type_t type, param_id_t *out_first, uint8_t *out_count)
{
    if ((out_first == NULL) || (out_count == NULL))
    {
        return 0U;
    }

    switch (type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_MD:
            *out_first = PARAM_DRUM_MD_MODEL; *out_count = 9U; return 1U;
        default:
            return 0U;
    }
}

static uint16_t track_runtime_compute_ui_ensemble_mask(
    brick_entity_id_t entity_id,
    const track_runtime_ctx_t *ctx,
    uint8_t active)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    entity_topology_descriptor_t topology;
    if ((entity_topology_get(entity_id, &topology) == 0U)
            || (topology.active == 0U))
    {
        return 0U;
    }
    const uint16_t topology_capabilities =
        entity_topology_get_capabilities(&topology);

    uint16_t mask = 0U;
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_CFG);
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_SEQ);
    if (topology.role == ENTITY_ROLE_GROUP_MASTER)
    {
        if (active != 0U)
        {
            mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_TONE);
            mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_ENV);
            mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MOD);
            mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIX);
            mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_FX);
        }
        return mask;
    }
    if ((topology_capabilities & (uint16_t)TRACK_CAPABILITY_KEYBOARD) != 0U)
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_KEYBOARD);
    }
    if (((topology_capabilities & (uint16_t)TRACK_CAPABILITY_MIDI_FX) != 0U)
            && (ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_OFF)
            && !((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER)))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX);
    }

    if (active == 0U)
    {
        return mask;
    }

    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_TONE);
    if (!(((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_LOOPER)))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MOD);
    }

    if (((topology_capabilities & (uint16_t)TRACK_CAPABILITY_NOTES) != 0U)
            && ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) != 0U))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_PLAY);
    }

    if (((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
            && (track_runtime_is_audio_routable(entity_id) != 0U))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_ENV);
    }

    if (track_runtime_is_audio_routable(entity_id) != 0U)
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIX);
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_FX);
    }

    return mask;
}

track_runtime_voice_mode_t track_runtime_get_voice_mode(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return TRACK_RUNTIME_VOICE_MODE_MONO;
    }

    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
    {
        return TRACK_RUNTIME_VOICE_MODE_POLY;
    }

    return TRACK_RUNTIME_VOICE_MODE_MONO;
}

uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx)
{
    if (track_runtime_ctx_is_active(ctx) == 0U)
        return 0U;
    if ((track_runtime_ctx_is_sampler_clip_or_looper(ctx) != 0U)
            && (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
    {
        return 0U;
    }

    if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH))
    {
        return 1U;
    }

    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return 1U;
    }

    return 0U;
}

static void track_runtime_prepare_ctx_base(uint8_t track, track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (track >= SEQ_LANE_CAPACITY))
    {
        return;
    }

    entity_topology_descriptor_t entity;
    if ((entity_topology_get((brick_entity_id_t)track, &entity) == 0U)
            || (entity.active == 0U))
    {
        memset(ctx, 0, sizeof(*ctx));
        ctx->family = (uint8_t)TRACK_RUNTIME_FAMILY_OFF;
        ctx->type = (uint8_t)TRACK_RUNTIME_TYPE_NONE;
        return;
    }

    const track_config_t config = track_state_get_config(track);
    const track_type_t ui_type = config.type;
    track_runtime_family_t family = track_runtime_family_from_ui(config.family);
    track_runtime_type_t type = track_runtime_type_from_ui(ui_type);
    memset(ctx, 0, sizeof(*ctx));
    ctx->midi_channel_1_16 = track_state_get_midi_channel(track);
    ctx->midi_source = (uint8_t)track_state_get_midi_source(track);
    ctx->family = (uint8_t)family;
    ctx->type = (uint8_t)type;
    ctx->flags = track_runtime_compute_flags(family, type);
    if (entity_topology_has_capability(track, TRACK_CAPABILITY_NOTES) == 0U)
    {
        ctx->flags &= (uint8_t)~(TRACK_RUNTIME_FLAG_CAN_PLAY | TRACK_RUNTIME_FLAG_CAN_SYNTH);
    }
}

void track_runtime_init(void)
{
    memset(&g_track_runtime_ctx, 0, sizeof(g_track_runtime_ctx));
    memset(g_track_runtime_program_topology_flags, 0,
           sizeof(g_track_runtime_program_topology_flags));
    track_runtime_rebuild_all();
}

void track_runtime_rebuild_all(void)
{
    track_runtime_ctx_t previous[SEQ_LANE_CAPACITY];
    track_runtime_ctx_t prepared[SEQ_LANE_CAPACITY];
    memcpy(previous, g_track_runtime_ctx, sizeof(previous));
    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
        track_runtime_prepare_ctx_base(track, &prepared[track]);

    for (brick_entity_id_t entity = 0U;
         entity < (brick_entity_id_t)SEQ_LANE_CAPACITY; ++entity)
    {
        const uint8_t next_topology_flags =
            track_runtime_topology_flags(entity);
        const uint8_t structure_changed = (uint8_t)(
            (track_runtime_logical_equal(
                &prepared[entity], &previous[entity]) == 0U)
            || (next_topology_flags
                != g_track_runtime_program_topology_flags[entity]));
        const uint8_t midi_changed = (uint8_t)(
            (prepared[entity].midi_channel_1_16
                != previous[entity].midi_channel_1_16)
            || (prepared[entity].midi_source != previous[entity].midi_source));
        if (structure_changed != 0U)
        {
            if (track_runtime_publish_program(entity, &prepared[entity]) != 0U)
            {
                g_track_runtime_ctx[entity] = prepared[entity];
                g_track_runtime_program_topology_flags[entity] =
                    next_topology_flags;
            }
        }
        else if (midi_changed != 0U)
            g_track_runtime_ctx[entity] = prepared[entity];
        if (midi_changed != 0U)
            (void)track_runtime_publish_midi_config(entity, &prepared[entity]);
    }

    ++g_track_runtime_revision;
    for (uint8_t track = 0U; track < (uint8_t)SEQ_LANE_CAPACITY; ++track)
        g_track_runtime_track_revision[track] = g_track_runtime_revision;
}

uint8_t track_runtime_is_track_prism_available(uint8_t track)
{
    return entity_topology_is_active((brick_entity_id_t)track);
}

void track_runtime_rebuild_track(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    entity_topology_descriptor_t topology;
    if ((entity_topology_get((brick_entity_id_t)track, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
    {
        track_runtime_rebuild_all();
        return;
    }

    const track_runtime_ctx_t previous = g_track_runtime_ctx[track];
    track_runtime_ctx_t next_ctx;

    track_runtime_prepare_ctx_base(track, &next_ctx);
    const uint8_t next_topology_flags = track_runtime_topology_flags(track);
    const uint8_t structure_changed = (uint8_t)(
        (track_runtime_logical_equal(&next_ctx, &previous) == 0U)
        || (next_topology_flags
            != g_track_runtime_program_topology_flags[track]));
    const uint8_t midi_changed = (uint8_t)(
        (next_ctx.midi_channel_1_16 != previous.midi_channel_1_16)
        || (next_ctx.midi_source != previous.midi_source));
    if (structure_changed != 0U)
    {
        if (track_runtime_publish_program(track, &next_ctx) != 0U)
        {
            g_track_runtime_ctx[track] = next_ctx;
            g_track_runtime_program_topology_flags[track] =
                next_topology_flags;
        }
    }
    else if (midi_changed != 0U)
        g_track_runtime_ctx[track] = next_ctx;
    if (midi_changed != 0U)
        (void)track_runtime_publish_midi_config(track, &next_ctx);
    ++g_track_runtime_revision;
    g_track_runtime_track_revision[track] = g_track_runtime_revision;
}

uint32_t track_runtime_get_revision(void)
{
    return g_track_runtime_revision;
}

uint32_t track_runtime_get_track_revision(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    return g_track_runtime_track_revision[track];
}

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0;
    }

    return &g_track_runtime_ctx[track];
}

static uint8_t track_runtime_ctx_entity(
    const track_runtime_ctx_t *ctx,
    brick_entity_id_t *out_entity)
{
    if ((ctx == NULL) || (out_entity == NULL))
        return 0U;
    for (brick_entity_id_t entity = 0U;
         entity < (brick_entity_id_t)SEQ_LANE_CAPACITY; ++entity)
        if (ctx == &g_track_runtime_ctx[entity])
        {
            *out_entity = entity;
            return 1U;
        }
    return 0U;
}

uint8_t track_runtime_is_audio_routable_ctx(const track_runtime_ctx_t *ctx)
{
    brick_entity_id_t entity;
    if ((track_runtime_ctx_is_active(ctx) == 0U)
            || (track_runtime_ctx_entity(ctx, &entity) == 0U)
            || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI))
        return 0U;
    if ((track_runtime_family_t)ctx->family
            == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return track_input_ownership_track_owns_input(
            entity, track_input_ownership_get_external_input(entity));
    }
    return 1U;
}

uint8_t track_runtime_is_audio_routable(uint8_t track)
{
    return track_runtime_is_audio_routable_ctx(track_runtime_get_ctx(track));
}

uint8_t track_runtime_has_capability(uint8_t track, track_capability_t capability)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    if (entity_topology_has_capability((brick_entity_id_t)track,
                                       capability) == 0U)
    {
        return 0U;
    }

    switch (capability)
    {
        case TRACK_CAPABILITY_NOTES:
        case TRACK_CAPABILITY_KEYBOARD:
        case TRACK_CAPABILITY_MIDI_FX:
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) != 0U) ? 1U : 0U;

        case TRACK_CAPABILITY_AUDIO:
            return track_runtime_is_audio_routable(track);

        case TRACK_CAPABILITY_MIDI:
            return (uint8_t)((((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SYNTH)
                    || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SAMPLER)
                    || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_DRUM)
                    || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_MIDI)
                    || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_EXTERNAL)) ? 1U : 0U);

        case TRACK_CAPABILITY_AUTOMATION:
            return 0U;

        case TRACK_CAPABILITY_MUTE:
            return track_runtime_ctx_is_active(ctx);

        case TRACK_CAPABILITY_INPUT_RESERVATION:
            return (uint8_t)(((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_EXTERNAL)
                    ? 1U : 0U);

        default:
            return 0U;
    }
}

uint8_t track_runtime_get_mix_target_track(uint8_t track, uint8_t *out_mix_track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (track_runtime_is_audio_routable(track) == 0U))
    {
        return 0U;
    }

    if (out_mix_track != NULL)
        *out_mix_track = (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP)
            ? TRACK_RUNTIME_GROUP_BUS_TRACK : track;

    return 1U;
}

uint8_t track_runtime_resolve_filter_target_track(uint8_t ui_track, uint8_t *out_filter_track)
{
    if ((out_filter_track == NULL) || (ui_track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(ui_track);
    if ((ctx == NULL) || (track_runtime_ctx_is_active(ctx) == 0U))
    {
        return 0U;
    }

    if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) == 0U)
    {
        return 0U;
    }

    if (track_runtime_get_mix_target_track(ui_track, out_filter_track) == 0U)
    {
        return 0U;
    }

    return 1U;
}

uint8_t track_runtime_get_midi_channel_1_16(uint8_t track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return 1U;
    }

    const uint8_t channel = ctx->midi_channel_1_16;
    return (channel < 1U) ? 1U : ((channel > 16U) ? 16U : channel);
}

uint8_t track_runtime_get_midi_channel_zero_based(uint8_t track)
{
    return (uint8_t)(track_runtime_get_midi_channel_1_16(track) - 1U);
}

track_runtime_midi_source_t track_runtime_get_midi_source(uint8_t track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return TRACK_RUNTIME_MIDI_SOURCE_ALL;
    }

    switch ((track_midi_source_t)ctx->midi_source)
    {
        case TRACK_MIDI_SOURCE_INTERNAL:
            return TRACK_RUNTIME_MIDI_SOURCE_INTERNAL;
        case TRACK_MIDI_SOURCE_EXTERNAL:
            return TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL;
        case TRACK_MIDI_SOURCE_ALL:
        default:
            return TRACK_RUNTIME_MIDI_SOURCE_ALL;
    }
}

uint8_t track_runtime_get_descriptor(uint8_t track, track_runtime_descriptor_t *out_descriptor)
{
    if ((track >= SEQ_LANE_CAPACITY) || (out_descriptor == NULL))
    {
        return 0U;
    }
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    entity_topology_descriptor_t topology;
    if ((entity_topology_get((brick_entity_id_t)track, &topology) == 0U)
            || (topology.active == 0U))
    {
        return 0U;
    }
    const uint16_t topology_capabilities =
        entity_topology_get_capabilities(&topology);

    const uint8_t active = track_runtime_ctx_is_active(ctx);
    out_descriptor->family = (track_runtime_family_t)ctx->family;
    out_descriptor->type = (track_runtime_type_t)ctx->type;
    out_descriptor->engine = track_runtime_choose_engine(
        out_descriptor->family, out_descriptor->type);
    out_descriptor->active = active;
    out_descriptor->instance_id = active != 0U ? track : 0xFFU;
    out_descriptor->mix_track_id = (active == 0U)
        || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI)
        ? 0xFFU
        : ((topology.role == ENTITY_ROLE_GROUP_MASTER)
            ? TRACK_RUNTIME_GROUP_BUS_TRACK : track);
    out_descriptor->flags = ctx->flags;
    out_descriptor->midi_channel_1_16 = track_runtime_get_midi_channel_1_16(track);
    out_descriptor->ui_ensemble_mask = track_runtime_compute_ui_ensemble_mask(
        track, ctx, active);
    out_descriptor->topology_capabilities = topology_capabilities;
    return 1U;
}

uint8_t track_runtime_resolve_track(uint8_t track, track_runtime_resolved_track_t *out_resolved)
{
    if ((track >= SEQ_LANE_CAPACITY) || (out_resolved == NULL))
    {
        return 0U;
    }

    memset(out_resolved, 0, sizeof(*out_resolved));
    out_resolved->track_id = track;
    out_resolved->midi_source = TRACK_RUNTIME_MIDI_SOURCE_ALL;

    if (track_runtime_get_descriptor(track, &out_resolved->descriptor) == 0U)
    {
        return 0U;
    }

    out_resolved->midi_channel_zero_based = (uint8_t)((out_resolved->descriptor.midi_channel_1_16 > 0U)
            ? (out_resolved->descriptor.midi_channel_1_16 - 1U)
            : 0U);
    out_resolved->midi_source = track_runtime_get_midi_source(track);

    uint8_t mix_track = 0U;
    if (track_runtime_get_mix_target_track(track, &mix_track) != 0U)
    {
        out_resolved->has_mix_target = 1U;
        out_resolved->mix_track_id = mix_track;
    }

    uint8_t filter_track = 0U;
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        out_resolved->has_filter_target = 1U;
        out_resolved->filter_track_id = filter_track;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    out_resolved->supports_vca_gate = track_runtime_supports_vca_gate(ctx);
    return 1U;
}

uint8_t track_runtime_is_ui_ensemble_available(uint8_t track, track_runtime_ui_ensemble_t ensemble)
{
    if ((uint8_t)ensemble >= (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_COUNT)
    {
        return 0U;
    }

    track_runtime_descriptor_t descriptor;
    if (track_runtime_get_descriptor(track, &descriptor) == 0U)
    {
        return 0U;
    }

    return (uint8_t)((descriptor.ui_ensemble_mask & (uint16_t)(1U << (uint8_t)ensemble)) != 0U);
}

track_runtime_param_rule_t track_runtime_get_param_rule(param_id_t param)
{
    track_runtime_param_rule_t rule = {
        .domain = TRACK_RUNTIME_PARAM_DOMAIN_NONE,
        .resource = TRACK_RUNTIME_RESOURCE_NONE,
        .cardinality = TRACK_RUNTIME_CARDINALITY_PER_TRACK,
        .status = TRACK_RUNTIME_PARAM_ALLOWED
    };

    if ((param >= PARAM_FM_OPERATOR_FIRST) && (param <= PARAM_FM_OPERATOR_LAST))
    {
        rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
        rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
        return rule;
    }

    if ((param >= PARAM_FM_DX7_HIDDEN_FIRST) && (param <= PARAM_FM_DX7_HIDDEN_LAST))
    {
        rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
        rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
        return rule;
    }

    if ((param >= PARAM_FM_UI_FIRST) && (param <= PARAM_FM_UI_LAST))
    {
        rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
        rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
        return rule;
    }

    switch (param)
    {
        case PARAM_FILTER_MORPH:
        case PARAM_FILTER_CUTOFF:
        case PARAM_FILTER_RESONANCE:
        case PARAM_FILTER_EG_AMT:
        case PARAM_FILTER_ATTACK:
        case PARAM_FILTER_DECAY:
        case PARAM_FILTER_SUSTAIN:
        case PARAM_FILTER_RELEASE:
        case PARAM_FILTER_KEYTRK:
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
        case PARAM_ENV_RETRIG_FILTER:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_ENV;
            rule.resource = TRACK_RUNTIME_RESOURCE_FILTER;
            return rule;
        case PARAM_VCA_ATTACK:
        case PARAM_VCA_DECAY:
        case PARAM_VCA_SUSTAIN:
        case PARAM_VCA_RELEASE:
        case PARAM_FILTER_MODE:
        case PARAM_ENV_RETRIG_VCA:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_ENV;
            rule.resource = TRACK_RUNTIME_RESOURCE_MIX;
            return rule;
        case PARAM_ENV3_ATTACK:
        case PARAM_ENV3_DECAY:
        case PARAM_ENV3_SUSTAIN:
        case PARAM_ENV3_RELEASE:
        case PARAM_ENV_RETRIG_MOD:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_ENV;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;
        case PARAM_DRUM_TRX_BD_PITCH:
        case PARAM_DRUM_TRX_BD_DECAY:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
        case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
        case PARAM_DRUM_TRX_BD_ATTACK:
        case PARAM_DRUM_TRX_BD_NOISE:
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_DRIVE:
        case PARAM_DRUM_MD_MODEL:
        case PARAM_DRUM_MD_P1:
        case PARAM_DRUM_MD_P2:
        case PARAM_DRUM_MD_P3:
        case PARAM_DRUM_MD_P4:
        case PARAM_DRUM_MD_P5:
        case PARAM_DRUM_MD_P6:
        case PARAM_DRUM_MD_P7:
        case PARAM_DRUM_MD_P8:
        case PARAM_PRISM_OSC1_MODEL:
        case PARAM_PRISM_VOLUME:
        case PARAM_PRISM_TUNE:
        case PARAM_PRISM_PITCH_MOD1:
        case PARAM_PRISM_OSC1_PARAM1:
        case PARAM_PRISM_OSC1_AMOD:
        case PARAM_PRISM_OSC1_PARAM2:
        case PARAM_PRISM_PHASE1_RESET:
        case PARAM_PRISM_BALANCE:
        case PARAM_PRISM_OSC2_MODEL:
        case PARAM_PRISM_DETUNE:
        case PARAM_PRISM_DRIFT:
        case PARAM_PRISM_PITCH_MOD2:
        case PARAM_PRISM_OSC2_PARAM1:
        case PARAM_PRISM_OSC2_AMOD:
        case PARAM_PRISM_OSC2_PARAM2:
        case PARAM_FM_RATIO:
        case PARAM_FM_ALGORITHM:
        case PARAM_FM_FEEDBACK:
        case PARAM_FM_SYNC:
        case PARAM_FM_BRIGHT:
        case PARAM_FM_BODY:
        case PARAM_FM_DETAIL:
        case PARAM_FM_METAL:
        case PARAM_FM_ENV_ATTACK:
        case PARAM_FM_ENV_DECAY:
        case PARAM_FM_ENV_SUSTAIN:
        case PARAM_FM_ENV_RELEASE:
        case PARAM_FM_PLAY_VEL:
        case PARAM_FM_PLAY_KEY:
        case PARAM_FM_PLAY_PITCH_ENV:
        case PARAM_FM_PLAY_PITCH_TIME:
        case PARAM_FM_OPERATOR_SELECT:
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_NOISE_LEVEL:
        case PARAM_STACK_OSC1_MODEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_MODEL:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_STACK_OSC3_TIMBRE:
        case PARAM_STACK_OSC3_COLOR:
        case PARAM_STACK_OSC_DETUNE:
        case PARAM_STACK_PHASE_RESET:
        case PARAM_WAVE_OSC1_TABLE:
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC1_LEN:
        case PARAM_WAVE_OSC2_TABLE:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC2_START:
        case PARAM_WAVE_OSC2_LEN:
        case PARAM_WAVE_VOLUME:
        case PARAM_WAVE_BALANCE:
        case PARAM_WAVE_TUNE:
        case PARAM_WAVE_DETUNE:
        case PARAM_MIDI_PROGRAM:
        case PARAM_MIDI_CC1_1:
        case PARAM_MIDI_CC1_2:
        case PARAM_MIDI_CC1_3:
        case PARAM_MIDI_CC1_4:
        case PARAM_MIDI_CC2_1:
        case PARAM_MIDI_CC2_2:
        case PARAM_MIDI_CC2_3:
        case PARAM_MIDI_CC2_4:
        case PARAM_MIDI_CC3_1:
        case PARAM_MIDI_CC3_2:
        case PARAM_MIDI_CC3_3:
        case PARAM_MIDI_CC3_4:
        case PARAM_EXTERNAL_INPUT:
        case PARAM_SAMPLER_SAMPLE:
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_START:
        case PARAM_SAMPLER_LENGTH:
        case PARAM_SAMPLER_MODE:
        case PARAM_SAMPLER_TUNE:
        case PARAM_SAMPLER_SLICE_COUNT:
        case PARAM_SAMPLER_LOOP_START:
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
        case PARAM_SAMPLER_CLIP_PITCH:
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
        case PARAM_SAMPLER_CLIP_LOOP:
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
        case PARAM_SAMPLER_CLIP_GRAIN:
        case PARAM_SAMPLER_CLIP_HOP:
        case PARAM_SAMPLER_CLIP_SEARCH:
        case PARAM_SAMPLER_MULTI_LOOP:
        case PARAM_LOOPER_ARM:
        case PARAM_LOOPER_LEN:
        case PARAM_LOOPER_PLAY:
        case PARAM_LOOPER_XFADE:
        case PARAM_LOOPER_STRETCH:
        case PARAM_LOOPER_PITCH:
        case PARAM_LOOPER_GRAIN:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;

        case PARAM_MIX_LEVEL:
        case PARAM_MIX_PAN:
        case PARAM_MIX_SEND1:
        case PARAM_MIX_SEND2:
        case PARAM_MIX_SEND3:
        case PARAM_MIX_MUTE:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_MIX;
            rule.resource = TRACK_RUNTIME_RESOURCE_MIX;
            return rule;

        case PARAM_SEQ_PLAY_V1_NOTE:
        case PARAM_SEQ_PLAY_V1_VEL:
        case PARAM_SEQ_PLAY_V1_LEN:
        case PARAM_SEQ_PLAY_V1_MICTIM:
        case PARAM_SEQ_PLAY_V2_NOTE:
        case PARAM_SEQ_PLAY_V2_VEL:
        case PARAM_SEQ_PLAY_V2_LEN:
        case PARAM_SEQ_PLAY_V2_MICTIM:
        case PARAM_SEQ_PLAY_V3_NOTE:
        case PARAM_SEQ_PLAY_V3_VEL:
        case PARAM_SEQ_PLAY_V3_LEN:
        case PARAM_SEQ_PLAY_V3_MICTIM:
        case PARAM_SEQ_PLAY_V4_NOTE:
        case PARAM_SEQ_PLAY_V4_VEL:
        case PARAM_SEQ_PLAY_V4_LEN:
        case PARAM_SEQ_PLAY_V4_MICTIM:
        case PARAM_SEQ_PLAY_V5_NOTE:
        case PARAM_SEQ_PLAY_V5_VEL:
        case PARAM_SEQ_PLAY_V5_LEN:
        case PARAM_SEQ_PLAY_V5_MICTIM:
        case PARAM_SEQ_PLAY_V6_NOTE:
        case PARAM_SEQ_PLAY_V6_VEL:
        case PARAM_SEQ_PLAY_V6_LEN:
        case PARAM_SEQ_PLAY_V6_MICTIM:
        case PARAM_SEQ_PLAY_V7_NOTE:
        case PARAM_SEQ_PLAY_V7_VEL:
        case PARAM_SEQ_PLAY_V7_LEN:
        case PARAM_SEQ_PLAY_V7_MICTIM:
        case PARAM_SEQ_PLAY_V8_NOTE:
        case PARAM_SEQ_PLAY_V8_VEL:
        case PARAM_SEQ_PLAY_V8_LEN:
        case PARAM_SEQ_PLAY_V8_MICTIM:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_PLAY;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;

        case PARAM_CFG_POLY_VOICES:
        case PARAM_CFG_POLY_SPREAD:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_CFG;
            rule.resource = TRACK_RUNTIME_RESOURCE_POLYPHONY;
            return rule;

        case PARAM_LFO1_RATE:
        case PARAM_LFO1_SHAPE:
        case PARAM_LFO1_TRIG:
        case PARAM_LFO1_PHASE:
        case PARAM_LFO2_RATE:
        case PARAM_LFO2_SHAPE:
        case PARAM_LFO2_TRIG:
        case PARAM_LFO2_PHASE:
        case PARAM_LFO3_RATE:
        case PARAM_LFO3_SHAPE:
        case PARAM_LFO3_TRIG:
        case PARAM_LFO3_PHASE:
        case PARAM_MOD_MATRIX_SLOT:
        case PARAM_MOD_MATRIX_SOURCE:
        case PARAM_MOD_MATRIX_DEST:
        case PARAM_MOD_MATRIX_DEPTH:
        case PARAM_MOD_MULTI_1_A:
        case PARAM_MOD_MULTI_1_B:
        case PARAM_MOD_MULTI_2_A:
        case PARAM_MOD_MULTI_2_B:
        case PARAM_MOD_SLEW_1_SOURCE:
        case PARAM_MOD_SLEW_1_AMOUNT:
        case PARAM_MOD_SLEW_2_SOURCE:
        case PARAM_MOD_SLEW_2_AMOUNT:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_MOD;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;

        case PARAM_MIDI_FX_S1_PARAM1:
        case PARAM_MIDI_FX_S1_PARAM2:
        case PARAM_MIDI_FX_S1_PARAM3:
        case PARAM_MIDI_FX_S1_MODEL:
        case PARAM_MIDI_FX_S2_PARAM1:
        case PARAM_MIDI_FX_S2_PARAM2:
        case PARAM_MIDI_FX_S2_PARAM3:
        case PARAM_MIDI_FX_S2_MODEL:
        case PARAM_MIDI_FX_S3_PARAM1:
        case PARAM_MIDI_FX_S3_PARAM2:
        case PARAM_MIDI_FX_S3_PARAM3:
        case PARAM_MIDI_FX_S3_MODEL:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_MIDI_FX;
            rule.resource = TRACK_RUNTIME_RESOURCE_MIDI_FX;
            return rule;

        case PARAM_AUDIO_FX_P1:
        case PARAM_AUDIO_FX_P2:
        case PARAM_AUDIO_FX_P3:
        case PARAM_AUDIO_FX_MODEL:
        case PARAM_AUDIO_FX_B_P1:
        case PARAM_AUDIO_FX_B_P2:
        case PARAM_AUDIO_FX_B_P3:
        case PARAM_AUDIO_FX_B_MODEL:
        case PARAM_AUDIO_FX_FILTER_POS:
        case PARAM_AUDIO_FX_ORDER:
        case PARAM_AUDIO_FX_MODE_A:
        case PARAM_AUDIO_FX_MODE_B:
        case PARAM_GROUP_FX_A_LEVEL:
        case PARAM_GROUP_FX_B_LEVEL:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX;
            rule.resource = TRACK_RUNTIME_RESOURCE_AUDIO_FX;
            return rule;

        case PARAM_MIX_REVERB_WET:
        case PARAM_MODFX_MODEL:
        case PARAM_MODFX_RATE:
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET:
        case PARAM_MIX_REVERB_ROOM_SIZE:
        case PARAM_MIX_REVERB_DAMPING:
        case PARAM_MIX_REVERB_WIDTH:
        case PARAM_MIX_REVERB_HPF:
        case PARAM_MIX_REVERB_LPF:
        case PARAM_MIX_REVERB_DELAYS:
        case PARAM_MIX_DELAY_TYPE:
        case PARAM_MIX_DELAY_TIME:
        case PARAM_MIX_DELAY_PINGPONG:
        case PARAM_MIX_DELAY_MODE:
        case PARAM_MIX_DELAY_TIME_R:
        case PARAM_MIX_DELAY_WIDTH:
        case PARAM_MIX_DELAY_FEEDBACK:
        case PARAM_MIX_DELAY_SPECTRAL_POSITION:
        case PARAM_MIX_DELAY_SPECTRAL_WIDTH:
        case PARAM_MIX_DELAY_FBW:
        case PARAM_MIX_DELAY_MOD:
        case PARAM_MIX_DELAY_MOD_RATE:
        case PARAM_MIX_DELAY_REV:
        case PARAM_MIX_DELAY_VOL:
        case PARAM_COMP_MODEL:
        case PARAM_BUS_COMP_THRESHOLD_DB:
        case PARAM_BUS_COMP_RATIO:
        case PARAM_BUS_COMP_ATTACK_INDEX:
        case PARAM_BUS_COMP_RELEASE_INDEX:
        case PARAM_BUS_COMP_MAKEUP_DB:
        case PARAM_BUS_COMP_DRYWET:
        case PARAM_BUS_COMP_HPF_HZ:
        case PARAM_COMP_DETECT:
        case PARAM_COMP_KNEE_DB:
        case PARAM_COMP_DELUGE_SAT:
        case PARAM_MASTER_GAIN:
        case PARAM_POST_GAIN:
        case PARAM_OUTPUT_COMP:
            rule.cardinality = TRACK_RUNTIME_CARDINALITY_GLOBAL;
            rule.status = TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED;
            return rule;

        default:
            return rule;
    }
}

uint8_t track_runtime_tone_slot_to_param(track_runtime_type_t type,
                                         uint8_t slot,
                                         param_id_t *out_param)
{
    if (out_param == NULL)
    {
        return 0U;
    }

    const param_id_t *table = NULL;
    uint8_t count = 0U;
    if (track_runtime_tone_table_for_type(type, &table, &count) == 0U)
    {
        return 0U;
    }

    if (slot >= count)
    {
        return 0U;
    }

    if (table != NULL)
    {
        *out_param = table[slot];
        return 1U;
    }

    param_id_t first = PARAM_COUNT;
    uint8_t drum_count = 0U;
    if (track_runtime_tone_drum_range(type, &first, &drum_count) == 0U)
    {
        return 0U;
    }
    if (slot >= drum_count)
    {
        return 0U;
    }

    *out_param = (param_id_t)((uint16_t)first + (uint16_t)slot);
    return 1U;
}

uint8_t track_runtime_tone_param_to_slot(track_runtime_type_t type,
                                         param_id_t param,
                                         uint8_t *out_slot)
{
    if ((out_slot == NULL) || (param >= PARAM_COUNT))
    {
        return 0U;
    }

    const param_id_t *table = NULL;
    uint8_t count = 0U;
    if (track_runtime_tone_table_for_type(type, &table, &count) == 0U)
    {
        return 0U;
    }

    if (table != NULL)
    {
        for (uint8_t slot = 0U; slot < count; ++slot)
        {
            if (table[slot] == param)
            {
                *out_slot = slot;
                return 1U;
            }
        }
        return 0U;
    }

    param_id_t first = PARAM_COUNT;
    uint8_t drum_count = 0U;
    if (track_runtime_tone_drum_range(type, &first, &drum_count) == 0U)
    {
        return 0U;
    }
    if ((param < first) || (param >= (param_id_t)((uint16_t)first + (uint16_t)drum_count)))
    {
        return 0U;
    }

    *out_slot = (uint8_t)((uint16_t)param - (uint16_t)first);
    return 1U;
}

track_runtime_param_status_t track_runtime_get_effective_param_status(uint8_t track, param_id_t param)
{
    if ((track >= SEQ_LANE_CAPACITY) || (param >= PARAM_COUNT))
    {
        return TRACK_RUNTIME_PARAM_UNAVAILABLE;
    }
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
    {
        return TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == 0)
    {
        return TRACK_RUNTIME_PARAM_UNAVAILABLE;
    }
    const uint8_t active = track_runtime_ctx_is_active(ctx);

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX)
    {
        entity_topology_descriptor_t topology;
        if ((entity_topology_get((brick_entity_id_t)track, &topology) == 0U)
                || (topology.active == 0U))
            return TRACK_RUNTIME_PARAM_UNAVAILABLE;
        const uint8_t group_level = track_runtime_is_group_fx_level_param(param);
        if (topology.role == ENTITY_ROLE_GROUP_CHILD)
            return (group_level != 0U) ? TRACK_RUNTIME_PARAM_ALLOWED
                                      : TRACK_RUNTIME_PARAM_UNAVAILABLE;
        if (group_level != 0U)
            return TRACK_RUNTIME_PARAM_UNAVAILABLE;
        if ((topology.role == ENTITY_ROLE_GROUP_MASTER)
                && ((param == PARAM_AUDIO_FX_FILTER_POS)
                    || (param == PARAM_AUDIO_FX_ORDER)))
            return TRACK_RUNTIME_PARAM_UNAVAILABLE;
    }

    if (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP)
    {
        if (active == 0U)
        {
            return TRACK_RUNTIME_PARAM_UNAVAILABLE;
        }
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
                || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_AUDIO_FX))
        {
            return TRACK_RUNTIME_PARAM_ALLOWED;
        }
        switch (param)
        {
            case PARAM_FILTER_MORPH:
            case PARAM_FILTER_CUTOFF:
            case PARAM_FILTER_RESONANCE:
            case PARAM_FILTER_KEYTRK:
            case PARAM_ENV3_ATTACK:
            case PARAM_ENV3_DECAY:
            case PARAM_ENV3_SUSTAIN:
            case PARAM_ENV3_RELEASE:
            case PARAM_ENV_RETRIG_MOD:
                return TRACK_RUNTIME_PARAM_ALLOWED;
            default:
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
        }
    }

    switch (rule.resource)
    {
        case TRACK_RUNTIME_RESOURCE_NONE:
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_FILTER:
            if (active == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            if (track_runtime_is_audio_routable(track) == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        case TRACK_RUNTIME_RESOURCE_SYNTH:
            if (active == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        case TRACK_RUNTIME_RESOURCE_POLYPHONY:
            if (active == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
            {
                return TRACK_RUNTIME_PARAM_ALLOWED;
            }
            return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        case TRACK_RUNTIME_RESOURCE_PLAY:
        {
            if (((param >= PARAM_FM_DX7_HIDDEN_FIRST) && (param <= PARAM_FM_DX7_HIDDEN_LAST))
                    || ((param >= PARAM_FM_UI_FIRST) && (param <= PARAM_FM_UI_LAST)))
            {
                return (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_FM)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            uint8_t play_index = 0U;
            seq_step_play_field_t play_field = SEQ_STEP_PLAY_FIELD_NOTE;
            if (seq_model_play_resolve_param(param,
                                                  &play_index,
                                                  &play_field) != 0U)
            {
                (void)play_field;
                return (play_index < seq_model_play_capacity((seq_track_id_t)track))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
        }
            if (track_runtime_param_is_looper_only(param) != 0U)
            {
                if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                        || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
                {
                    return TRACK_RUNTIME_PARAM_UNAVAILABLE;
                }
                return TRACK_RUNTIME_PARAM_ALLOWED;
            }
            if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            if (track_runtime_param_is_clip_only(param) != 0U)
            {
                if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                        || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
                {
                    return TRACK_RUNTIME_PARAM_UNAVAILABLE;
                }
            }
            if ((param >= PARAM_MIDI_PROGRAM) && (param <= PARAM_MIDI_CC3_4))
            {
                const uint8_t midi_track = (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI) ? 1U : 0U;
                const uint8_t external_track =
                    ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                     && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL)) ? 1U : 0U;
                if ((midi_track == 0U) && (external_track == 0U))
                {
                    return TRACK_RUNTIME_PARAM_UNAVAILABLE;
                }
            }
            if (param == PARAM_EXTERNAL_INPUT)
            {
                return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                        && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_MIX:
            if (track_runtime_is_audio_routable(track) == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            if ((track_runtime_param_is_vca(param) != 0U)
                    && (track_runtime_supports_vca_gate(ctx) == 0U))
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            return (track_runtime_is_audio_routable(track) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        case TRACK_RUNTIME_RESOURCE_MIDI_FX:
            return (track_runtime_is_ui_ensemble_available(track,
                                                            TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX) != 0U)
                ? TRACK_RUNTIME_PARAM_ALLOWED
                : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        case TRACK_RUNTIME_RESOURCE_AUDIO_FX:
            if (active == 0U)
            {
                return TRACK_RUNTIME_PARAM_UNAVAILABLE;
            }
            return (track_runtime_is_audio_routable(track) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_UNAVAILABLE;

        default:
            return TRACK_RUNTIME_PARAM_UNAVAILABLE;
    }
}
