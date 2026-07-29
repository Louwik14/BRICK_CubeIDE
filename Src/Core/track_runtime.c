#include "Core/track_runtime.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Audio/mixer.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_daisy_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_state.h"
#include "Param/param_registry_backends.h"
#include "UI/ui_track_catalog.h"
#include "stm32h7xx_hal.h"

#define TRACK_RUNTIME_FLAG_CAN_FILTER  (1U << 0)
#define TRACK_RUNTIME_FLAG_CAN_SYNTH   (1U << 1)
#define TRACK_RUNTIME_FLAG_CAN_PLAY    (1U << 2)
#define TRACK_RUNTIME_INSTANCE_NONE    0xFFU
#define TRACK_RUNTIME_MIX_TRACK_NONE   0xFFU
#define TRACK_RUNTIME_DRUM_MAX_INSTANCES SEQ_TRACK_COUNT
#define TRACK_RUNTIME_MIX_TRACK_COUNT SEQ_TRACK_COUNT
#define TRACK_RUNTIME_FIXED_INPUT_MIX_TRACK_COUNT UI_AUDIO_INPUT_PROTO_WIRED_COUNT

SEQ_STATE_D2 static track_runtime_ctx_t g_track_runtime_ctx[SEQ_TRACK_COUNT];
static volatile uint8_t g_track_runtime_global_dirty = 1U;
static volatile uint8_t g_track_runtime_track_dirty[SEQ_TRACK_COUNT];
static uint32_t g_track_runtime_revision = 0U;
static uint32_t g_track_runtime_track_revision[SEQ_TRACK_COUNT];
static track_runtime_synth_usage_t g_track_runtime_synth_usage;
static uint8_t g_track_runtime_logical_track_by_mix_track[MIXER_MAX_TRACKS];
volatile uint32_t g_track_runtime_refresh_all_count;
volatile uint32_t g_track_runtime_refresh_in_irq_count;
volatile uint32_t g_track_runtime_refresh_track_count[SEQ_TRACK_COUNT];
volatile uint32_t g_track_runtime_braids_reset_count[BRICK6_BRAIDS_MAX_INSTANCES];
volatile uint32_t g_track_runtime_stack_reset_count[BRICK6_STACK_MAX_INSTANCES];
volatile uint32_t g_track_runtime_daisy_reset_count[BRICK6_DAISY_MAX_INSTANCES];

typedef struct
{
    uint8_t drum_used;
} track_runtime_allocator_state_t;

static void track_runtime_rebuild_mix_track_reverse_map(void)
{
    for (uint8_t mix_track = 0U; mix_track < MIXER_MAX_TRACKS; ++mix_track)
    {
        g_track_runtime_logical_track_by_mix_track[mix_track] = 0xFFU;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS))
        {
            g_track_runtime_logical_track_by_mix_track[ctx->mix_track_id] = track;
        }
    }
}

static track_runtime_family_t track_runtime_family_from_ui(ui_track_family_t family)
{
    if (family == UI_TRACK_FAMILY_OFF)
    {
        return TRACK_RUNTIME_FAMILY_OFF;
    }

    if (family == UI_TRACK_FAMILY_SYNTH)
    {
        return TRACK_RUNTIME_FAMILY_SYNTH;
    }
    if (family == UI_TRACK_FAMILY_SAMPLER)
    {
        return TRACK_RUNTIME_FAMILY_SAMPLER;
    }
    if (family == UI_TRACK_FAMILY_DRUM)
    {
        return TRACK_RUNTIME_FAMILY_DRUM;
    }
    if (family == UI_TRACK_FAMILY_MASTER)
    {
        return TRACK_RUNTIME_FAMILY_MASTER;
    }
    if (family == UI_TRACK_FAMILY_MIDI)
    {
        return TRACK_RUNTIME_FAMILY_MIDI;
    }

    if (ui_track_catalog_family_is_input(family) != 0)
    {
        return TRACK_RUNTIME_FAMILY_INPUT;
    }

    return TRACK_RUNTIME_FAMILY_OTHER;
}

static track_runtime_type_t track_runtime_type_from_ui(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_AUDIO:
            return TRACK_RUNTIME_TYPE_AUDIO;

        case UI_TRACK_TYPE_HYBRID:
            return TRACK_RUNTIME_TYPE_HYBRID;

        case UI_TRACK_TYPE_RAM:
            return TRACK_RUNTIME_TYPE_RAM;
        case UI_TRACK_TYPE_STREAM:
            return TRACK_RUNTIME_TYPE_STREAM;
        case UI_TRACK_TYPE_PRISM:
            return TRACK_RUNTIME_TYPE_PRISM;
        case UI_TRACK_TYPE_WAVE:
            return TRACK_RUNTIME_TYPE_WAVE;

        case UI_TRACK_TYPE_DRUM_TRX_BD:
            return TRACK_RUNTIME_TYPE_DRUM_TRX_BD;
        case UI_TRACK_TYPE_MIDI:
            return TRACK_RUNTIME_TYPE_MIDI;
        case UI_TRACK_TYPE_MASTER_FX:
            return TRACK_RUNTIME_TYPE_MASTER_FX;
        case UI_TRACK_TYPE_DRUM_BD_ANALOG:
            return TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG;
        case UI_TRACK_TYPE_LOOPER:
            return TRACK_RUNTIME_TYPE_LOOPER;
        case UI_TRACK_TYPE_MULTI:
            return TRACK_RUNTIME_TYPE_MULTI;
        case UI_TRACK_TYPE_STACK:
            return TRACK_RUNTIME_TYPE_STACK;
        case UI_TRACK_TYPE_DAISY:
            return TRACK_RUNTIME_TYPE_DAISY;

        default:
            return TRACK_RUNTIME_TYPE_OTHER;
    }
}

