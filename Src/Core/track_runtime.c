#include "Core/track_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/brick6_sampler_runtime.h"
#include "Seq/seq_lane.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Audio/mixer.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/track_input_ownership.h"
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
#define TRACK_RUNTIME_MIX_TRACK_COUNT MIXER_MAX_TRACKS

SEQ_STATE_D2 static track_runtime_ctx_t g_track_runtime_ctx[SEQ_LANE_CAPACITY];
static volatile uint8_t g_track_runtime_global_dirty = 1U;
static volatile uint8_t g_track_runtime_track_dirty[SEQ_LANE_CAPACITY];
static uint32_t g_track_runtime_revision = 0U;
static uint32_t g_track_runtime_track_revision[SEQ_LANE_CAPACITY];
static track_runtime_synth_usage_t g_track_runtime_synth_usage;
static uint8_t g_track_runtime_logical_track_by_mix_track[MIXER_MAX_TRACKS];
volatile uint32_t g_track_runtime_refresh_all_count;
volatile uint32_t g_track_runtime_refresh_in_irq_count;
volatile uint32_t g_track_runtime_refresh_track_count[SEQ_LANE_CAPACITY];

typedef struct
{
    uint8_t drum_used;
    uint8_t looper_used;
} track_runtime_allocator_state_t;

static void track_runtime_rebuild_mix_track_reverse_map(void)
{
    for (uint8_t mix_track = 0U; mix_track < MIXER_MAX_TRACKS; ++mix_track)
    {
        g_track_runtime_logical_track_by_mix_track[mix_track] = 0xFFU;
    }

    for (uint8_t track = 0U; track < SEQ_LANE_CAPACITY; ++track)
    {
        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if ((ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS)
                && ((track_runtime_is_audio_routable(track) != 0U)
                    || (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP)))
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
    if (family == UI_TRACK_FAMILY_MIDI)
    {
        return TRACK_RUNTIME_FAMILY_MIDI;
    }
    if (family == UI_TRACK_FAMILY_EXTERNAL)
    {
        return TRACK_RUNTIME_FAMILY_EXTERNAL;
    }

    return TRACK_RUNTIME_FAMILY_OTHER;
}

static track_runtime_type_t track_runtime_type_from_ui(ui_track_type_t type)
{
    switch (type)
    {
        case UI_TRACK_TYPE_NONE:
            return TRACK_RUNTIME_TYPE_NONE;

        case UI_TRACK_TYPE_RAM:
            return TRACK_RUNTIME_TYPE_RAM;
        case UI_TRACK_TYPE_STREAM:
            return TRACK_RUNTIME_TYPE_STREAM;
        case UI_TRACK_TYPE_PRISM:
            return TRACK_RUNTIME_TYPE_PRISM;
        case UI_TRACK_TYPE_WAVE:
            return TRACK_RUNTIME_TYPE_WAVE;

        case UI_TRACK_TYPE_DRUM_MD:
            return TRACK_RUNTIME_TYPE_DRUM_MD;
        case UI_TRACK_TYPE_MIDI:
            return TRACK_RUNTIME_TYPE_MIDI;
        case UI_TRACK_TYPE_DRUM_BD_ANALOG:
            return TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG;
        case UI_TRACK_TYPE_LOOPER:
            return TRACK_RUNTIME_TYPE_LOOPER;
        case UI_TRACK_TYPE_MULTI:
            return TRACK_RUNTIME_TYPE_MULTI;
        case UI_TRACK_TYPE_GROUP:
            return TRACK_RUNTIME_TYPE_GROUP;
        case UI_TRACK_TYPE_STACK:
            return TRACK_RUNTIME_TYPE_STACK;
        case UI_TRACK_TYPE_EXTERNAL:
            return TRACK_RUNTIME_TYPE_EXTERNAL;

        default:
            return TRACK_RUNTIME_TYPE_OTHER;
    }
}

static uint8_t track_runtime_type_is_drum_model(track_runtime_type_t type)
{
    switch (type)
    {
        case TRACK_RUNTIME_TYPE_DRUM_MD:
        case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
            return 1U;
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

    /* The GROUP parent is a control/sequencer identity, not an audio source.
     * Child sampler lanes will receive their own runtime contexts later. */
    if (type == TRACK_RUNTIME_TYPE_GROUP)
    {
        return 0U;
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
                     || (param == PARAM_VCA_ENV_TYPE)
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
    PARAM_WAVE_OSC1_END,
    PARAM_WAVE_OSC1_LEVEL,
    PARAM_WAVE_OSC1_TUNE,
    PARAM_WAVE_OSC2_TABLE,
    PARAM_WAVE_OSC2_POS,
    PARAM_WAVE_OSC2_START,
    PARAM_WAVE_OSC2_END,
    PARAM_WAVE_OSC2_LEVEL,
    PARAM_WAVE_OSC2_TUNE,
    PARAM_WAVE_FRAME_INTERP,
    PARAM_WAVE_SAMPLE_INTERP,
    PARAM_WAVE_POS_UPDATE,
    PARAM_WAVE_POS_SMOOTH
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

_Static_assert((sizeof(g_track_runtime_tone_slots_prism) / sizeof(g_track_runtime_tone_slots_prism[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "PRISM TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_stack) / sizeof(g_track_runtime_tone_slots_stack[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "STACK TONE slots exceed compact capacity");
_Static_assert((sizeof(g_track_runtime_tone_slots_wave) / sizeof(g_track_runtime_tone_slots_wave[0]))
                   <= SEQ_PARAM_TONE_SLOT_COUNT, "WAVE TONE slots exceed compact capacity");
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

static uint16_t track_runtime_compute_ui_ensemble_mask(const track_runtime_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    uint16_t topology_capabilities = 0U;
    track_topology_descriptor_t topology;
    if (track_topology_get_descriptor(ctx->track_id, &topology) != 0U)
    {
        topology_capabilities = topology.capabilities;
    }
    else if (ctx->track_id >= SEQ_MAIN_TRACK_COUNT)
    {
        topology_capabilities = (uint16_t)(TRACK_CAPABILITY_NOTES
                | TRACK_CAPABILITY_AUDIO
                | TRACK_CAPABILITY_MIDI
                | TRACK_CAPABILITY_KEYBOARD
                | TRACK_CAPABILITY_MIDI_FX
                | TRACK_CAPABILITY_MUTE);
    }
    else
    {
        return 0U;
    }

    uint16_t mask = 0U;
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_CFG);
    mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_SEQ);
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

    if (((topology_capabilities & (uint16_t)TRACK_CAPABILITY_NOTES) != 0U)
            && ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_PLAY) != 0U))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_PLAY);
    }

    if (((ctx->flags & TRACK_RUNTIME_FLAG_CAN_FILTER) != 0U)
            && (track_runtime_is_audio_routable(ctx->track_id) != 0U))
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_ENV);
    }

    if (track_runtime_is_audio_routable(ctx->track_id) != 0U)
    {
        mask |= (uint16_t)(1U << (uint8_t)TRACK_RUNTIME_UI_ENSEMBLE_MIX);
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
        case TRACK_RUNTIME_ENGINE_NONE:
        case TRACK_RUNTIME_ENGINE_AUDIO_TRACK:
        case TRACK_RUNTIME_ENGINE_DRUM:
        default:
            return TRACK_RUNTIME_VOICE_MODE_MONO;
    }
}