static uint8_t track_runtime_type_is_drum_model(track_runtime_type_t type)
{
    switch (type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_TRX_BD:
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t track_runtime_input_family_mix_track(track_runtime_family_t family,
                                                    ui_track_family_t ui_family,
                                                    uint8_t *out_mix_track)
{
    if ((out_mix_track == NULL) || (family != TRACK_RUNTIME_FAMILY_INPUT))
    {
        return 0U;
    }

    switch (ui_family)
    {
        case UI_TRACK_FAMILY_INPUT1:
            *out_mix_track = 0U;
            return 1U;
#if UI_AUDIO_INPUT_RESOURCE_COUNT > 1U
        case UI_TRACK_FAMILY_INPUT2:
            *out_mix_track = 1U;
            return 1U;
#endif
#if UI_AUDIO_INPUT_RESOURCE_COUNT > 2U
        case UI_TRACK_FAMILY_INPUT3:
            *out_mix_track = 2U;
            return 1U;
#endif
        default:
            return 0U;
    }
}

static uint8_t track_runtime_mix_reserve_track(track_runtime_ctx_t *ctx,
                                               uint8_t preferred_mix_track,
                                               uint8_t *used,
                                               uint8_t used_len)
{
    if ((ctx == NULL) || (used == NULL) || (used_len == 0U))
    {
        return 0U;
    }

    if ((preferred_mix_track < used_len) && (used[preferred_mix_track] == 0U))
    {
        ctx->mix_track_id = preferred_mix_track;
        used[preferred_mix_track] = 1U;
        return 1U;
    }

    for (uint8_t i = 0U; i < used_len; ++i)
    {
        if (used[i] == 0U)
        {
            ctx->mix_track_id = i;
            used[i] = 1U;
            return 1U;
        }
    }

    ctx->mix_track_id = TRACK_RUNTIME_MIX_TRACK_NONE;
    return 0U;
}

static void track_runtime_mark_reserved_input_mix_tracks(uint8_t *used, uint8_t used_len)
{
    if (used == NULL)
    {
        return;
    }

    const uint8_t count = (TRACK_RUNTIME_FIXED_INPUT_MIX_TRACK_COUNT < used_len)
        ? TRACK_RUNTIME_FIXED_INPUT_MIX_TRACK_COUNT
        : used_len;
    for (uint8_t lane = 0U; lane < count; ++lane)
    {
        used[lane] = 1U;
    }
}

static uint8_t track_runtime_mix_try_reserve_exact(track_runtime_ctx_t *ctx,
                                                   uint8_t mix_track,
                                                   uint8_t *used,
                                                   uint8_t used_len)
{
    if ((ctx == NULL) || (used == NULL) || (mix_track >= used_len))
    {
        return 0U;
    }

    if (used[mix_track] != 0U)
    {
        return 0U;
    }

    ctx->mix_track_id = mix_track;
    used[mix_track] = 1U;
    return 1U;
}

static uint8_t track_runtime_compute_flags(track_runtime_family_t family,
                                           track_runtime_type_t type)
{
    uint8_t flags = 0U;

    if ((family == TRACK_RUNTIME_FAMILY_INPUT)
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
            || (((family == TRACK_RUNTIME_FAMILY_INPUT) && (type == TRACK_RUNTIME_TYPE_HYBRID))
                || (type == TRACK_RUNTIME_TYPE_RAM)))
    {
        flags |= TRACK_RUNTIME_FLAG_CAN_PLAY;
    }

    if (family == TRACK_RUNTIME_FAMILY_MIDI)
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
                     || (param == PARAM_VCA_RELEASE));
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
    PARAM_PRISM_TIMBRE,
    PARAM_PRISM_COLOR,
    PARAM_PRISM_MODULATION,
    PARAM_PRISM_EDIT,
    PARAM_PRISM_LEVEL,
    PARAM_PRISM_COARSE,
    PARAM_PRISM_FM,
    PARAM_PRISM_PHASE_RESET,
    PARAM_PRISM_OSC2_TIMBRE,
    PARAM_PRISM_OSC2_COLOR,
    PARAM_PRISM_OSC2_MODULATION,
    PARAM_PRISM_OSC2_EDIT,
    PARAM_PRISM_OSC2_LEVEL,
    PARAM_PRISM_OSC2_COARSE,
    PARAM_PRISM_OSC2_FM,
    PARAM_PRISM_OSC2_PHASE_RESET
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
    PARAM_STACK_OSC1_PARAM3,
    PARAM_STACK_OSC2_MODEL,
    PARAM_STACK_OSC2_TUNE,
    PARAM_STACK_OSC2_TIMBRE,
    PARAM_STACK_OSC2_COLOR,
    PARAM_STACK_OSC2_PARAM3,
    PARAM_STACK_OSC3_MODEL,
    PARAM_STACK_OSC3_TUNE,
    PARAM_STACK_OSC3_TIMBRE,
    PARAM_STACK_OSC3_COLOR,
    PARAM_STACK_OSC3_PARAM3,
    PARAM_STACK_OSC_DETUNE,
    PARAM_STACK_PHASE_RESET
};

static const param_id_t g_track_runtime_tone_slots_wave[] = {
    PARAM_WAVE_OSC1_TABLE,
    PARAM_WAVE_OSC1_POS,
    PARAM_WAVE_OSC1_START,
    PARAM_WAVE_OSC1_END,
    PARAM_WAVE_OSC1_LEVEL,
    PARAM_WAVE_OSC1_TUNE,
    PARAM_WAVE_OSC1_PHASE,
    PARAM_WAVE_OSC1_FLIP,
    PARAM_WAVE_OSC2_TABLE,
    PARAM_WAVE_OSC2_POS,
    PARAM_WAVE_OSC2_START,
    PARAM_WAVE_OSC2_END,
    PARAM_WAVE_OSC2_LEVEL,
    PARAM_WAVE_OSC2_TUNE,
    PARAM_WAVE_OSC2_PHASE,
    PARAM_WAVE_OSC2_FLIP,
    PARAM_WAVE_FRAME_INTERP,
    PARAM_WAVE_SAMPLE_INTERP,
    PARAM_WAVE_POS_UPDATE,
    PARAM_WAVE_POS_SMOOTH
};

static const param_id_t g_track_runtime_tone_slots_daisy[] = {
    PARAM_DAISY_MODEL,
    PARAM_DAISY_PARAM1,
    PARAM_DAISY_PARAM2,
    PARAM_DAISY_PARAM3,
    PARAM_DAISY_PARAM4,
    PARAM_DAISY_PARAM5,
    PARAM_DAISY_PARAM6,
    PARAM_DAISY_PARAM7,
    PARAM_DAISY_PARAM8,
    PARAM_DAISY_PARAM9,
    PARAM_DAISY_PARAM10,
    PARAM_DAISY_PARAM11,
    PARAM_DAISY_PARAM12,
    PARAM_DAISY_PARAM13,
    PARAM_DAISY_PARAM14,
    PARAM_DAISY_PARAM15
};

static const param_id_t g_track_runtime_tone_slots_sampler[] = {
    PARAM_SAMPLER_SAMPLE,
    PARAM_SAMPLER_GAIN,
    PARAM_SAMPLER_START,
    PARAM_SAMPLER_END,
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

static const param_id_t g_track_runtime_tone_slots_hybrid[] = {
    PARAM_HYBRID_GATE,
    PARAM_MIDI_PROGRAM,
    PARAM_MIDI_CC1_1, PARAM_MIDI_CC1_2, PARAM_MIDI_CC1_3, PARAM_MIDI_CC1_4,
    PARAM_MIDI_CC2_1, PARAM_MIDI_CC2_2, PARAM_MIDI_CC2_3, PARAM_MIDI_CC2_4,
    PARAM_MIDI_CC3_1, PARAM_MIDI_CC3_2, PARAM_MIDI_CC3_3, PARAM_MIDI_CC3_4
};

static const param_id_t g_track_runtime_tone_slots_master_fx[] = {
    PARAM_MASTER_FX1_TYPE, PARAM_MASTER_FX1_LEVEL, PARAM_MASTER_FX1_A, PARAM_MASTER_FX1_B,
    PARAM_MASTER_FX2_TYPE, PARAM_MASTER_FX2_LEVEL, PARAM_MASTER_FX2_A, PARAM_MASTER_FX2_B,
    PARAM_MASTER_FX3_TYPE, PARAM_MASTER_FX3_LEVEL, PARAM_MASTER_FX3_A, PARAM_MASTER_FX3_B,
    PARAM_MASTER_FX4_TYPE, PARAM_MASTER_FX4_LEVEL, PARAM_MASTER_FX4_A, PARAM_MASTER_FX4_B
};

static const param_id_t g_track_runtime_tone_slots_drum_bd_analog[] = {
    PARAM_DRUM_TRX_BD_PITCH,
    PARAM_DRUM_TRX_BD_DECAY,
    PARAM_DRUM_TRX_BD_HARMONICS,
    PARAM_DRUM_TRX_BD_PITCH_SWEEP
};

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

        case TRACK_RUNTIME_TYPE_DAISY:
            *out_table = g_track_runtime_tone_slots_daisy;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_daisy) / sizeof(g_track_runtime_tone_slots_daisy[0]));
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

        case TRACK_RUNTIME_TYPE_HYBRID:
            *out_table = g_track_runtime_tone_slots_hybrid;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_hybrid) / sizeof(g_track_runtime_tone_slots_hybrid[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_MASTER_FX:
            *out_table = g_track_runtime_tone_slots_master_fx;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_master_fx) / sizeof(g_track_runtime_tone_slots_master_fx[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            *out_table = g_track_runtime_tone_slots_drum_bd_analog;
            *out_count = (uint8_t)(sizeof(g_track_runtime_tone_slots_drum_bd_analog) / sizeof(g_track_runtime_tone_slots_drum_bd_analog[0]));
            return 1U;

        case TRACK_RUNTIME_TYPE_DRUM_TRX_BD:
            *out_table = NULL;
            *out_count = 8U;
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
        case TRACK_RUNTIME_TYPE_DRUM_TRX_BD:
            *out_first = PARAM_DRUM_TRX_BD_PITCH; *out_count = 8U; return 1U;
        default:
            return 0U;
    }
}

static uint16_t track_runtime_compute_ui_ensemble_mask(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    uint16_t mask = 0U;
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_CFG);
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_KEYBOARD);
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_ARP);
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_SEQ);

    if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
    {
        return mask;
    }

    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_TONE);
    if (!(((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_LOOPER)))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MOD);
    }

    if ((((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_MASTER)
            && ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_MASTER_FX)))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_COLORS);
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIX);
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_VCA);
        return mask;
    }

    if (((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) != 0U)
            && (track_state_get_voice_group_role(ctx->track_id) != TRACK_VOICE_GROUP_ROLE_SLAVE))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_PLAY);
    }

    if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_COLORS);
    }

    if (track_runtime_is_audio_routable(ctx->track_id) != 0U)
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIX);
    }

    if ((((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SYNTH)
            || (((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_SAMPLER)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_STREAM)
                && ((track_runtime_type_t)ctx->type != TRACK_RUNTIME_TYPE_LOOPER))
            || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_DRUM))
            || (((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_INPUT)
                && ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_HYBRID)))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_VCA);
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

    switch ((track_runtime_engine_t)ctx->engine)
    {
        case TRACK_RUNTIME_ENGINE_SAMPLER:
        case TRACK_RUNTIME_ENGINE_LOOPER:
        case TRACK_RUNTIME_ENGINE_PRISM:
        case TRACK_RUNTIME_ENGINE_STACK:
        case TRACK_RUNTIME_ENGINE_WAVE:
        case TRACK_RUNTIME_ENGINE_DAISY:
        case TRACK_RUNTIME_ENGINE_NONE:
        case TRACK_RUNTIME_ENGINE_AUDIO_TRACK:
        case TRACK_RUNTIME_ENGINE_DRUM:
        default:
            return TRACK_RUNTIME_VOICE_MODE_MONO;
    }
}