uint8_t track_runtime_get_play_voice_count(const track_runtime_ctx_t *ctx)
{
    if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)
            && (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER))
    {
        return brick6_sampler_runtime_get_multi_voice_count(ctx->track_id);
    }
    if ((ctx != NULL) && ((ctx->engine == TRACK_RUNTIME_ENGINE_PRISM)
            || (ctx->engine == TRACK_RUNTIME_ENGINE_STACK)
            || (ctx->engine == TRACK_RUNTIME_ENGINE_WAVE)))
        return synth_polyphony_get_voice_count(ctx->track_id);
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
        return brick6_sampler_runtime_get_multi_voice_count(descriptor->instance_id);
    }
    if ((descriptor->engine == TRACK_RUNTIME_ENGINE_PRISM)
            || (descriptor->engine == TRACK_RUNTIME_ENGINE_STACK)
            || (descriptor->engine == TRACK_RUNTIME_ENGINE_WAVE))
        return synth_polyphony_get_voice_count(descriptor->instance_id);

    switch ((track_runtime_engine_t)descriptor->engine)
    {
        case TRACK_RUNTIME_ENGINE_NONE:
        case TRACK_RUNTIME_ENGINE_AUDIO_TRACK:
        case TRACK_RUNTIME_ENGINE_SAMPLER:
        case TRACK_RUNTIME_ENGINE_LOOPER:
        case TRACK_RUNTIME_ENGINE_PRISM:
        case TRACK_RUNTIME_ENGINE_STACK:
        case TRACK_RUNTIME_ENGINE_WAVE:
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

    if ((track_runtime_ctx_is_sampler_clip_or_looper(ctx) != 0U)
            && (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_STREAM))
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

    if (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return 1U;
    }

    return 0U;
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

    if ((family != TRACK_RUNTIME_FAMILY_SYNTH) && (family != TRACK_RUNTIME_FAMILY_DRUM))
    {
        (void)synth_polyphony_set_track_active(ctx->track_id, 0U, 0U);
    }

    if (family == TRACK_RUNTIME_FAMILY_OFF)
    {
        track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_TRACK_OFF);
        return;
    }

    if ((family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (type == TRACK_RUNTIME_TYPE_GROUP))
    {
        /* Keep the parent represented in runtime without reserving an audio
         * engine or voice.  Lane scheduling will be added in the next step. */
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_NONE, TRACK_RUNTIME_INSTANCE_NONE);
        return;
    }

    if (family == TRACK_RUNTIME_FAMILY_MIDI)
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_NONE, TRACK_RUNTIME_INSTANCE_NONE);
        return;
    }

    if ((family == TRACK_RUNTIME_FAMILY_EXTERNAL)
            && (type == TRACK_RUNTIME_TYPE_EXTERNAL))
    {
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_AUDIO_TRACK, ctx->track_id);
        return;
    }

    if ((family != TRACK_RUNTIME_FAMILY_SYNTH)
            && (family != TRACK_RUNTIME_FAMILY_SAMPLER)
            && (family != TRACK_RUNTIME_FAMILY_DRUM))
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

        if (synth_polyphony_set_track_active(ctx->track_id, 1U,
                (uint8_t)TRACK_RUNTIME_ENGINE_DRUM) == 0U)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }
        (void)synth_polyphony_set_voice_count(ctx->track_id, 1U);

        /*
         * Keep drum instance ownership stable per logical track.
         * This prevents cross-track state migration when drum track cardinality changes.
         */
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_DRUM, ctx->track_id);
        allocator->drum_used++;
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
        if (allocator->looper_used >= BRICK6_LOOPER_GLOBAL_CAP)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }
        allocator->looper_used++;
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_LOOPER, 0U);
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_PRISM)
    {
        if (ctx->track_id >= SYNTH_POLYPHONY_TRACK_CAPACITY)
        {
            track_runtime_set_quota_blocked(ctx);
            return;
        }

        if (synth_polyphony_set_track_active(ctx->track_id, 1U,
                (uint8_t)TRACK_RUNTIME_ENGINE_PRISM) == 0U)
        { track_runtime_set_quota_blocked(ctx); return; }
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_PRISM,
                                synth_polyphony_get_slot(ctx->track_id, 0U));
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_STACK)
    {
        if (synth_polyphony_set_track_active(ctx->track_id, 1U,
                (uint8_t)TRACK_RUNTIME_ENGINE_STACK) == 0U)
        { track_runtime_set_quota_blocked(ctx); return; }
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_STACK,
                                synth_polyphony_get_slot(ctx->track_id, 0U));
        return;
    }

    if (type == TRACK_RUNTIME_TYPE_WAVE)
    {
        if (synth_polyphony_set_track_active(ctx->track_id, 1U,
                (uint8_t)TRACK_RUNTIME_ENGINE_WAVE) == 0U)
        { track_runtime_set_quota_blocked(ctx); return; }
        track_runtime_set_bound(ctx, TRACK_RUNTIME_ENGINE_WAVE,
                                synth_polyphony_get_slot(ctx->track_id, 0U));
        return;
    }

    (void)synth_polyphony_set_track_active(ctx->track_id, 0U, 0U);
    track_runtime_set_unbound(ctx, TRACK_RUNTIME_BIND_REASON_UNSUPPORTED);
}