uint8_t track_runtime_get_play_voice_count(const track_runtime_ctx_t *ctx)
{
    return (track_runtime_get_voice_mode(ctx) == TRACK_RUNTIME_VOICE_MODE_POLY) ? 4U : 1U;
}

uint8_t track_runtime_get_play_voice_count_from_descriptor(const track_runtime_descriptor_t *descriptor)
{
    if (descriptor == NULL)
    {
        return 1U;
    }

    if ((descriptor->family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor->type == TRACK_RUNTIME_TYPE_MULTI))
    {
        return 4U;
    }

    switch ((track_runtime_engine_t)descriptor->engine)
    {
        case TRACK_RUNTIME_ENGINE_NONE:
        case TRACK_RUNTIME_ENGINE_AUDIO_TRACK:
        case TRACK_RUNTIME_ENGINE_SAMPLER:
        case TRACK_RUNTIME_ENGINE_LOOPER:
        case TRACK_RUNTIME_ENGINE_PRISM:
        case TRACK_RUNTIME_ENGINE_STACK:
        case TRACK_RUNTIME_ENGINE_WAVE:
        case TRACK_RUNTIME_ENGINE_DAISY:
        case TRACK_RUNTIME_ENGINE_DRUM:
        default:
            return 1U;
    }
}

uint8_t track_runtime_supports_vca_gate(const track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (track_runtime_ctx_is_sampler_clip_or_looper(ctx) != 0U)
    {
        return 0U;
    }

    if ((ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        return 1U;
    }

    return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
}

static uint8_t track_runtime_param_play_voice_index(param_id_t param)
{
    switch (param)
    {
        case PARAM_SEQ_PLAY_V1_NOTE:
        case PARAM_SEQ_PLAY_V1_VEL:
        case PARAM_SEQ_PLAY_V1_LEN:
        case PARAM_SEQ_PLAY_V1_MICTIM:
            return 0U;

        case PARAM_SEQ_PLAY_V2_NOTE:
        case PARAM_SEQ_PLAY_V2_VEL:
        case PARAM_SEQ_PLAY_V2_LEN:
        case PARAM_SEQ_PLAY_V2_MICTIM:
            return 1U;

        case PARAM_SEQ_PLAY_V3_NOTE:
        case PARAM_SEQ_PLAY_V3_VEL:
        case PARAM_SEQ_PLAY_V3_LEN:
        case PARAM_SEQ_PLAY_V3_MICTIM:
            return 2U;

        case PARAM_SEQ_PLAY_V4_NOTE:
        case PARAM_SEQ_PLAY_V4_VEL:
        case PARAM_SEQ_PLAY_V4_LEN:
        case PARAM_SEQ_PLAY_V4_MICTIM:
            return 3U;

        default:
            return 0xFFU;
    }
}

static void track_runtime_set_unbound(track_runtime_ctx_t *ctx, track_runtime_bind_reason_t reason)
{
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
    ctx->bind_reason = reason;
}

static void track_runtime_set_quota_blocked(track_runtime_ctx_t *ctx)
{
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_QUOTA_BLOCKED;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_QUOTA_EXCEEDED;
}

static void track_runtime_set_bound(track_runtime_ctx_t *ctx,
                                    track_runtime_engine_t engine,
                                    uint8_t instance_id)
{
    ctx->engine = (uint8_t)engine;
    ctx->instance_id = instance_id;
    ctx->bind_state = TRACK_RUNTIME_BIND_BOUND;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
}

static void track_runtime_bind_ctx(track_runtime_ctx_t *ctx,
                                   track_runtime_allocator_state_t *allocator)
{
    const track_runtime_family_t family = (track_runtime_family_t)ctx->family;
    const track_runtime_type_t type = (track_runtime_type_t)ctx->type;

    if (family == TRACK_RUNTIME_FAMILY_OFF)
    {
        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_TRACK_OFF);
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_INPUT)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_AUDIO_TRACK, ctx->track_id);
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_NONE, TRACK_RUNTIME_INSTANCE_NONE);
        return;
    }

    if ((family != TRACK_RUNTIME_FAMILY_SYNTH)
            && (family != TRACK_RUNTIME_FAMILY_SAMPLER)
            && (family != TRACK_RUNTIME_FAMILY_DRUM)
            && (family != TRACK_RUNTIME_FAMILY_MASTER))
    {
        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_DRUM)
    {
        if (track_runtime_type_is_drum_model(type) == 0U)
        {
            track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
            return;
        }

        if (ctx->track_id >= TRACK_RUNTIME_DRUM_MAX_INSTANCES)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }

        /*
         * Keep drum instance ownership stable per logical track.
         * This prevents cross-track state migration when drum track cardinality changes.
         */
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_DRUM, ctx->track_id);
        allocator->drum_used++;
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_MASTER)
    {
        if (type == TRACK_RUNTIME_TYPE_MASTER_FX)
        {
            track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_NONE, TRACK_RUNTIME_INSTANCE_NONE);
            return;
        }

        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
        return;
    }

    if ((type == TRACK_RUNTIME_TYPE_RAM)
            || (type == TRACK_RUNTIME_TYPE_STREAM)
            || (type == TRACK_RUNTIME_TYPE_MULTI))
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_SAMPLER, ctx->track_id);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_LOOPER)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_LOOPER, ctx->track_id);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_PRISM)
    {
        if (ctx->track_id >= BRICK6_BRAIDS_MAX_INSTANCES)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }

        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_PRISM, ctx->track_id);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_STACK)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_STACK, ctx->track_id);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_WAVE)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_WAVE, ctx->track_id);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_DAISY)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_DAISY, ctx->track_id);
        return;
    }

    track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
}

static void track_runtime_prepare_ctx_base(uint8_t track, track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return;
    }

    const ui_track_config_t config = track_state_get_config(track);
    const ui_track_family_t ui_family = config.family;
    const ui_track_type_t ui_type = config.type;
    const track_runtime_family_t family = track_runtime_family_from_ui(ui_family);
    const track_runtime_type_t type = track_runtime_type_from_ui(ui_type);

    memset(ctx, 0, sizeof(*ctx));
    ctx->track_id = track;
    ctx->mix_track_id = TRACK_RUNTIME_MIX_TRACK_NONE;
    ctx->midi_channel_1_16 = track_state_get_midi_channel(track);
    ctx->midi_source = (uint8_t)track_state_get_midi_source(track);
    ctx->family = (uint8_t)family;
    ctx->type = (uint8_t)type;
    ctx->flags = track_runtime_compute_flags(family, type);
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;

    uint8_t input_mix_track = TRACK_RUNTIME_MIX_TRACK_NONE;
    if (track_runtime_input_family_mix_track(family, ui_family, &input_mix_track) != 0U)
    {
        if (input_mix_track < TRACK_RUNTIME_MIX_TRACK_COUNT)
        {
            ctx->mix_track_id = input_mix_track;
        }
    }
}

static void track_runtime_mark_used_mix_tracks_except(uint8_t except_track,
                                                      uint8_t *mix_track_used,
                                                      uint8_t used_len)
{
    if (mix_track_used == NULL)
    {
        return;
    }

    memset(mix_track_used, 0, used_len);
    track_runtime_mark_reserved_input_mix_tracks(mix_track_used, used_len);
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (track == except_track)
        {
            continue;
        }

        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (ctx->mix_track_id < used_len)
        {
            mix_track_used[ctx->mix_track_id] = 1U;
        }
    }
}

static void track_runtime_recompute_synth_usage(void)
{
    uint8_t drum_count = 0U;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM))
        {
            drum_count++;
        }
    }

    g_track_runtime_synth_usage.drum_tracks = drum_count;
}

static void track_runtime_reset_prism_if_owner_changed(uint8_t previous_engine,
                                                      uint8_t previous_instance,
                                                      const track_runtime_ctx_t *current_ctx)
{
    const uint8_t current_is_prism = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            && (current_ctx->instance_id < BRICK6_BRAIDS_MAX_INSTANCES)) ? 1U : 0U;
    const uint8_t previous_is_prism = ((previous_engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            && (previous_instance < BRICK6_BRAIDS_MAX_INSTANCES)) ? 1U : 0U;

    if ((previous_is_prism != 0U)
            && ((current_is_prism == 0U) || (current_ctx->instance_id != previous_instance)))
    {
        brick6_braids_runtime_reset_instance(previous_instance);
        g_track_runtime_braids_reset_count[previous_instance]++;
    }

    if ((current_is_prism != 0U)
            && ((previous_is_prism == 0U) || (previous_instance != current_ctx->instance_id)))
    {
        brick6_braids_runtime_reset_instance(current_ctx->instance_id);
        g_track_runtime_braids_reset_count[current_ctx->instance_id]++;
        (void)param_backend_reapply_tone_prism_runtime(current_ctx->track_id);
    }
}
static void track_runtime_reset_stack_if_owner_changed(uint8_t previous_engine,
                                                       uint8_t previous_instance,
                                                       const track_runtime_ctx_t *current_ctx)
{
    const uint8_t current_is_stack = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            && (current_ctx->instance_id < BRICK6_STACK_MAX_INSTANCES)) ? 1U : 0U;
    const uint8_t previous_is_stack = ((previous_engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            && (previous_instance < BRICK6_STACK_MAX_INSTANCES)) ? 1U : 0U;

    if ((previous_is_stack != 0U)
            && ((current_is_stack == 0U) || (current_ctx->instance_id != previous_instance)))
    {
        (void)brick6_stack_runtime_submit_reset_instance(previous_instance);
        g_track_runtime_stack_reset_count[previous_instance]++;
    }

    if ((current_is_stack != 0U)
            && ((previous_is_stack == 0U) || (previous_instance != current_ctx->instance_id)))
    {
        (void)brick6_stack_runtime_submit_reset_instance(current_ctx->instance_id);
        g_track_runtime_stack_reset_count[current_ctx->instance_id]++;
        (void)param_backend_reapply_tone_stack_runtime(current_ctx->track_id);
    }
}