static void track_runtime_prepare_ctx_base(uint8_t track, track_runtime_ctx_t *ctx)
{
    if ((ctx == NULL) || (track >= SEQ_TRACK_COUNT))
    {
        return;
    }

    const ui_track_config_t config = track_state_get_config(track);
    const ui_track_type_t ui_type = config.type;
    track_runtime_family_t family = track_runtime_family_from_ui(config.family);
    track_runtime_type_t type = track_runtime_type_from_ui(ui_type);
    memset(ctx, 0, sizeof(*ctx));
    ctx->track_id = track;
    ctx->mix_track_id = TRACK_RUNTIME_MIX_TRACK_NONE;
    ctx->midi_channel_1_16 = track_state_get_midi_channel(track);
    ctx->midi_source = (uint8_t)track_state_get_midi_source(track);
    ctx->family = (uint8_t)family;
    ctx->type = (uint8_t)type;
    ctx->flags = track_runtime_compute_flags(family, type);
    if (track_topology_has_capability(track, TRACK_CAPABILITY_NOTES) == 0U)
    {
        ctx->flags &= (uint8_t)~(TRACK_RUNTIME_FLAG_CAN_PLAY | TRACK_RUNTIME_FLAG_CAN_SYNTH);
    }
    ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
    ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
    ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
    ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;

}

static uint8_t track_runtime_child_capabilities(uint8_t track)
{
    seq_lane_descriptor_t lane;
    return (seq_lane_get_descriptor((seq_lane_id_t)track, &lane) != 0U)
            && (lane.active != 0U)
            && (lane.role == SEQ_LANE_ROLE_GROUP_CHILD)
            ? (uint8_t)1U : (uint8_t)0U;
}

static void track_runtime_prepare_group_children(void)
{
    const track_runtime_ctx_t *const parent =
        &g_track_runtime_ctx[SEQ_GROUP_PARENT_MAIN_TRACK];
    const uint8_t group_bound = (uint8_t)((parent->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (parent->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP));

    for (uint8_t child = 0U; child < (uint8_t)SEQ_GROUP_SUBTRACK_COUNT; ++child)
    {
        const uint8_t lane_id = (uint8_t)(SEQ_GROUP_FIRST_CHILD_LANE + child);
        track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[lane_id];
        memset(ctx, 0, sizeof(*ctx));
        ctx->track_id = lane_id;
        ctx->mix_track_id = TRACK_RUNTIME_MIX_TRACK_NONE;
        ctx->midi_channel_1_16 = parent->midi_channel_1_16;
        ctx->midi_source = parent->midi_source;

        if ((group_bound == 0U) || (track_runtime_child_capabilities(lane_id) == 0U))
        {
            ctx->family = (uint8_t)TRACK_RUNTIME_FAMILY_OFF;
            ctx->type = (uint8_t)TRACK_RUNTIME_TYPE_NONE;
            ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_NONE;
            ctx->instance_id = TRACK_RUNTIME_INSTANCE_NONE;
            ctx->bind_state = TRACK_RUNTIME_BIND_UNBOUND;
            ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_TRACK_OFF;
            continue;
        }

        ctx->family = (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER;
        ctx->type = (uint8_t)TRACK_RUNTIME_TYPE_RAM;
        ctx->engine = (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER;
        ctx->instance_id = lane_id;
        ctx->mix_track_id = lane_id;
        ctx->flags = track_runtime_compute_flags(TRACK_RUNTIME_FAMILY_SAMPLER,
                                                  TRACK_RUNTIME_TYPE_RAM);
        ctx->bind_state = TRACK_RUNTIME_BIND_BOUND;
        ctx->bind_reason = TRACK_RUNTIME_BIND_REASON_NONE;
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
    track_runtime_synth_usage_t usage = { 0U };
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND) continue;
        switch ((track_runtime_engine_t)ctx->engine)
        {
            case TRACK_RUNTIME_ENGINE_DRUM: usage.drum_tracks++; break;
            case TRACK_RUNTIME_ENGINE_PRISM: usage.prism_tracks++; break;
            case TRACK_RUNTIME_ENGINE_STACK: usage.stack_tracks++; break;
            case TRACK_RUNTIME_ENGINE_WAVE: usage.wave_tracks++; break;
            default: break;
        }
    }
    g_track_runtime_synth_usage = usage;
}

static void track_runtime_reset_prism_if_owner_changed(uint8_t previous_engine,
                                                      uint8_t previous_instance,
                                                      const track_runtime_ctx_t *current_ctx)
{
    (void)previous_instance;
    const uint8_t current_is_prism = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)) ? 1U : 0U;
    if ((current_is_prism != 0U) && (previous_engine != (uint8_t)TRACK_RUNTIME_ENGINE_PRISM))
    {
        (void)param_backend_reapply_tone_prism_runtime(current_ctx->track_id);
    }
}
static void track_runtime_reset_stack_if_owner_changed(uint8_t previous_engine,
                                                       uint8_t previous_instance,
                                                       const track_runtime_ctx_t *current_ctx)
{
    (void)previous_instance;
    const uint8_t current_is_stack = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)) ? 1U : 0U;
    if ((current_is_stack != 0U) && (previous_engine != (uint8_t)TRACK_RUNTIME_ENGINE_STACK))
    {
        (void)param_backend_reapply_tone_stack_runtime(current_ctx->track_id);
    }
}

static void track_runtime_reset_wave_if_owner_changed(uint8_t previous_engine,
                                                      uint8_t previous_instance,
                                                      const track_runtime_ctx_t *current_ctx)
{
    (void)previous_instance;
    const uint8_t current_is_wave = ((current_ctx != NULL)
            && (current_ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (current_ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)) ? 1U : 0U;
    if ((current_is_wave != 0U) && (previous_engine != (uint8_t)TRACK_RUNTIME_ENGINE_WAVE))
    {
        (void)param_backend_reapply_tone_wave_runtime(current_ctx->track_id);
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
    track_runtime_synth_usage_t synth_usage = { 0U };
    uint8_t previous_looper[SEQ_TRACK_COUNT];

    g_track_runtime_refresh_all_count++;
    memset(mix_track_used, 0, sizeof(mix_track_used));
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        previous_looper[track] = (uint8_t)((g_track_runtime_ctx[track].bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (g_track_runtime_ctx[track].engine == (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER));
        previous_mix_track[track] = g_track_runtime_ctx[track].mix_track_id;
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
                || ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_GROUP)
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
        else if ((track_runtime_type_t)ctx->type == TRACK_RUNTIME_TYPE_GROUP)
        {
            (void)track_runtime_mix_try_reserve_exact(ctx,
                                                      (uint8_t)MIXER_GROUP_BUS_TRACK,
                                                      mix_track_used,
                                                      (uint8_t)sizeof(mix_track_used));
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
            switch ((track_runtime_engine_t)ctx->engine)
            {
                case TRACK_RUNTIME_ENGINE_DRUM: synth_usage.drum_tracks++; break;
                case TRACK_RUNTIME_ENGINE_PRISM: synth_usage.prism_tracks++; break;
                case TRACK_RUNTIME_ENGINE_STACK: synth_usage.stack_tracks++; break;
                case TRACK_RUNTIME_ENGINE_WAVE: synth_usage.wave_tracks++; break;
                default: break;
            }
        }
    }

    track_runtime_prepare_group_children();

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        if ((previous_looper[track] != 0U)
                && ((g_track_runtime_ctx[track].bind_state != TRACK_RUNTIME_BIND_BOUND)
                    || (g_track_runtime_ctx[track].engine != (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER)))
        {
            brick6_looper_runtime_prepare_replace(track);
        }
    }


    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        const track_runtime_ctx_t *const ctx = &g_track_runtime_ctx[track];
        if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND) continue;
        if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_PRISM)
            (void)param_backend_reapply_tone_prism_runtime(track);
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_STACK)
            (void)param_backend_reapply_tone_stack_runtime(track);
        else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_WAVE)
            (void)param_backend_reapply_tone_wave_runtime(track);
    }

    g_track_runtime_synth_usage = synth_usage;
    track_runtime_rebuild_mix_track_reverse_map();
    g_track_runtime_global_dirty = 0U;
    ++g_track_runtime_revision;
    for (uint8_t track = 0U; track < (uint8_t)SEQ_LANE_CAPACITY; ++track)
    {
        g_track_runtime_track_dirty[track] = 0U;
        g_track_runtime_track_revision[track] = g_track_runtime_revision;
    }
}