static void track_runtime_reset_wave_if_owner_changed(uint8_t previous_engine,
                                                      uint8_t previous_instance,
                                                      const track_runtime_ctx_t *current_ctx)
{
    const uint8_t current_is_wave = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            && (current_ctx->instance_id < BRICK6_WAVE_MAX_INSTANCES)) ? 1U : 0U;
    const uint8_t previous_is_wave = ((previous_engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            && (previous_instance < BRICK6_WAVE_MAX_INSTANCES)) ? 1U : 0U;

    if ((previous_is_wave != 0U)
            && ((current_is_wave == 0U) || (current_ctx->instance_id != previous_instance)))
    {
        brick6_wave_runtime_reset_instance(previous_instance);
    }

    if ((current_is_wave != 0U)
            && ((previous_is_wave == 0U) || (previous_instance != current_ctx->instance_id)))
    {
        brick6_wave_runtime_reset_instance(current_ctx->instance_id);
        (void)param_backend_reapply_tone_wave_runtime(current_ctx->track_id);
    }
}

static void track_runtime_reset_daisy_if_owner_changed(uint8_t previous_engine,
                                                       uint8_t previous_instance,
                                                       const track_runtime_ctx_t *current_ctx)
{
    const uint8_t current_is_daisy = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DAISY)
            && (current_ctx->instance_id < BRICK6_DAISY_MAX_INSTANCES)) ? 1U : 0U;
    const uint8_t previous_is_daisy = ((previous_engine == (uint8_t)TRACK_RUNTIME_ENGINE_DAISY)
            && (previous_instance < BRICK6_DAISY_MAX_INSTANCES)) ? 1U : 0U;

    if ((previous_is_daisy != 0U)
            && ((current_is_daisy == 0U) || (current_ctx->instance_id != previous_instance)))
    {
        brick6_daisy_runtime_reset_instance(previous_instance);
        g_track_runtime_daisy_reset_count[previous_instance]++;
    }

    if ((current_is_daisy != 0U)
            && ((previous_is_daisy == 0U) || (previous_instance != current_ctx->instance_id)))
    {
        brick6_daisy_runtime_reset_instance(current_ctx->instance_id);
        g_track_runtime_daisy_reset_count[current_ctx->instance_id]++;
        (void)param_backend_reapply_tone_daisy_runtime(current_ctx->track_id);
    }
}

void track_runtime_init(void)
{
    memset(&g_track_runtime_ctx, 0, sizeof(g_track_runtime_ctx));
    track_runtime_rebuild_mix_track_reverse_map();
    memset((void *)g_track_runtime_track_dirty, 1, sizeof(g_track_runtime_track_dirty));
    g_track_runtime_global_dirty = 1U;
    track_runtime_refresh_all();
}

void track_runtime_invalidate_all(void)
{
    g_track_runtime_global_dirty = 1U;
    memset((void *)g_track_runtime_track_dirty, 1, sizeof(g_track_runtime_track_dirty));
}

void track_runtime_invalidate_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    g_track_runtime_track_dirty[track] = 1U;
}

uint8_t track_runtime_refresh_if_dirty(void)
{
    const uint8_t in_irq = (__get_IPSR() != 0U) ? 1U : 0U;

    if (g_track_runtime_global_dirty != 0U)
    {
        if (in_irq != 0U)
        {
            g_track_runtime_refresh_in_irq_count++;
            return 0U;
        }
        track_runtime_refresh_all();
        return 1U;
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if (g_track_runtime_track_dirty[track] != 0U)
        {
            if (in_irq != 0U)
            {
                g_track_runtime_refresh_in_irq_count++;
                return 0U;
            }
            track_runtime_refresh_track(track);
            return 1U;
        }
    }

    return 0U;
}

void track_runtime_refresh_all(void)
{
    track_runtime_allocator_state_t allocator = { 0U };
    uint8_t mix_track_used[TRACK_RUNTIME_MIX_TRACK_COUNT];
    uint8_t previous_mix_track[SEQ_TRACK_COUNT];
    uint8_t drum_count = 0U;
    uint8_t previous_prism_owner[BRICK6_BRAIDS_MAX_INSTANCES];
    uint8_t current_prism_owner[BRICK6_BRAIDS_MAX_INSTANCES];
    uint8_t previous_stack_owner[BRICK6_STACK_MAX_INSTANCES];
    uint8_t current_stack_owner[BRICK6_STACK_MAX_INSTANCES];
    uint8_t previous_wave_owner[BRICK6_WAVE_MAX_INSTANCES];
    uint8_t current_wave_owner[BRICK6_WAVE_MAX_INSTANCES];
    uint8_t previous_daisy_owner[BRICK6_DAISY_MAX_INSTANCES];
    uint8_t current_daisy_owner[BRICK6_DAISY_MAX_INSTANCES];

    g_track_runtime_refresh_all_count++;
    memset(mix_track_used, 0, sizeof(mix_track_used));
    track_runtime_mark_reserved_input_mix_tracks(mix_track_used, (uint8_t)sizeof(mix_track_used));
    for (uint8_t instance = 0U; instance < BRICK6_BRAIDS_MAX_INSTANCES; ++instance)
    {
        previous_prism_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
        current_prism_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
    }
    for (uint8_t instance = 0U; instance < BRICK6_STACK_MAX_INSTANCES; ++instance)
    {
        previous_stack_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
        current_stack_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
    }
    for (uint8_t instance = 0U; instance < BRICK6_WAVE_MAX_INSTANCES; ++instance)
    {
        previous_wave_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
        current_wave_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
    }
    for (uint8_t instance = 0U; instance < BRICK6_DAISY_MAX_INSTANCES; ++instance)
    {
        previous_daisy_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
        current_daisy_owner[instance] = TRACK_RUNTIME_INSTANCE_NONE;
    }
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        previous_mix_track[track] = g_track_runtime_ctx[track].mix_track_id;
        if ((g_track_runtime_ctx[track].bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (g_track_runtime_ctx[track].engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
                && (g_track_runtime_ctx[track].instance_id < BRICK6_BRAIDS_MAX_INSTANCES))
        {
            previous_prism_owner[g_track_runtime_ctx[track].instance_id] = track;
        }
        else if ((g_track_runtime_ctx[track].bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (g_track_runtime_ctx[track].engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
                && (g_track_runtime_ctx[track].instance_id < BRICK6_STACK_MAX_INSTANCES))
        {
            previous_stack_owner[g_track_runtime_ctx[track].instance_id] = track;
        }
        else if ((g_track_runtime_ctx[track].bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (g_track_runtime_ctx[track].engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
                && (g_track_runtime_ctx[track].instance_id < BRICK6_WAVE_MAX_INSTANCES))
        {
            previous_wave_owner[g_track_runtime_ctx[track].instance_id] = track;
        }
        else if ((g_track_runtime_ctx[track].bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (g_track_runtime_ctx[track].engine == (uint8_t)TRACK_RUNTIME_ENGINE_DAISY)
                && (g_track_runtime_ctx[track].instance_id < BRICK6_DAISY_MAX_INSTANCES))
        {
            previous_daisy_owner[g_track_runtime_ctx[track].instance_id] = track;
        }
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        track_runtime_prepare_ctx_base(track, ctx);
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (ctx->mix_track_id < TRACK_RUNTIME_MIX_TRACK_COUNT)
        {
            mix_track_used[ctx->mix_track_id] = 1U;
        }
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_MIDI)
                || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_OFF)
                || (ctx->mix_track_id != TRACK_RUNTIME_MIX_TRACK_NONE))
        {
            continue;
        }

        (void)track_runtime_mix_try_reserve_exact(ctx,
                                                  previous_mix_track[track],
                                                  mix_track_used,
                                                  (uint8_t)sizeof(mix_track_used));
    }

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_MIDI)
                || ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_OFF))
        {
            ctx->mix_track_id = TRACK_RUNTIME_MIX_TRACK_NONE;
        }
        else if (ctx->mix_track_id == TRACK_RUNTIME_MIX_TRACK_NONE)
        {
            (void)track_runtime_mix_reserve_track(ctx,
                                                  track,
                                                  mix_track_used,
                                                  (uint8_t)sizeof(mix_track_used));
        }

        track_runtime_bind_ctx(ctx, &allocator);

        if (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
        {
            if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
            {
                drum_count++;
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            {
                if (ctx->instance_id < BRICK6_BRAIDS_MAX_INSTANCES)
                {
                    current_prism_owner[ctx->instance_id] = track;
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            {
                if (ctx->instance_id < BRICK6_STACK_MAX_INSTANCES)
                {
                    current_stack_owner[ctx->instance_id] = track;
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            {
                if (ctx->instance_id < BRICK6_WAVE_MAX_INSTANCES)
                {
                    current_wave_owner[ctx->instance_id] = track;
                }
            }
            else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DAISY)
            {
                if (ctx->instance_id < BRICK6_DAISY_MAX_INSTANCES)
                {
                    current_daisy_owner[ctx->instance_id] = track;
                }
            }
        }
    }

    for (uint8_t instance = 0U; instance < BRICK6_BRAIDS_MAX_INSTANCES; ++instance)
    {
        if (previous_prism_owner[instance] != current_prism_owner[instance])
        {
            brick6_braids_runtime_reset_instance(instance);
            g_track_runtime_braids_reset_count[instance]++;
            if (current_prism_owner[instance] < SEQ_TRACK_COUNT)
            {
                (void)param_backend_reapply_tone_prism_runtime(current_prism_owner[instance]);
            }
        }
    }

    for (uint8_t instance = 0U; instance < BRICK6_STACK_MAX_INSTANCES; ++instance)
    {
        if (previous_stack_owner[instance] != current_stack_owner[instance])
        {
            (void)brick6_stack_runtime_submit_reset_instance(instance);
            g_track_runtime_stack_reset_count[instance]++;
            if (current_stack_owner[instance] < SEQ_TRACK_COUNT)
            {
                (void)param_backend_reapply_tone_stack_runtime(current_stack_owner[instance]);
            }
        }
    }

    for (uint8_t instance = 0U; instance < BRICK6_WAVE_MAX_INSTANCES; ++instance)
    {
        if (previous_wave_owner[instance] != current_wave_owner[instance])
        {
            brick6_wave_runtime_reset_instance(instance);
            if (current_wave_owner[instance] < SEQ_TRACK_COUNT)
            {
                (void)param_backend_reapply_tone_wave_runtime(current_wave_owner[instance]);
            }
        }
    }

    for (uint8_t instance = 0U; instance < BRICK6_DAISY_MAX_INSTANCES; ++instance)
    {
        if (previous_daisy_owner[instance] != current_daisy_owner[instance])
        {
            brick6_daisy_runtime_reset_instance(instance);
            g_track_runtime_daisy_reset_count[instance]++;
            if (current_daisy_owner[instance] < SEQ_TRACK_COUNT)
            {
                (void)param_backend_reapply_tone_daisy_runtime(current_daisy_owner[instance]);
            }
        }
    }

    g_track_runtime_synth_usage.drum_tracks = drum_count;
    track_runtime_rebuild_mix_track_reverse_map();
    g_track_runtime_global_dirty = 0U;
    ++g_track_runtime_revision;
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        g_track_runtime_track_dirty[track] = 0U;
        g_track_runtime_track_revision[track] = g_track_runtime_revision;
    }
}

uint8_t track_runtime_is_track_prism_available(uint8_t track)
{
    uint8_t used = 0U;
    for (uint8_t other_track = 0U; other_track < SEQ_TRACK_COUNT; ++other_track)
    {
        if (other_track == track)
        {
            continue;
        }

        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[other_track];
        if ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
        {
            ++used;
        }
    }

    return (used < BRICK6_BRAIDS_MAX_INSTANCES) ? 1U : 0U;
}

void track_runtime_refresh_track(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    if (g_track_runtime_global_dirty != 0U)
    {
        if (__get_IPSR() != 0U)
        {
            g_track_runtime_refresh_in_irq_count++;
            return;
        }
        track_runtime_refresh_all();
        return;
    }

    if (g_track_runtime_track_dirty[track] != 0U)
    {
        track_runtime_allocator_state_t allocator = { 0U };
        uint8_t mix_track_used[TRACK_RUNTIME_MIX_TRACK_COUNT];
        const uint8_t previous_engine = g_track_runtime_ctx[track].engine;
        const uint8_t previous_instance = g_track_runtime_ctx[track].instance_id;
        const uint8_t previous_mix_track = g_track_runtime_ctx[track].mix_track_id;
        track_runtime_ctx_t next_ctx;

        g_track_runtime_refresh_track_count[track]++;
        track_runtime_prepare_ctx_base(track, &next_ctx);
        track_runtime_mark_used_mix_tracks_except(track,
                                                  mix_track_used,
                                                  (uint8_t)sizeof(mix_track_used));

        if (((track_runtime_family_t)next_ctx.family != TRACK_RUNTIME_FAMILY_MIDI)
                && ((track_runtime_family_t)next_ctx.family != TRACK_RUNTIME_FAMILY_OFF)
                && (next_ctx.mix_track_id == TRACK_RUNTIME_MIX_TRACK_NONE))
        {
            if (track_runtime_mix_try_reserve_exact(&next_ctx,
                                                    previous_mix_track,
                                                    mix_track_used,
                                                    (uint8_t)sizeof(mix_track_used)) == 0U)
            {
                (void)track_runtime_mix_reserve_track(&next_ctx,
                                                      track,
                                                      mix_track_used,
                                                      (uint8_t)sizeof(mix_track_used));
            }
        }

        track_runtime_bind_ctx(&next_ctx, &allocator);
        g_track_runtime_ctx[track] = next_ctx;
        track_runtime_reset_prism_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_reset_stack_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_reset_wave_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_reset_daisy_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_recompute_synth_usage();
        track_runtime_rebuild_mix_track_reverse_map();
        g_track_runtime_track_dirty[track] = 0U;
        ++g_track_runtime_revision;
        g_track_runtime_track_revision[track] = g_track_runtime_revision;
    }
}

void track_runtime_get_cached_synth_usage(track_runtime_synth_usage_t *out_usage)
{
    if (out_usage != NULL)
    {
        *out_usage = g_track_runtime_synth_usage;
    }
}

uint32_t track_runtime_get_revision(void)
{
    return g_track_runtime_revision;
}

uint32_t track_runtime_get_track_revision(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0U;
    }

    return g_track_runtime_track_revision[track];
}

const track_runtime_ctx_t *track_runtime_get_ctx(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return 0;
    }

    return &g_track_runtime_ctx[track];
}

uint8_t track_runtime_is_audio_routable(uint8_t track)
{
    /* Logical track can exist without any physical mixer lane. */
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    return (ctx->mix_track_id < MIXER_MAX_TRACKS) ? 1U : 0U;
}

uint8_t track_runtime_get_mix_target_track(uint8_t track, uint8_t *out_mix_track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->mix_track_id == TRACK_RUNTIME_MIX_TRACK_NONE))
    {
        return 0U;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return 0U;
    }

    if (out_mix_track != NULL)
    {
        *out_mix_track = ctx->mix_track_id;
    }

    return 1U;
}