uint8_t track_runtime_is_track_prism_available(uint8_t track)
{
    return (synth_polyphony_get_available_for_track(track) > 0U) ? 1U : 0U;
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
        for (uint8_t other = 0U; other < SEQ_TRACK_COUNT; ++other)
        {
            if ((other != track)
                    && (g_track_runtime_ctx[other].bind_state == TRACK_RUNTIME_BIND_BOUND)
                    && (g_track_runtime_ctx[other].engine == (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER))
            {
                allocator.looper_used = 1U;
                break;
            }
        }
        track_runtime_mark_used_mix_tracks_except(track,
                                                  mix_track_used,
                                                  (uint8_t)sizeof(mix_track_used));

        if (((track_runtime_family_t)next_ctx.family != TRACK_RUNTIME_FAMILY_MIDI)
                && ((track_runtime_family_t)next_ctx.family != TRACK_RUNTIME_FAMILY_OFF)
                && ((track_runtime_type_t)next_ctx.type != TRACK_RUNTIME_TYPE_GROUP)
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
        if (track == (uint8_t)SEQ_GROUP_PARENT_MAIN_TRACK)
        {
            track_runtime_prepare_group_children();
            for (uint8_t child = (uint8_t)SEQ_GROUP_FIRST_CHILD_LANE;
                 child <= (uint8_t)SEQ_GROUP_LAST_CHILD_LANE;
                 ++child)
            {
                g_track_runtime_track_dirty[child] = 0U;
                g_track_runtime_track_revision[child] = g_track_runtime_revision + 1U;
            }
        }
        track_runtime_reset_prism_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_reset_stack_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        track_runtime_reset_wave_if_owner_changed(previous_engine, previous_instance, &g_track_runtime_ctx[track]);
        if ((previous_engine == (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER)
                && (g_track_runtime_ctx[track].engine != (uint8_t)TRACK_RUNTIME_ENGINE_LOOPER))
        {
            brick6_looper_runtime_prepare_replace(track);
        }
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

uint8_t track_runtime_is_audio_routable_ctx(const track_runtime_ctx_t *ctx)
{
    /* Logical track can exist without any physical mixer lane. */
    if ((ctx == NULL)
            || (ctx->track_id >= SEQ_LANE_CAPACITY)
            || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if (ctx->mix_track_id >= MIXER_MAX_TRACKS)
    {
        return 0U;
    }
    if (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_GROUP)
    {
        return 0U;
    }
    if ((track_runtime_family_t)ctx->family == TRACK_RUNTIME_FAMILY_EXTERNAL)
    {
        return track_input_ownership_track_owns_input(
            ctx->track_id,
            track_input_ownership_get_external_input(ctx->track_id));
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

    if ((track >= SEQ_MAIN_TRACK_COUNT)
            && (track_runtime_child_capabilities(track) == 0U))
    {
        return 0U;
    }
    if ((track < SEQ_MAIN_TRACK_COUNT)
            && (track_topology_has_capability(track, capability) == 0U))
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
            return (uint8_t)(ctx->bind_state == TRACK_RUNTIME_BIND_BOUND);

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
    if ((ctx == NULL) || (ctx->mix_track_id == TRACK_RUNTIME_MIX_TRACK_NONE))
    {
        return 0U;
    }

    if ((track_runtime_is_audio_routable(track) == 0U)
            && (ctx->type != (uint8_t)TRACK_RUNTIME_TYPE_GROUP))
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
    if (track < SEQ_LANE_CAPACITY)
    {
        *out_track = track;
        return 1U;
    }

    return 0U;
}

uint8_t track_runtime_resolve_filter_target_track(uint8_t ui_track, uint8_t *out_filter_track)
{
    if ((out_filter_track == NULL) || (ui_track >= SEQ_LANE_CAPACITY))
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
    if ((track >= SEQ_LANE_CAPACITY) || (out_descriptor == NULL))
    {
        return 0U;
    }
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if (ctx == NULL)
    {
        return 0U;
    }

    uint16_t topology_capabilities = 0U;
    track_topology_descriptor_t topology;
    if (track_topology_get_descriptor(track, &topology) != 0U)
    {
        topology_capabilities = topology.capabilities;
    }
    else if ((track >= SEQ_MAIN_TRACK_COUNT)
            && (track_runtime_child_capabilities(track) != 0U))
    {
        topology_capabilities = (uint16_t)(TRACK_CAPABILITY_NOTES
                | TRACK_CAPABILITY_AUDIO
                | TRACK_CAPABILITY_MIDI
                | TRACK_CAPABILITY_KEYBOARD
                | TRACK_CAPABILITY_MIDI_FX
                | TRACK_CAPABILITY_MUTE);
    }
    else
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
            rule.domain = TRACK_RUNTIME_PARAM_DOMAIN_ENV;
            rule.resource = TRACK_RUNTIME_RESOURCE_FILTER;
            return rule;
        case PARAM_VCA_ATTACK:
        case PARAM_VCA_DECAY:
        case PARAM_VCA_SUSTAIN:
        case PARAM_VCA_RELEASE:
        case PARAM_VCA_ENV_TYPE:
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
        case PARAM_WAVE_OSC1_END:
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_TABLE:
        case PARAM_WAVE_OSC2_POS:
        case PARAM_WAVE_OSC2_START:
        case PARAM_WAVE_OSC2_END:
        case PARAM_WAVE_OSC2_LEVEL:
        case PARAM_WAVE_OSC2_TUNE:
        case PARAM_WAVE_FRAME_INTERP:
        case PARAM_WAVE_SAMPLE_INTERP:
        case PARAM_WAVE_POS_UPDATE:
        case PARAM_WAVE_POS_SMOOTH:
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

        case PARAM_MIX_REVERB_WET:
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
            if (track_runtime_is_audio_routable(track) == 0U)
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
            return ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_POLYPHONY:
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if ((ctx->flags & TRACK_RUNTIME_FLAG_CAN_SYNTH) != 0U)
            {
                return TRACK_RUNTIME_PARAM_ALLOWED;
            }
            return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
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
                const uint8_t external_track =
                    ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                     && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL)) ? 1U : 0U;
                if ((midi_track == 0U) && (external_track == 0U))
                {
                    return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
                }
            }
            if (param == PARAM_EXTERNAL_INPUT)
            {
                return ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_EXTERNAL)
                        && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_EXTERNAL))
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
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
            if (ctx->bind_state == TRACK_RUNTIME_BIND_QUOTA_BLOCKED)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if (track_runtime_is_audio_routable(track) == 0U)
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            if ((track_runtime_param_is_vca(param) != 0U)
                    && (track_runtime_supports_vca_gate(ctx) == 0U))
            {
                return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
            }
            return (track_runtime_is_audio_routable(track) != 0U)
                    ? TRACK_RUNTIME_PARAM_ALLOWED
                    : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        case TRACK_RUNTIME_RESOURCE_MIDI_FX:
            return (track_runtime_is_ui_ensemble_available(track,
                                                            TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX) != 0U)
                ? TRACK_RUNTIME_PARAM_ALLOWED
                : TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;

        default:
            return TRACK_RUNTIME_PARAM_BLOCKED_TRANSITIONAL;
    }
}