uint8_t track_runtime_get_logical_track_for_mix_track(uint8_t mix_track, uint8_t *out_track)
{
    if ((out_track == NULL) || (mix_track >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    const uint8_t track = g_track_runtime_logical_track_by_mix_track[mix_track];
    if (track < SEQ_TRACK_COUNT)
    {
        *out_track = track;
        return 1U;
    }

    return 0U;
}

uint8_t track_runtime_resolve_filter_target_track(uint8_t ui_track, uint8_t *out_filter_track)
{
    if ((out_filter_track == NULL) || (ui_track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(ui_track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
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

    switch ((ui_track_midi_source_t)ctx->midi_source)
    {
        case UI_TRACK_MIDI_SRC_INT:
            return TRACK_RUNTIME_MIDI_SOURCE_INTERNAL;
        case UI_TRACK_MIDI_SRC_EXT:
            return TRACK_RUNTIME_MIDI_SOURCE_EXTERNAL;
        case UI_TRACK_MIDI_SRC_ALL:
        default:
            return TRACK_RUNTIME_MIDI_SOURCE_ALL;
    }
}

uint8_t track_runtime_get_descriptor(uint8_t track, track_runtime_descriptor_t *out_descriptor)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_descriptor == NULL))
    {
        return 0U;
    }
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    out_descriptor->family = (track_runtime_family_t)ctx->family;
    out_descriptor->type = (track_runtime_type_t)ctx->type;
    out_descriptor->engine = (track_runtime_engine_t)ctx->engine;
    out_descriptor->bind_state = ctx->bind_state;
    out_descriptor->bind_reason = ctx->bind_reason;
    out_descriptor->instance_id = ctx->instance_id;
    out_descriptor->mix_track_id = ctx->mix_track_id;
    out_descriptor->flags = ctx->flags;
    out_descriptor->midi_channel_1_16 = track_runtime_get_midi_channel_1_16(track);
    out_descriptor->ui_ensemble_mask = track_runtime_compute_ui_ensemble_mask(ctx);
    return 1U;
}

uint8_t track_runtime_resolve_track(uint8_t track, track_runtime_resolved_track_t *out_resolved)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_resolved == NULL))
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

    switch (param)
    {
        case PARAM_FILTER_TYPE:
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
        case PARAM_FILTER_EQ_LOW:
        case PARAM_FILTER_EQ_MID:
        case PARAM_FILTER_EQ_HIGH:
        case PARAM_ENV_RETRIG_FILTER:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_COLORS;
            rule.resource = TRACK_RUNTIME_RESOURCE_FILTER;
            return rule;
        case PARAM_DRUM_TRX_BD_PITCH:
        case PARAM_DRUM_TRX_BD_DECAY:
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
        case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
        case PARAM_DRUM_TRX_BD_ATTACK:
        case PARAM_DRUM_TRX_BD_NOISE:
        case PARAM_DRUM_TRX_BD_HARMONICS:
        case PARAM_DRUM_TRX_BD_DRIVE:
        case PARAM_PRISM_EDIT:
        case PARAM_PRISM_FINE:
        case PARAM_PRISM_COARSE:
        case PARAM_PRISM_FM:
        case PARAM_PRISM_TIMBRE:
        case PARAM_PRISM_MODULATION:
        case PARAM_PRISM_COLOR:
        case PARAM_PRISM_PHASE_RESET:
        case PARAM_PRISM_LEVEL:
        case PARAM_PRISM_OSC2_EDIT:
        case PARAM_PRISM_OSC2_FINE:
        case PARAM_PRISM_OSC2_COARSE:
        case PARAM_PRISM_OSC2_FM:
        case PARAM_PRISM_OSC2_TIMBRE:
        case PARAM_PRISM_OSC2_MODULATION:
        case PARAM_PRISM_OSC2_COLOR:
        case PARAM_PRISM_OSC2_PHASE_RESET:
        case PARAM_PRISM_OSC2_LEVEL:
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
        case PARAM_STACK_NOISE_LEVEL:
        case PARAM_STACK_OSC1_MODEL:
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC1_PARAM3:
        case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC2_PARAM3:
        case PARAM_STACK_OSC3_MODEL:
        case PARAM_STACK_OSC3_TUNE:
        case PARAM_STACK_OSC3_TIMBRE:
        case PARAM_STACK_OSC3_COLOR:
        case PARAM_STACK_OSC3_PARAM3:
        case PARAM_STACK_OSC_DETUNE:
        case PARAM_STACK_PHASE_RESET:
        case PARAM_WAVE_OSC1_TABLE:
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC1_END:
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC1_PHASE:
        case PARAM_WAVE_OSC1_FLIP:
        case PARAM_WAVE_OSC2_TABLE:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC2_START:
        case PARAM_WAVE_OSC2_END:
        case PARAM_WAVE_OSC2_LEVEL:
        case PARAM_WAVE_OSC2_TUNE:
        case PARAM_WAVE_OSC2_PHASE:
        case PARAM_WAVE_OSC2_FLIP:
        case PARAM_WAVE_FRAME_INTERP:
        case PARAM_WAVE_SAMPLE_INTERP:
        case PARAM_WAVE_POS_UPDATE:
        case PARAM_WAVE_POS_SMOOTH:
        case PARAM_DAISY_MODEL:
        case PARAM_DAISY_PARAM1:
        case PARAM_DAISY_PARAM2:
        case PARAM_DAISY_PARAM3:
        case PARAM_DAISY_PARAM4:
        case PARAM_DAISY_PARAM5:
        case PARAM_DAISY_PARAM6:
        case PARAM_DAISY_PARAM7:
        case PARAM_DAISY_PARAM8:
        case PARAM_DAISY_PARAM9:
        case PARAM_DAISY_PARAM10:
        case PARAM_DAISY_PARAM11:
        case PARAM_DAISY_PARAM12:
        case PARAM_DAISY_PARAM13:
        case PARAM_DAISY_PARAM14:
        case PARAM_DAISY_PARAM15:
        case PARAM_MASTER_FX1_TYPE:
        case PARAM_MASTER_FX1_LEVEL:
        case PARAM_MASTER_FX1_A:
        case PARAM_MASTER_FX1_B:
        case PARAM_MASTER_FX2_TYPE:
        case PARAM_MASTER_FX2_LEVEL:
        case PARAM_MASTER_FX2_A:
        case PARAM_MASTER_FX2_B:
        case PARAM_MASTER_FX3_TYPE:
        case PARAM_MASTER_FX3_LEVEL:
        case PARAM_MASTER_FX3_A:
        case PARAM_MASTER_FX3_B:
        case PARAM_MASTER_FX4_TYPE:
        case PARAM_MASTER_FX4_LEVEL:
        case PARAM_MASTER_FX4_A:
        case PARAM_MASTER_FX4_B:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_TONE;
            rule.resource = TRACK_RUNTIME_RESOURCE_SYNTH;
            return rule;

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
        case PARAM_SAMPLER_SAMPLE:
        case PARAM_SAMPLER_GAIN:
        case PARAM_SAMPLER_START:
        case PARAM_SAMPLER_END:
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
        case PARAM_MIX_MUTE:
        case PARAM_HYBRID_GATE:
        case PARAM_VCA_ATTACK:
        case PARAM_VCA_DECAY:
        case PARAM_VCA_SUSTAIN:
        case PARAM_VCA_RELEASE:
        case PARAM_ENV_RETRIG_VCA:
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
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_PLAY;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
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
        case PARAM_ENV3_ATTACK:
        case PARAM_ENV3_DECAY:
        case PARAM_ENV3_SUSTAIN:
        case PARAM_ENV3_RELEASE:
        case PARAM_ENV_RETRIG_MOD:
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_MOD;
            rule.resource = TRACK_RUNTIME_RESOURCE_PLAY;
            return rule;

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
    if ((track >= SEQ_TRACK_COUNT) || (param >= PARAM_COUNT))
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(param);
    if (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
    {
        return TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == 0)
    {
        return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }

    switch (rule.resource)
    {
        case TRACK_RUNTIME_RESOURCE_NONE:
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_FILTER:
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_SYNTH:
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MASTER_FX)
                    && (param >= PARAM_MASTER_FX1_TYPE)
                    && (param <= PARAM_MASTER_FX4_B))
            {
                return TRACK_RUNTIME_PARAM_ALLOWED;
            }
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_PLAY:
            if (track_runtime_param_is_looper_only(param) != 0U)
            {
                if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                        || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
                {
                    return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
                }
                return TRACK_RUNTIME_PARAM_ALLOWED;
            }
            if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) == 0U)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (track_runtime_param_is_clip_only(param) != 0U)
            {
                if ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                        || (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
                {
                    return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
                }
            }
            if ((param >= PARAM_MIDI_PROGRAM) && (param <= PARAM_MIDI_CC3_4))
            {
                const uint8_t midi_track = (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MIDI) ? 1U : 0U;
                const uint8_t hybrid_input_track =
                    ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                     && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID)) ? 1U : 0U;
                if ((midi_track == 0U) && (hybrid_input_track == 0U))
                {
                    return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
                }
            }
            {
                const uint8_t voice = track_runtime_param_play_voice_index(param);
                if ((voice != 0xFFU) && (voice >= track_runtime_get_play_voice_count(ctx)))
                {
                    return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
                }
            }
            return TRACK_RUNTIME_PARAM_ALLOWED;

        case TRACK_RUNTIME_RESOURCE_MIX:
            if ((ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
                    || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_MASTER))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if ((track_runtime_param_is_vca(param) != 0U)
                    && (track_runtime_ctx_is_sampler_clip_or_looper(ctx) != 0U))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return (track_runtime_is_audio_routable(track) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}

uint8_t track_runtime_get_voice_group_role(uint8_t track, uint8_t *out_role)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_role == NULL))
    {
        return 0U;
    }

    *out_role = (uint8_t)track_state_get_voice_group_role(track);
    return 1U;
}

uint8_t track_runtime_get_voice_group_effective_master(uint8_t track, uint8_t *out_master_track)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_master_track == NULL))
    {
        return 0U;
    }

    const track_voice_group_role_t role = track_state_get_voice_group_role(track);
    if ((role == TRACK_VOICE_GROUP_ROLE_SOLO) || (role == TRACK_VOICE_GROUP_ROLE_MASTER))
    {
        *out_master_track = track;
        return 1U;
    }

    if (track == 0U)
    {
        return 0U;
    }

    for (int32_t i = (int32_t)track; i >= 0; --i)
    {
        const track_voice_group_role_t cursor_role = track_state_get_voice_group_role((uint8_t)i);
        if (cursor_role == TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            continue;
        }

        if (cursor_role == TRACK_VOICE_GROUP_ROLE_MASTER)
        {
            *out_master_track = (uint8_t)i;
            return 1U;
        }

        return 0U;
    }

    return 0U;
}

uint8_t track_runtime_collect_voice_group_members(uint8_t master_track,
                                                  uint8_t *out_members,
                                                  uint8_t out_members_capacity,
                                                  uint8_t *out_count)
{
    if ((master_track >= SEQ_TRACK_COUNT) || (out_count == NULL))
    {
        return 0U;
    }

    if (track_state_get_voice_group_role(master_track) != TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return 0U;
    }

    uint8_t count = 0U;
    for (uint8_t track = master_track; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_voice_group_role_t role = track_state_get_voice_group_role(track);
        if (track == master_track)
        {
            if (role != TRACK_VOICE_GROUP_ROLE_MASTER)
            {
                return 0U;
            }
        }
        else if (role != TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            break;
        }

        if ((out_members != NULL) && (count < out_members_capacity))
        {
            out_members[count] = track;
        }
        ++count;
    }

    if ((out_members != NULL) && (count > out_members_capacity))
    {
        return 0U;
    }

    *out_count = count;
    return 1U;
}

uint8_t track_runtime_get_voice_group_seq_link(uint8_t track, uint8_t *out_seq_link)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_seq_link == NULL))
    {
        return 0U;
    }

    *out_seq_link = 0U;

    uint8_t master_track = track;
    if (track_runtime_get_voice_group_effective_master(track, &master_track) == 0U)
    {
        return 1U;
    }

    if (track_state_get_voice_group_role(master_track) != TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        return 1U;
    }

    uint8_t member_count = 0U;
    if ((track_runtime_collect_voice_group_members(master_track, NULL, 0U, &member_count) == 0U)
            || (member_count <= 1U))
    {
        return 1U;
    }

    *out_seq_link = track_state_get_voice_group_seq_link(master_track);
    return 1U;
}
