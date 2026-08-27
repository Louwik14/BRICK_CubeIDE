/**
 * @file mixer.c
 * @brief Moteur de mixage final track-based (gain/pan/mute/routing/inserts/sends).
 *
 * Rôle du module:
 * - Maintenir l'état runtime des tracks du mixer.
 * - Effectuer le mix final MAIN avec inserts et send FX.
 *
 * Architecture:
 * - Appelé par: my_dsp() (brick6_app_init.c via dsp_engine).
 * - Appelle: fx_chain_process_track_inserts_pre_fader(),
 *   fx_chain_process_audio_fx_post_fader(),
 *   fx_chain_process_global_slot(), audio_float_set_master_gain().
 *
 * Contraintes temps réel:
 * - IRQ: oui (mixer_process est dans le chemin DSP audio).
 * - Hard realtime: oui.
 * - malloc: interdit.
 *
 * Notes:
 * - Slots insert/send à -1 => FX inactif (coût CPU nul sur le slot).
 */

#include "mixer.h"
#include "Audio/audio_float.h"
#include "Audio/audio_fx_runtime.h"
#include "Audio/audio_waveform_capture.h"
#include "Audio/audio_io.h"
#include "Audio/audio_rec_level_snapshot.h"

#include "Audio/audio_xfade.h"
#include "env_adsr.h"
#include "vca_env.h"
#include "fx_biquad_filter.h"
#include "fx_deluge_filter.h"
#include "fx_delay_dual.h"
#include "fx_delay_stereo.h"
#include "Audio/fx_modfx_global.h"
#include "fx_reverb.h"
#include "Audio/spectral_window.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_fm_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Core/mixer_routing_publication.h"
#include "Core/audio_rec_bus_projection.h"
#include "Audio/audio_note_engine_adapter.h"
#include "Audio/multi_voice_dsp.h"

#include "Storage/audio_recorder.h"
#include "Storage/sample_capture.h"
#include "Core/control_routing.h"

#include <math.h>
#include <string.h>

#include "fx_chain.h"
#include "fx_pool.h"
#include "memory_layout.h"

typedef struct __attribute__((aligned(32))) {
    float gain;
    float pan;
    float gain_current;
    float pan_current;
    float mute_gain_current;
    uint8_t mute;

    uint8_t route_master;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
    float send_level_current[MIXER_NUM_SENDS];
    float group_fx_level[2];
    float group_fx_level_current[2];
} mixer_audio_track_runtime_t;

_Static_assert(sizeof(mixer_audio_track_runtime_t) == 64U,
               "GROUP local levels must stay inside mixer track padding");

typedef enum
{
    MIXER_FILTER_DSP_STEREO = 0U,
    MIXER_FILTER_DSP_MONO = 1U
} mixer_filter_dsp_format_t;

typedef enum
{
    MIXER_CONT_MORPH = 0,
    MIXER_CONT_CUTOFF,
    MIXER_CONT_RESONANCE,
    MIXER_CONT_EG_AMOUNT,
    MIXER_CONT_FILTER_ATTACK,
    MIXER_CONT_FILTER_DECAY,
    MIXER_CONT_FILTER_SUSTAIN,
    MIXER_CONT_FILTER_RELEASE,
    MIXER_CONT_VCA_ATTACK,
    MIXER_CONT_VCA_DECAY,
    MIXER_CONT_VCA_SUSTAIN,
    MIXER_CONT_VCA_RELEASE,
    MIXER_CONT_COUNT
} mixer_continuous_param_t;

typedef struct __attribute__((aligned(32))) {
    union {
        fx_biquad_filter_t biquad;
        fx_biquad_filter_mono_t biquad_mono;
        fx_deluge_filter_t deluge;
    };
    env_adsr_t filter_env;
    env_adsr_t vca_env;
    vca_env_t synth_vca_env;
    float sample_rate;
    float cutoff_hz;
    float cutoff_target_hz;
    float cutoff_mod_hz;
    float cutoff_mod_target_hz;
    float resonance;
    float resonance_target;
    float eg_amount;
    float keytrack;
    float keytrack_ratio;
    float keytrack_ratio_target;
    float morph;
    float morph_target;
    float morph_step;
    float vca_env_value;
    float filter_env_value;
    uint32_t config_version;
    uint32_t continuous_epoch;
    uint32_t continuous_version[MIXER_CONT_COUNT];
    int16_t filter_env_prepared_first[AUDIO_BLOCK_SIZE / 8U];
    int16_t filter_env_prepared_terminal[AUDIO_BLOCK_SIZE / 8U];
    uint16_t filter_env_prepared_frames;
    uint8_t note_active;
    uint8_t current_note;
    uint8_t vca_enabled;
    uint8_t vca_note_active;
    uint8_t vca_note_count;
    uint8_t vca_current_note;
    uint8_t vca_gate;
    uint8_t filter_retrigger_hard;
    uint8_t vca_retrigger_hard;
    uint8_t filter_mode;
    uint8_t morph_ramp_remaining;
    uint8_t filter_env_prepared_count;
    uint8_t filter_env_prepared_consumed;
    uint8_t dsp_format;
} mixer_track_filter_t;

_Static_assert(_Alignof(mixer_track_filter_t) >= 32U,
               "mixer track filter DSP storage must stay 32-byte aligned");
_Static_assert((sizeof(mixer_track_filter_t) % 32U) == 0U,
               "mixer track filter array stride must preserve DSP alignment");
_Static_assert(sizeof(mixer_track_filter_t) == 416U,
               "mixer track filter size changed; remeasure before accepting it");

typedef enum
{
    MIXER_LANE_SOURCE_NONE = 0U,
    MIXER_LANE_SOURCE_HW_STEREO,
    MIXER_LANE_SOURCE_EXT_STEREO,
    MIXER_LANE_SOURCE_EXT_MONO_NATIVE,
    MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO,
    MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO
} mixer_lane_source_kind_t;

typedef enum
{
    MIXER_LANE_EXEC_STEREO = 0U,
    MIXER_LANE_EXEC_MONO_NATIVE
} mixer_lane_exec_kind_t;

typedef struct
{
    uint8_t active;
    uint8_t hw_enabled;
    uint8_t ext_enabled;
    uint8_t ext_format;
    uint32_t ext_frames;
    mixer_lane_source_kind_t source_kind;
    mixer_lane_exec_kind_t exec_kind;
} mixer_lane_plan_t;

static mixer_audio_track_runtime_t g_tracks[MIXER_MAX_TRACKS];
static uint32_t g_mixer_routing_audio_generation;
static int8_t g_send_fx_slot[MIXER_NUM_SENDS];
static AUDIO_HOT mixer_track_filter_t g_track_filters[MIXER_MAX_TRACKS];
static uint32_t g_mixer_filter_config_version;
AUDIO_HOT static mixer_track_filter_t g_poly_filters_hot[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
typedef struct
{
    float effective_hz;
    uint8_t valid;
} mixer_poly_cutoff_override_t;
AUDIO_HOT static mixer_poly_cutoff_override_t
    g_poly_cutoff_override[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
static AUDIO_HOT float g_external_track_mono[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_l[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_r[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static uint8_t g_external_track_enabled[MIXER_MAX_TRACKS];
static uint32_t g_external_lane_mask;
static uint8_t g_external_poly_initialized[MIXER_MAX_TRACKS];
static uint8_t g_external_track_format[MIXER_MAX_TRACKS];
static uint16_t g_external_track_frames_valid[MIXER_MAX_TRACKS];
volatile uint32_t g_mixer_lane_rebind_count[MIXER_MAX_TRACKS];
static float g_looper_xfade_smoothed = 0.0f;
static float g_looper_xfade_prev = 0.0f;

enum
{
    MIXER_STATIC_GROUP_CHILD = 1U << 0,
    MIXER_STATIC_LOOPER = 1U << 1,
    MIXER_STATIC_ROUTE_MAIN = 1U << 2,
    MIXER_STATIC_AUDIO_FX_ACTIVE = 1U << 3,
    MIXER_STATIC_AUDIO_FX_PRE_FILTER = 1U << 4,
    MIXER_STATIC_REQUIRES_STEREO = 1U << 5,
    MIXER_STATIC_INSERT_STAGE = 1U << 6,
    MIXER_STATIC_AUDIO_FX_COMP = 1U << 7
};

static uint8_t g_mixer_static_lane_flags[MIXER_MAX_TRACKS];
static uint8_t g_mixer_static_group_active;

void mixer_rebuild_static_plan(void)
{
    for (uint8_t lane = 0U; lane < MIXER_MAX_TRACKS; ++lane)
    {
        uint8_t flags = (g_tracks[lane].route_master != 0U)
            ? MIXER_STATIC_ROUTE_MAIN : 0U;
        const brick_entity_id_t entity =
            audio_note_engine_adapter_entity_for_mix_lane(lane);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (entity < BRICK_ENTITY_CAPACITY)
                && (audio_note_engine_adapter_audio_ctx_snapshot(
                    entity, &ctx_value) != 0U) ? &ctx_value : NULL;
        if ((ctx != NULL)
                && ((ctx->flags & AUDIO_RUNTIME_FLAG_GROUP_CHILD) != 0U))
            flags |= MIXER_STATIC_GROUP_CHILD;
        if ((ctx != NULL)
                && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER))
            flags |= MIXER_STATIC_LOOPER;
        if ((entity < BRICK_ENTITY_CAPACITY)
                && ((ctx == NULL)
                    || ((ctx->flags & AUDIO_RUNTIME_FLAG_GROUP_MASTER) == 0U))
                && (fx_chain_audio_fx_is_active(entity) != 0U))
        {
            flags |= MIXER_STATIC_AUDIO_FX_ACTIVE;
            if (audio_fx_runtime_get_filter_pos(entity)
                    != AUDIO_FX_FILTER_POS_PRE)
                flags |= MIXER_STATIC_AUDIO_FX_PRE_FILTER;
        }
        if ((entity < BRICK_ENTITY_CAPACITY)
                && (fx_chain_audio_fx_is_comp(entity) != 0U))
            flags |= MIXER_STATIC_AUDIO_FX_COMP;
        if (fx_chain_track_inserts_require_stereo(
                entity,
                g_tracks[lane].insert_slot,
                MIXER_INSERTS_PER_TRACK) != 0U)
            flags |= MIXER_STATIC_REQUIRES_STEREO;
        if (fx_chain_track_has_pre_fader_insert(
                entity,
                g_tracks[lane].insert_slot,
                MIXER_INSERTS_PER_TRACK) != 0U)
            flags |= MIXER_STATIC_INSERT_STAGE;
        g_mixer_static_lane_flags[lane] = flags;
    }

    track_audio_runtime_ctx_t group_master_ctx_value;
    const track_audio_runtime_ctx_t *const group_master_ctx =
        (audio_note_engine_adapter_audio_ctx_snapshot(
            BRICK_ENTITY_GROUP_MASTER_ID, &group_master_ctx_value) != 0U)
            ? &group_master_ctx_value : NULL;
    g_mixer_static_group_active = (uint8_t)((group_master_ctx != NULL)
        && ((group_master_ctx->flags & AUDIO_RUNTIME_FLAG_GROUP_MASTER) != 0U));
}

static __attribute__((noinline)) void mixer_routing_audio_apply_publication(void)
{
    mixer_routing_snapshot_t snapshot;
    if (mixer_routing_publication_audio_read(
            g_mixer_routing_audio_generation, &snapshot) == 0U)
        return;
    for (uint32_t track = 0U; track < MIXER_MAX_TRACKS; ++track)
    {
        g_tracks[track].route_master = snapshot.route_master[track];
        for (uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
            g_tracks[track].insert_slot[insert] = snapshot.insert_slot[track][insert];
    }
    g_mixer_routing_audio_generation = snapshot.generation;
    mixer_rebuild_static_plan();
}

static void mixer_track_filter_process_biquad_stereo_block(mixer_track_filter_t *filter,
                                                           float *left,
                                                           float *right,
                                                           uint32_t frames,
                                                           float cutoff_start_hz,
                                                           float cutoff_mod_start_hz,
                                                           float resonance_start,
                                                           float keytrack_ratio_start);
static uint8_t ITCM_TEXT mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
                                                     float *mono,
                                                     uint32_t frames,
                                                     const mixer_poly_cutoff_override_t *poly_cutoff);
static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter);

static uint32_t mixer_track_filter_next_config_version(void)
{
    g_mixer_filter_config_version++;
    if (g_mixer_filter_config_version == 0U)
    {
        g_mixer_filter_config_version = 1U;
    }
    return g_mixer_filter_config_version;
}

static void mixer_track_filter_touch_config(mixer_track_filter_t *filter)
{
    if (filter != NULL)
    {
        filter->config_version = mixer_track_filter_next_config_version();
    }
}

static void mixer_track_filter_touch_continuous(mixer_track_filter_t *filter,
                                                mixer_continuous_param_t param)
{
    if ((filter != NULL) && (param < MIXER_CONT_COUNT))
    {
        const uint32_t version = mixer_track_filter_next_config_version();
        filter->continuous_version[param] = version;
        filter->continuous_epoch = version;
    }
}

static void mixer_track_filter_advance_morph(mixer_track_filter_t *filter, uint32_t frames)
{
    if ((filter == NULL) || (filter->morph_ramp_remaining == 0U)) return;
    if (frames >= filter->morph_ramp_remaining)
    {
        filter->morph = filter->morph_target;
        filter->morph_ramp_remaining = 0U;
    }
    else
    {
        filter->morph += filter->morph_step * (float)frames;
        filter->morph_ramp_remaining = (uint8_t)(filter->morph_ramp_remaining - frames);
    }
    if (filter->filter_mode == 1U)
    {
        if (filter->dsp_format == MIXER_FILTER_DSP_MONO)
            fx_biquad_filter_mono_set_morph(&filter->biquad_mono, filter->morph);
        else
            fx_biquad_filter_set_morph(&filter->biquad, filter->morph);
    }
}

enum
{
    MIXER_EXTERNAL_FORMAT_NONE = 0U,
    MIXER_EXTERNAL_FORMAT_MONO_NATIVE = 1U,
    MIXER_EXTERNAL_FORMAT_STEREO = 2U,
    MIXER_EXTERNAL_FORMAT_POLY_STEREO = 3U,
    MIXER_EXTERNAL_FORMAT_MULTI_STEREO = 4U,
    MIXER_EXTERNAL_FORMAT_MULTI_MONO = 5U
};

static mixer_track_filter_t *mixer_poly_filter(uint32_t poly_track_id, uint8_t voice)
{
    if ((poly_track_id >= SYNTH_POLYPHONY_TRACK_CAPACITY)
            || (voice >= SYNTH_POLYPHONY_MAX_VOICES))
    {
        return NULL;
    }
    const uint8_t index = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
    return (index < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
        ? &g_poly_filters_hot[index] : NULL;
}

static ITCM_TEXT void mixer_poly_filter_sync_config(mixer_track_filter_t *dst,
                                          const mixer_track_filter_t *src)
{
    if ((dst == NULL) || (src == NULL))
    {
        return;
    }
    if ((dst->config_version == src->config_version)
            && (dst->continuous_epoch == src->continuous_epoch))
    {
        return;
    }
    const uint8_t full = (dst->config_version != src->config_version) ? 1U : 0U;
    if (full != 0U)
    {
        dst->sample_rate = src->sample_rate;
        dst->keytrack = src->keytrack;
        dst->morph_target = src->morph_target;
        if (dst->morph != dst->morph_target) {
            dst->morph_step = (dst->morph_target - dst->morph) * (1.0f / 64.0f);
            dst->morph_ramp_remaining = 64U;
        }
        dst->filter_retrigger_hard = src->filter_retrigger_hard;
        dst->vca_retrigger_hard = src->vca_retrigger_hard;
        if(dst->filter_mode != src->filter_mode)
        {
            dst->filter_mode = src->filter_mode;
            fx_deluge_filter_init(&dst->deluge, dst->sample_rate);
        }
        dst->config_version = src->config_version;
    }
    /*
     * A poly voice always needs its own amplitude envelope.  The track-level
     * VCA enable flag only selects the mono track processor; using it here
     * leaves free-running oscillators permanently audible and prevents the
     * allocator from ever observing an IDLE release.
     */
    dst->vca_enabled = 1U;
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_CUTOFF]
            != src->continuous_version[MIXER_CONT_CUTOFF]))
    {
        dst->cutoff_target_hz = src->cutoff_target_hz;
        dst->cutoff_mod_target_hz = src->cutoff_mod_target_hz;
        dst->continuous_version[MIXER_CONT_CUTOFF] = src->continuous_version[MIXER_CONT_CUTOFF];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_MORPH]
            != src->continuous_version[MIXER_CONT_MORPH]))
    {
        dst->morph_target = src->morph_target;
        dst->morph_step = (dst->morph_target - dst->morph) * (1.0f / 64.0f);
        dst->morph_ramp_remaining = (dst->morph == dst->morph_target) ? 0U : 64U;
        dst->continuous_version[MIXER_CONT_MORPH] = src->continuous_version[MIXER_CONT_MORPH];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_RESONANCE]
            != src->continuous_version[MIXER_CONT_RESONANCE]))
    {
        dst->resonance_target = src->resonance_target;
        dst->continuous_version[MIXER_CONT_RESONANCE] = src->continuous_version[MIXER_CONT_RESONANCE];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_EG_AMOUNT]
            != src->continuous_version[MIXER_CONT_EG_AMOUNT]))
    {
        dst->eg_amount = src->eg_amount;
        dst->continuous_version[MIXER_CONT_EG_AMOUNT] = src->continuous_version[MIXER_CONT_EG_AMOUNT];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_ATTACK]
            != src->continuous_version[MIXER_CONT_FILTER_ATTACK]))
    {
        env_adsr_set_attack(&dst->filter_env, src->filter_env.attack);
        dst->continuous_version[MIXER_CONT_FILTER_ATTACK] = src->continuous_version[MIXER_CONT_FILTER_ATTACK];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_DECAY]
            != src->continuous_version[MIXER_CONT_FILTER_DECAY]))
    {
        env_adsr_set_decay(&dst->filter_env, src->filter_env.decay);
        dst->continuous_version[MIXER_CONT_FILTER_DECAY] = src->continuous_version[MIXER_CONT_FILTER_DECAY];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_SUSTAIN]
            != src->continuous_version[MIXER_CONT_FILTER_SUSTAIN]))
    {
        env_adsr_set_sustain(&dst->filter_env, src->filter_env.sustain);
        dst->continuous_version[MIXER_CONT_FILTER_SUSTAIN] = src->continuous_version[MIXER_CONT_FILTER_SUSTAIN];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_FILTER_RELEASE]
            != src->continuous_version[MIXER_CONT_FILTER_RELEASE]))
    {
        env_adsr_set_release(&dst->filter_env, src->filter_env.release);
        dst->continuous_version[MIXER_CONT_FILTER_RELEASE] = src->continuous_version[MIXER_CONT_FILTER_RELEASE];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_ATTACK]
            != src->continuous_version[MIXER_CONT_VCA_ATTACK]))
    {
        vca_env_set_attack(&dst->synth_vca_env, src->synth_vca_env.attack_time);
        dst->continuous_version[MIXER_CONT_VCA_ATTACK] = src->continuous_version[MIXER_CONT_VCA_ATTACK];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_DECAY]
            != src->continuous_version[MIXER_CONT_VCA_DECAY]))
    {
        vca_env_set_decay(&dst->synth_vca_env, src->synth_vca_env.decay_time);
        dst->continuous_version[MIXER_CONT_VCA_DECAY] = src->continuous_version[MIXER_CONT_VCA_DECAY];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_SUSTAIN]
            != src->continuous_version[MIXER_CONT_VCA_SUSTAIN]))
    {
        vca_env_set_sustain(&dst->synth_vca_env, src->synth_vca_env.sustain);
        dst->continuous_version[MIXER_CONT_VCA_SUSTAIN] = src->continuous_version[MIXER_CONT_VCA_SUSTAIN];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_VCA_RELEASE]
            != src->continuous_version[MIXER_CONT_VCA_RELEASE]))
    {
        vca_env_set_release(&dst->synth_vca_env, src->synth_vca_env.release_time);
        dst->continuous_version[MIXER_CONT_VCA_RELEASE] = src->continuous_version[MIXER_CONT_VCA_RELEASE];
    }
    if(full != 0U) mixer_track_filter_apply_core_params(dst);
    dst->continuous_epoch = src->continuous_epoch;
}

#define MIXER_FILTER_SAMPLE_RATE_DEFAULT 48000.0f
#define MIXER_FILTER_CUTOFF_MIN_HZ 20.0f
#define MIXER_FILTER_CUTOFF_MAX_HZ 16000.0f
#define MIXER_FILTER_ATTACK_MIN_S 0.001f
#define MIXER_FILTER_ATTACK_MAX_S 5.0f
#define MIXER_FILTER_DECAY_MIN_S 0.001f
#define MIXER_FILTER_DECAY_MAX_S 5.0f
#define MIXER_FILTER_RELEASE_MIN_S 0.001f
#define MIXER_FILTER_RELEASE_MAX_S 5.0f
#define MIXER_FILTER_NOTE_REF_MIDI 60U
#define MIXER_FILTER_UPDATE_PERIOD 8U
#define MIXER_FILTER_BLOCK_SMOOTH 0.25f
#define MIXER_REVERB_SEND_INDEX 0U
#define MIXER_DELAY_SEND_INDEX 1U
#define MIXER_LOOPER_RECORD_PCM24_MAX 8388607.0f
#define MIXER_LOOPER_RECORD_PCM24_MIN (-8388608.0f)
#define MIXER_LOOPER_XFADE_EPS 0.0001f

typedef enum
{
    MIXER_DELAY_TYPE_CLASSIC = 0U,
    MIXER_DELAY_TYPE_DUAL = 1U
} mixer_delay_type_t;

static uint8_t g_delay_type = (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
static float g_delay_diag_volume;
static float g_delay_diag_reverb_send;
#define MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS 30.0f
typedef struct
{
    float wet;
    float room_size;
    float damping;
    float width;
    float hpf;
    float lpf;
} mixer_reverb_state_t;

static AUDIO_HOT mixer_reverb_state_t g_reverb = {
    .wet = 0.0f,
    .room_size = 0.60f,
    .damping = 0.72f,
    .width = 1.0f,
    .hpf = 0.0f,
    .lpf = 1.0f,
};

static float g_delay_spectral_position = 0.50f;
static float g_delay_spectral_width = 1.0f;

static int32_t mixer_looper_float_to_pcm24(float sample)
{
    float scaled = sample * MIXER_LOOPER_RECORD_PCM24_MAX;
    if(scaled > MIXER_LOOPER_RECORD_PCM24_MAX)
    {
        scaled = MIXER_LOOPER_RECORD_PCM24_MAX;
    }
    else if(scaled < MIXER_LOOPER_RECORD_PCM24_MIN)
    {
        scaled = MIXER_LOOPER_RECORD_PCM24_MIN;
    }

    return (int32_t)scaled;
}

static uint8_t mixer_looper_record_capture_is_active(uint8_t *out_looper_track)
{
    if((out_looper_track == NULL)
            || (brick6_looper_runtime_get_record_capture_track(out_looper_track) == 0U)
            || (*out_looper_track >= MIXER_MAX_TRACKS))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t mixer_lane_routes_to_looper(uint8_t looper_track,
                                           uint8_t mix_track)
{
    const brick_entity_id_t entity =
        audio_note_engine_adapter_entity_for_mix_lane(mix_track);
    const uint8_t source_track = (entity < BRICK_ENTITY_CAPACITY)
        ? entity : mix_track;
    if ((looper_track >= MIXER_MAX_TRACKS)
            || (source_track >= MIXER_MAX_TRACKS)
            || (source_track == looper_track)
            || ((g_mixer_static_lane_flags[mix_track]
                & MIXER_STATIC_LOOPER) != 0U))
    {
        return 0U;
    }

    return control_routing_audio_get_looper_source(looper_track, source_track);
}

static float mixer_get_looper_xfade(void)
{
    const float target = audio_xfade_get();
    return audio_xfade_smooth_next(target, &g_looper_xfade_smoothed);
}

static uint8_t mixer_looper_xfade_value_is_zero(float value)
{
    return (value <= MIXER_LOOPER_XFADE_EPS) ? 1U : 0U;
}

static uint8_t mixer_looper_xfade_value_is_full(float value)
{
    return (value >= (1.0f - MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
}

static uint8_t mixer_looper_xfade_values_are_stable(float a, float b)
{
    const float delta = a - b;
    return ((delta <= MIXER_LOOPER_XFADE_EPS) && (delta >= -MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
}

static float clampf_local(float v, float lo, float hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static float mixer_smooth_block(float current, float target, float alpha)
{
    return current + ((target - current) * alpha);
}

enum
{
    MIXER_FILTER_TIME_LUT_SIZE = 512U
};

#define MIXER_FILTER_LOG2_MIN_TIME (-9.965784284662087f)
#define MIXER_FILTER_INV_LOG2_TIME_RANGE 0.08138130233100371f
#define MIXER_FILTER_INV_LOG2 1.4426950408889634f

static uint16_t g_mixer_filter_time_lut[MIXER_FILTER_TIME_LUT_SIZE + 1U];
static float g_mixer_filter_log2_lut[257U];
static uint8_t g_mixer_filter_time_lut_ready = 0U;

static void mixer_track_filter_init_time_lut(void)
{
    if(g_mixer_filter_time_lut_ready != 0U)
        return;

    const float time_log_ratio = logf(MIXER_FILTER_ATTACK_MAX_S / MIXER_FILTER_ATTACK_MIN_S);
    const float max_segment_samples = MIXER_FILTER_SAMPLE_RATE_DEFAULT * MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS;

    for(uint32_t i = 0U; i <= 256U; ++i)
    {
        const float x = 1.0f + ((float)i * (1.0f / 256.0f));
        g_mixer_filter_log2_lut[i] = logf(x) * MIXER_FILTER_INV_LOG2;
    }

    for(uint32_t i = 0U; i <= MIXER_FILTER_TIME_LUT_SIZE; ++i)
    {
        const float norm = (float)i * (1.0f / (float)MIXER_FILTER_TIME_LUT_SIZE);
        const float time_s = MIXER_FILTER_ATTACK_MIN_S * expf(time_log_ratio * norm);
        const float desired_samples = clampf_local(time_s * MIXER_FILTER_SAMPLE_RATE_DEFAULT, 1.0f, max_segment_samples);
        const float normalized = cbrtf((desired_samples - 1.0f) / max_segment_samples);
        const float scaled = clampf_local(normalized, 0.0f, 1.0f) * 65535.0f;
        g_mixer_filter_time_lut[i] = (uint16_t)(scaled + 0.5f);
    }

    g_mixer_filter_time_lut_ready = 1U;
}

static float mixer_track_filter_log2_lut(float x)
{
    union
    {
        float f;
        uint32_t u;
    } bits;

    bits.f = clampf_local(x, 1.0e-20f, 1.0e20f);
    const int32_t exponent = (int32_t)((bits.u >> 23) & 0xffU) - 127;
    const uint32_t mantissa_bits = bits.u & 0x7fffffU;
    uint32_t index = mantissa_bits >> 15;
    if(index >= 256U)
        index = 255U;

    const float frac = (float)(mantissa_bits & 0x7fffU) * (1.0f / 32768.0f);
    const float a = g_mixer_filter_log2_lut[index];
    const float b = g_mixer_filter_log2_lut[index + 1U];
    return (float)exponent + a + ((b - a) * frac);
}

static uint16_t mixer_track_filter_time_lut_lookup(float time_s)
{
    const float clamped = clampf_local(time_s, MIXER_FILTER_ATTACK_MIN_S, MIXER_FILTER_ATTACK_MAX_S);
    float pos = (mixer_track_filter_log2_lut(clamped) - MIXER_FILTER_LOG2_MIN_TIME)
                * MIXER_FILTER_INV_LOG2_TIME_RANGE
                * (float)MIXER_FILTER_TIME_LUT_SIZE;
    if(pos <= 0.0f)
        return g_mixer_filter_time_lut[0];
    if(pos >= (float)MIXER_FILTER_TIME_LUT_SIZE)
        return g_mixer_filter_time_lut[MIXER_FILTER_TIME_LUT_SIZE];

    const uint32_t index = (uint32_t)pos;
    const float frac = pos - (float)index;
    const float a = (float)g_mixer_filter_time_lut[index];
    const float b = (float)g_mixer_filter_time_lut[index + 1U];
    return (uint16_t)(a + ((b - a) * frac) + 0.5f);
}

static float mixer_track_filter_resonance_to_biquad_q(float resonance)
{
    const float r = clampf_local(resonance, 0.0f, 1.0f);
    const float distributed = r * (0.35f + (0.65f * r));
    return 0.70710678f + ((6.5f - 0.70710678f) * distributed);
}

static uint16_t mixer_track_filter_time_s_to_peaks(float time_s, float sample_rate)
{
    (void)sample_rate;
    return mixer_track_filter_time_lut_lookup(time_s);
}

static uint16_t mixer_track_filter_sustain_to_peaks(float sustain)
{
    const float clamped = clampf_local(sustain, 0.0f, 1.0f);
    return (uint16_t)(clamped * 32767.0f + 0.5f);
}

static float mixer_track_filter_keytrack_ratio(const mixer_track_filter_t *filter)
{
    if (filter == NULL)
    {
        return 1.0f;
    }
    const float semitones =
        ((float)((int32_t)filter->current_note - (int32_t)MIXER_FILTER_NOTE_REF_MIDI))
        * clampf_local(filter->keytrack, 0.0f, 1.0f);
    return exp2f(semitones * (1.0f / 12.0f));
}

static float mixer_track_filter_compute_modulated_cutoff(const mixer_track_filter_t *filter,
                                                         float base_hz,
                                                         float modulation_hz,
                                                         float keytrack_ratio,
                                                         float env)
{
    float cutoff_hz = (base_hz + modulation_hz) * keytrack_ratio;
    cutoff_hz = clampf_local(cutoff_hz,
                             MIXER_FILTER_CUTOFF_MIN_HZ,
                             MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->eg_amount == 0.0f)
    {
        return cutoff_hz;
    }
    const float env_value = clampf_local(env, 0.0f, 1.0f);
    if(filter->eg_amount >= 0.0f)
    {
        cutoff_hz += (MIXER_FILTER_CUTOFF_MAX_HZ - cutoff_hz)
                   * filter->eg_amount * env_value;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MIXER_FILTER_CUTOFF_MIN_HZ)
                   * filter->eg_amount * env_value;
    }
    return clampf_local(cutoff_hz,
                        MIXER_FILTER_CUTOFF_MIN_HZ,
                        MIXER_FILTER_CUTOFF_MAX_HZ);
}

static uint8_t mixer_track_filter_env_control_is_static(
    const mixer_track_filter_t *filter,
    uint32_t frames)
{
    if ((filter == NULL) || (filter->eg_amount == 0.0f))
    {
        return 1U;
    }

    const uint8_t prepared =
        (uint8_t)((filter->filter_env_prepared_consumed == 0U)
            && (filter->filter_env_prepared_frames == frames)
            && (filter->filter_env_prepared_count != 0U));
    if (prepared != 0U)
    {
        const int16_t reference = filter->filter_env_prepared_first[0];
        for (uint8_t i = 0U; i < filter->filter_env_prepared_count; ++i)
        {
            if ((filter->filter_env_prepared_first[i] != reference)
                    || (filter->filter_env_prepared_terminal[i] != reference))
            {
                return 0U;
            }
        }
        return 1U;
    }

    const env_adsr_peaks_stage_t stage = env_adsr_stage(&filter->filter_env);
    return (uint8_t)((stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        || (stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN));
}

static float mixer_track_filter_static_env_value(mixer_track_filter_t *filter,
                                                  uint32_t frames)
{
    const uint8_t prepared =
        (uint8_t)((filter->filter_env_prepared_consumed == 0U)
            && (filter->filter_env_prepared_frames == frames)
            && (filter->filter_env_prepared_count != 0U));
    int16_t value = env_adsr_value(&filter->filter_env);
    if (prepared != 0U)
    {
        value = filter->filter_env_prepared_first[0];
    }
    else
    {
        value = env_adsr_process_advance(&filter->filter_env, frames, NULL);
        filter->filter_env_value = (float)value * (1.0f / 32767.0f);
    }
    filter->filter_env_prepared_consumed = 1U;
    return (float)value * (1.0f / 32767.0f);
}

static void mixer_track_filter_rebind_dsp_storage(mixer_track_filter_t *filter)
{
    (void)filter;
}

static void mixer_track_filter_convert_biquad_to_mono(mixer_track_filter_t *filter)
{
    const fx_biquad_filter_t *const stereo = &filter->biquad;
    fx_biquad_filter_mono_t mono = {0};
    mono.sample_rate = stereo->sample_rate;
    mono.cutoff_hz = stereo->cutoff_hz;
    mono.q = stereo->q;
    mono.current = stereo->current;
    mono.ic1eq = stereo->ic1eq_l;
    mono.ic2eq = stereo->ic2eq_l;
    mono.bypass_xfade_remaining = stereo->bypass_xfade_remaining;
    mono.bypass_mix = stereo->bypass_mix;
    mono.morph = stereo->morph;
    mono.morph_a = stereo->morph_a;
    mono.morph_b = stereo->morph_b;
    mono.morph_plan = stereo->morph_plan;
    mono.bypass = stereo->bypass;
    mono.reset_after_bypass = stereo->reset_after_bypass;
    filter->biquad_mono = mono;
}

static void mixer_track_filter_convert_biquad_to_stereo(mixer_track_filter_t *filter)
{
    const fx_biquad_filter_mono_t *const mono = &filter->biquad_mono;
    fx_biquad_filter_t stereo = {0};
    stereo.sample_rate = mono->sample_rate;
    stereo.cutoff_hz = mono->cutoff_hz;
    stereo.q = mono->q;
    stereo.current = mono->current;
    stereo.ic1eq_l = mono->ic1eq;
    stereo.ic2eq_l = mono->ic2eq;
    stereo.ic1eq_r = mono->ic1eq;
    stereo.ic2eq_r = mono->ic2eq;
    stereo.bypass_xfade_remaining = mono->bypass_xfade_remaining;
    stereo.bypass_mix = mono->bypass_mix;
    stereo.morph = mono->morph;
    stereo.morph_a = mono->morph_a;
    stereo.morph_b = mono->morph_b;
    stereo.morph_plan = mono->morph_plan;
    stereo.bypass = mono->bypass;
    stereo.reset_after_bypass = mono->reset_after_bypass;
    filter->biquad = stereo;
}

static void mixer_track_filter_set_dsp_format(mixer_track_filter_t *filter,
                                               mixer_filter_dsp_format_t format)
{
    if ((filter == NULL) || (filter->dsp_format == (uint8_t)format))
        return;

    if (format == MIXER_FILTER_DSP_MONO)
    {
        mixer_track_filter_convert_biquad_to_mono(filter);
    }
    else
    {
        mixer_track_filter_convert_biquad_to_stereo(filter);
    }
    filter->dsp_format = (uint8_t)format;
    mixer_track_filter_rebind_dsp_storage(filter);
}

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    if (filter->filter_mode == 2U)
    {
        fx_deluge_filter_configure(&filter->deluge, filter->morph,
                                   filter->cutoff_hz, filter->resonance);
        return;
    }

    const uint8_t enabled = (filter->filter_mode != 0U) ? 1U : 0U;
    if (filter->dsp_format == (uint8_t)MIXER_FILTER_DSP_MONO)
    {
        fx_biquad_filter_mono_set_sample_rate(&filter->biquad_mono, filter->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&filter->biquad_mono, filter->cutoff_hz);
        fx_biquad_filter_mono_set_q(
            &filter->biquad_mono,
            mixer_track_filter_resonance_to_biquad_q(filter->resonance));
        fx_biquad_filter_mono_set_morph(&filter->biquad_mono, filter->morph);
        fx_biquad_filter_mono_set_bypass(&filter->biquad_mono,
                                         (enabled != 0U) ? 0U : 1U);
        return;
    }

    fx_biquad_filter_set_cutoff(&filter->biquad, filter->cutoff_hz);
    fx_biquad_filter_set_q(
        &filter->biquad,
        mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    fx_biquad_filter_set_morph(&filter->biquad, filter->morph);
    fx_biquad_filter_set_bypass(&filter->biquad,
                                (enabled != 0U) ? 0U : 1U);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    filter->dsp_format = (uint8_t)MIXER_FILTER_DSP_STEREO;
    fx_biquad_filter_init(&filter->biquad, filter->sample_rate);
    fx_biquad_filter_reset(&filter->biquad);
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_init(mixer_track_filter_t *filter, float sample_rate)
{
    if(filter == NULL)
        return;

    filter->sample_rate = (sample_rate > 0.0f) ? sample_rate : MIXER_FILTER_SAMPLE_RATE_DEFAULT;
    filter->cutoff_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
    filter->cutoff_target_hz = filter->cutoff_hz;
    filter->cutoff_mod_hz = filter->cutoff_hz;
    filter->cutoff_mod_target_hz = filter->cutoff_hz;
    filter->resonance = 0.0f;
    filter->resonance_target = 0.0f;
    filter->eg_amount = 0.0f;
    filter->keytrack = 0.0f;
    filter->keytrack_ratio = 1.0f;
    filter->keytrack_ratio_target = 1.0f;
    filter->current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->morph = 0.0f;
    filter->morph_target = 0.0f;
    filter->note_active = 0U;
    filter->vca_enabled = 0U;
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->vca_gate = 0U;
    filter->filter_retrigger_hard = 1U;
    filter->vca_retrigger_hard = 1U;
    filter->vca_env_value = 0.0f;
    filter->filter_env_value = 0.0f;
    filter->filter_env_prepared_frames = 0U;
    filter->filter_env_prepared_count = 0U;
    filter->filter_env_prepared_consumed = 1U;
    filter->config_version = mixer_track_filter_next_config_version();

    env_adsr_init(&filter->filter_env, filter->sample_rate);
    env_adsr_set_attack(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.01f, filter->sample_rate));
    env_adsr_set_decay(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    env_adsr_set_sustain(&filter->filter_env, mixer_track_filter_sustain_to_peaks(1.0f));
    env_adsr_set_release(&filter->filter_env, mixer_track_filter_time_s_to_peaks(0.10f, filter->sample_rate));
    env_adsr_init(&filter->vca_env, filter->sample_rate);
    env_adsr_set_attack(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_set_decay(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_set_sustain(&filter->vca_env, mixer_track_filter_sustain_to_peaks(1.0f));
    env_adsr_set_release(&filter->vca_env, mixer_track_filter_time_s_to_peaks(0.001f, filter->sample_rate));
    env_adsr_reset(&filter->vca_env);
    vca_env_init(&filter->synth_vca_env, filter->sample_rate);
    vca_env_set_attack(&filter->synth_vca_env, 0.001f);
    vca_env_set_decay(&filter->synth_vca_env, 0.001f);
    vca_env_set_sustain(&filter->synth_vca_env, 1.0f);
    vca_env_set_release(&filter->synth_vca_env, 0.001f);
    filter->filter_mode = 0U;
    fx_deluge_filter_init(&filter->deluge, filter->sample_rate);

    mixer_track_filter_reset_dsp(filter);
}

static void mixer_track_state_reset(mixer_audio_track_runtime_t *track)
{
    if (track == NULL)
    {
        return;
    }

    track->gain = 1.0f;
    track->pan = 0.0f;
    track->gain_current = 1.0f;
    track->pan_current = 0.0f;
    track->mute_gain_current = 1.0f;
    track->mute = 0U;
    track->route_master = 1U;

    for (uint32_t i = 0U; i < MIXER_INSERTS_PER_TRACK; ++i)
    {
        track->insert_slot[i] = -1;
    }

    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level[s] = 0.0f;
        track->send_level_current[s] = 0.0f;
    }
    track->group_fx_level[0] = 0.0f;
    track->group_fx_level[1] = 0.0f;
    track->group_fx_level_current[0] = 0.0f;
    track->group_fx_level_current[1] = 0.0f;
}

static void mixer_external_input_clear_lane(uint32_t lane)
{
    if (lane >= MIXER_MAX_TRACKS)
    {
        return;
    }

    g_external_track_enabled[lane] = 0U;
    g_external_lane_mask &= ~(uint32_t)(1UL << lane);
    g_external_poly_initialized[lane] = 0U;
    g_external_track_format[lane] = MIXER_EXTERNAL_FORMAT_NONE;
    g_external_track_frames_valid[lane] = 0U;
}

void mixer_rebind_track_states(const uint8_t *previous_mix_tracks,
                               const uint8_t *next_mix_tracks,
                               uint32_t track_count)
{
    static mixer_audio_track_runtime_t previous_tracks[MIXER_MAX_TRACKS];
    static mixer_track_filter_t previous_filters[MIXER_MAX_TRACKS];

    if ((previous_mix_tracks == NULL) || (next_mix_tracks == NULL))
    {
        return;
    }

    memcpy(previous_tracks, g_tracks, sizeof(previous_tracks));
    memcpy(previous_filters, g_track_filters, sizeof(previous_filters));

    for (uint32_t lane = 0U; lane < MIXER_MAX_TRACKS; ++lane)
    {
        mixer_track_state_reset(&g_tracks[lane]);
        mixer_track_filter_init(&g_track_filters[lane], previous_filters[lane].sample_rate);
    }

    for (uint32_t track = 0U; track < track_count; ++track)
    {
        const uint8_t next_mix = next_mix_tracks[track];
        if (next_mix >= MIXER_MAX_TRACKS)
        {
            continue;
        }

        const uint8_t previous_mix = previous_mix_tracks[track];
        if (previous_mix >= MIXER_MAX_TRACKS)
        {
            continue;
        }

        g_tracks[next_mix] = previous_tracks[previous_mix];
        g_track_filters[next_mix] = previous_filters[previous_mix];
        mixer_track_filter_rebind_dsp_storage(&g_track_filters[next_mix]);
    }

    g_mixer_routing_audio_generation = 0U;
    mixer_external_inputs_clear();
    mixer_rebuild_static_plan();
}

void mixer_rebind_track_state(uint8_t previous_mix_track, uint8_t next_mix_track)
{
    mixer_audio_track_runtime_t previous_track = { 0 };
    mixer_track_filter_t previous_filter = { 0 };
    const uint8_t has_previous = (previous_mix_track < MIXER_MAX_TRACKS) ? 1U : 0U;
    const uint8_t has_next = (next_mix_track < MIXER_MAX_TRACKS) ? 1U : 0U;

    if ((has_previous != 0U) && (has_next != 0U) && (previous_mix_track == next_mix_track))
    {
        return;
    }

    if (has_previous != 0U)
    {
        previous_track = g_tracks[previous_mix_track];
        previous_filter = g_track_filters[previous_mix_track];
        mixer_track_state_reset(&g_tracks[previous_mix_track]);
        mixer_track_filter_init(&g_track_filters[previous_mix_track], previous_filter.sample_rate);
        mixer_external_input_clear_lane(previous_mix_track);
    }

    if (has_next != 0U)
    {
        const float sample_rate = (has_previous != 0U)
            ? previous_filter.sample_rate
            : g_track_filters[next_mix_track].sample_rate;
        mixer_track_state_reset(&g_tracks[next_mix_track]);
        mixer_track_filter_init(&g_track_filters[next_mix_track], sample_rate);
        mixer_external_input_clear_lane(next_mix_track);
    }

    if ((has_previous != 0U) && (has_next != 0U))
    {
        g_tracks[next_mix_track] = previous_track;
        g_track_filters[next_mix_track] = previous_filter;
        mixer_track_filter_rebind_dsp_storage(&g_track_filters[next_mix_track]);
        mixer_track_filter_all_notes_off(next_mix_track);
        mixer_track_vca_all_notes_off(next_mix_track);
    }

    if (has_next != 0U)
    {
        g_mixer_lane_rebind_count[next_mix_track]++;
    }
    else if (has_previous != 0U)
    {
        g_mixer_lane_rebind_count[previous_mix_track]++;
    }
    g_mixer_routing_audio_generation = 0U;
    mixer_rebuild_static_plan();
}

void mixer_snap_track_runtime_state(uint32_t track_id)
{
    if (track_id >= MIXER_MAX_TRACKS)
    {
        return;
    }

    mixer_audio_track_runtime_t *const track = &g_tracks[track_id];
    mixer_track_filter_t *const filter = &g_track_filters[track_id];

    track->gain_current = track->gain;
    track->pan_current = track->pan;
    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level_current[s] = track->send_level[s];
    }
    track->group_fx_level_current[0] = track->group_fx_level[0];
    track->group_fx_level_current[1] = track->group_fx_level[1];

    filter->cutoff_hz = filter->cutoff_target_hz;
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = filter->resonance_target;
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_apply_core_params(filter);
}

static ITCM_TEXT void mixer_track_filter_process_block(mixer_track_filter_t *filter,
                                             float *left,
                                             float *right,
                                             uint32_t frames)
{
    if((filter == NULL) || (left == NULL) || (right == NULL))
        return;

    if(filter->filter_mode != 2U)
        mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_STEREO);

    if(filter->filter_mode == 0U)
    {
        filter->filter_env_prepared_consumed = 1U;
        return;
    }

    const float cutoff_start_hz = filter->cutoff_hz;
    const float cutoff_mod_start_hz = filter->cutoff_mod_hz;
    const float resonance_start = filter->resonance;
    const float keytrack_ratio_start = filter->keytrack_ratio;
    filter->cutoff_hz = mixer_smooth_block(cutoff_start_hz,
                                           filter->cutoff_target_hz,
                                           MIXER_FILTER_BLOCK_SMOOTH);
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = mixer_smooth_block(resonance_start, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_process_biquad_stereo_block(filter, left, right, frames,
                                                   cutoff_start_hz, cutoff_mod_start_hz,
                                                   resonance_start, keytrack_ratio_start);
}

static void mixer_track_filter_process_biquad_stereo_block(mixer_track_filter_t *filter,
                                                           float *left,
                                                           float *right,
                                                           uint32_t frames,
                                                           float cutoff_start_hz,
                                                           float cutoff_mod_start_hz,
                                                           float resonance_start,
                                                           float keytrack_ratio_start)
{
    uint32_t i = 0U;
    const uint8_t filter_env_modulated = (filter->eg_amount != 0.0f) ? 1U : 0U;
    if ((filter->morph_ramp_remaining == 0U)
            && (mixer_track_filter_env_control_is_static(filter, frames) != 0U)
            && ((filter->filter_mode == 2U) || (filter->biquad.bypass_xfade_remaining == 0U)))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float modulation_hz = filter->cutoff_mod_hz
            - filter->cutoff_target_hz;
        const float cutoff = mixer_track_filter_compute_modulated_cutoff(filter,
            filter->cutoff_hz, modulation_hz, filter->keytrack_ratio, env);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff, filter->resonance);
            fx_deluge_filter_process(&filter->deluge, left, right, frames);
        }
        else
        {
            fx_biquad_filter_set_params(&filter->biquad, cutoff,
                mixer_track_filter_resonance_to_biquad_q(filter->resonance));
            fx_biquad_filter_process_block(&filter->biquad, left, right, frames);
        }
        return;
    }
    while(i < frames)
    {
        uint32_t chunk = frames - i;
        if(chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        int16_t terminal_value = env_adsr_value(&filter->filter_env);
        const uint8_t prepared =
            (uint8_t)((filter->filter_env_prepared_consumed == 0U)
                && (filter->filter_env_prepared_frames == frames)
                && ((i / MIXER_FILTER_UPDATE_PERIOD) < filter->filter_env_prepared_count));
        if (prepared != 0U)
        {
            first_value =
                filter->filter_env_prepared_first[i / MIXER_FILTER_UPDATE_PERIOD];
            terminal_value =
                filter->filter_env_prepared_terminal[i / MIXER_FILTER_UPDATE_PERIOD];
        }
        else
        {
            terminal_value =
                env_adsr_process_advance(&filter->filter_env, chunk, &first_value);
        }
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            const float env_first = (float)first_value * (1.0f / 32767.0f);
            const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
            env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
        }
        const float progress = (float)(i + chunk) / (float)frames;
        const float base_hz = cutoff_start_hz
            + ((filter->cutoff_hz - cutoff_start_hz) * progress);
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((filter->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = mod_absolute_hz - filter->cutoff_target_hz;
        const float keytrack_ratio = keytrack_ratio_start
            + ((filter->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance =
                resonance_start + ((filter->resonance - resonance_start) * progress);
        if (prepared == 0U)
        {
            filter->filter_env_value = (float)terminal_value * (1.0f / 32767.0f);
        }
        const float cutoff = mixer_track_filter_compute_modulated_cutoff(filter, base_hz,
                                                                          modulation_hz,
                                                                          keytrack_ratio, env);
        mixer_track_filter_advance_morph(filter, chunk);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff, resonance);
            fx_deluge_filter_process(&filter->deluge, &left[i], &right[i], chunk);
        }
        else
        {
            fx_biquad_filter_set_params(&filter->biquad, cutoff,
                mixer_track_filter_resonance_to_biquad_q(resonance));
            fx_biquad_filter_process_block(&filter->biquad, &left[i], &right[i], chunk);
        }
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static void mixer_track_filter_process_biquad_mono_block(mixer_track_filter_t *filter,
                                                         float *mono,
                                                         uint32_t frames,
                                                         float cutoff_start_hz,
                                                         float cutoff_mod_start_hz,
                                                         float resonance_start,
                                                         float keytrack_ratio_start,
                                                         const mixer_poly_cutoff_override_t *poly_cutoff)
{
    uint32_t i = 0U;
    const uint8_t filter_env_modulated = (filter->eg_amount != 0.0f) ? 1U : 0U;
    const uint8_t poly_cutoff_valid = (poly_cutoff != NULL) ? poly_cutoff->valid : 0U;
    const float poly_cutoff_hz = (poly_cutoff_valid != 0U)
        ? poly_cutoff->effective_hz : 0.0f;
    const uint8_t control_static = mixer_track_filter_env_control_is_static(filter, frames);
    if ((filter->morph_ramp_remaining == 0U) && (control_static != 0U)
            && ((filter->filter_mode == 2U) || (filter->biquad_mono.bypass_xfade_remaining == 0U)))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float base_hz = (poly_cutoff_valid != 0U)
            ? poly_cutoff_hz : filter->cutoff_hz;
        const float modulation_hz = (poly_cutoff_valid != 0U)
            ? 0.0f : (filter->cutoff_mod_hz - filter->cutoff_target_hz);
        const float cutoff_arg = mixer_track_filter_compute_modulated_cutoff(filter,
            base_hz, modulation_hz, filter->keytrack_ratio, env);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff_arg, filter->resonance);
            fx_deluge_filter_process_mono(&filter->deluge, mono, frames);
        }
        else
        {
            const float q_arg = mixer_track_filter_resonance_to_biquad_q(filter->resonance);
            fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);
            fx_biquad_filter_mono_process_block(&filter->biquad_mono, mono, frames);
        }
        return;
    }
    while(i < frames)
    {
        uint32_t chunk = frames - i;
        if(chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        int16_t terminal_value = env_adsr_value(&filter->filter_env);
        const uint8_t prepared =
            (uint8_t)((filter->filter_env_prepared_consumed == 0U)
                && (filter->filter_env_prepared_frames == frames)
                && ((i / MIXER_FILTER_UPDATE_PERIOD) < filter->filter_env_prepared_count));
        if (prepared != 0U)
        {
            first_value =
                filter->filter_env_prepared_first[i / MIXER_FILTER_UPDATE_PERIOD];
            terminal_value =
                filter->filter_env_prepared_terminal[i / MIXER_FILTER_UPDATE_PERIOD];
        }
        else
        {
            terminal_value =
                env_adsr_process_advance(&filter->filter_env, chunk, &first_value);
        }
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            const float env_first = (float)first_value * (1.0f / 32767.0f);
            const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
            env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
        }
        const float progress = (float)(i + chunk) / (float)frames;
        const float base_hz = (poly_cutoff_valid != 0U)
            ? poly_cutoff_hz
            : (cutoff_start_hz + ((filter->cutoff_hz - cutoff_start_hz) * progress));
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((filter->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = (poly_cutoff_valid != 0U)
            ? 0.0f : (mod_absolute_hz - filter->cutoff_target_hz);
        const float keytrack_ratio = keytrack_ratio_start
            + ((filter->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance =
                resonance_start + ((filter->resonance - resonance_start) * progress);
        if (prepared == 0U)
        {
            filter->filter_env_value = (float)terminal_value * (1.0f / 32767.0f);
        }
        const float cutoff_arg = mixer_track_filter_compute_modulated_cutoff(
            filter, base_hz, modulation_hz, keytrack_ratio, env);
        mixer_track_filter_advance_morph(filter, chunk);
        if(filter->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&filter->deluge,
                filter->morph,
                cutoff_arg, resonance);
            fx_deluge_filter_process_mono(&filter->deluge, &mono[i], chunk);
        }
        else
        {
            const float q_arg = mixer_track_filter_resonance_to_biquad_q(resonance);
            fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);
            fx_biquad_filter_mono_process_block(&filter->biquad_mono, &mono[i], chunk);
        }
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static mixer_lane_plan_t mixer_build_lane_plan(uint32_t track_id,
                                               const mixer_audio_track_runtime_t *track,
                                               const mixer_track_filter_t *filter,
                                               uint8_t hw_enabled,
                                               uint8_t ext_enabled,
                                               uint8_t ext_format,
                                               uint32_t ext_frames,
                                               uint32_t frames)
{
    mixer_lane_plan_t plan;
    memset(&plan, 0, sizeof(plan));

    (void)track_id;
    plan.hw_enabled = hw_enabled;
    plan.ext_enabled = ext_enabled;
    plan.ext_format = ext_format;
    plan.ext_frames = ext_frames;

    if ((track == NULL) || (filter == NULL))
    {
        return plan;
    }

    if (((hw_enabled == 0U) && (ext_enabled == 0U))
            || ((track->mute != 0U) && (track->mute_gain_current <= 0.000001f)))
    {
        return plan;
    }

    if ((ext_enabled != 0U)
            && ((ext_frames != frames)
                || ((ext_format != MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                    && (ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO))))
    {
        plan.ext_enabled = 0U;
        plan.ext_format = MIXER_EXTERNAL_FORMAT_NONE;
        plan.ext_frames = 0U;
    }

    if (((plan.hw_enabled == 0U) && (plan.ext_enabled == 0U))
            || ((track->mute != 0U) && (track->mute_gain_current <= 0.000001f)))
    {
        return plan;
    }

    plan.active = 1U;
    plan.exec_kind = MIXER_LANE_EXEC_STEREO;

    if (plan.hw_enabled != 0U)
    {
        if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)))
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO;
        }
        else if (plan.ext_enabled != 0U)
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO;
        }
        else
        {
            plan.source_kind = MIXER_LANE_SOURCE_HW_STEREO;
        }

        return plan;
    }

    if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_STEREO)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_STEREO)))
    {
        plan.source_kind = MIXER_LANE_SOURCE_EXT_STEREO;
        return plan;
    }

    if ((plan.ext_enabled != 0U)
            && ((plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                || (plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)))
    {
        plan.source_kind = MIXER_LANE_SOURCE_EXT_MONO_NATIVE;
        plan.exec_kind = MIXER_LANE_EXEC_MONO_NATIVE;
    }

    return plan;
}

typedef struct
{
    float *mono;
    float *left;
    float *right;
} mixer_lane_buffers_t;

static mixer_lane_buffers_t mixer_lane_prepare_stereo_buffers(uint32_t track_id,
                                                              const mixer_lane_plan_t *plan,
                                                              StereoTrack *tracks)
{
    mixer_lane_buffers_t buffers = {0};

    if ((plan == NULL) || (tracks == NULL))
    {
        return buffers;
    }

    if ((plan->source_kind == MIXER_LANE_SOURCE_HW_STEREO)
            || (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO)
            || (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO))
    {
        buffers.left = tracks[track_id].L;
        buffers.right = tracks[track_id].R;
        return buffers;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_EXT_STEREO)
    {
        buffers.left = g_external_track_l[track_id];
        buffers.right = g_external_track_r[track_id];
    }

    return buffers;
}

static ITCM_TEXT void mixer_lane_accumulate_external_source(uint32_t track_id,
                                                  const mixer_lane_plan_t *plan,
                                                  float *left,
                                                  float *right)
{
    if ((plan == NULL) || (left == NULL) || (right == NULL))
    {
        return;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_MONO)
    {
        for (uint32_t i = 0U; i < plan->ext_frames; ++i)
        {
            const float s = g_external_track_mono[track_id][i];
            left[i] += s;
            right[i] += s;
        }
        return;
    }

    if (plan->source_kind == MIXER_LANE_SOURCE_HW_PLUS_EXT_STEREO)
    {
        for (uint32_t i = 0U; i < plan->ext_frames; ++i)
        {
            left[i] += g_external_track_l[track_id][i];
            right[i] += g_external_track_r[track_id][i];
        }
    }
}

static ITCM_TEXT mixer_lane_buffers_t mixer_lane_run_mono_native_path(uint32_t track_id,
                                                            const mixer_audio_track_runtime_t *track,
                                                            mixer_track_filter_t *filter,
                                                            uint32_t frames)
{
    mixer_lane_buffers_t buffers = {0};

    if ((track == NULL) || (filter == NULL))
    {
        return buffers;
    }

    (void)mixer_track_filter_process_block_mono(filter, g_external_track_mono[track_id], frames, NULL);

    buffers.mono = g_external_track_mono[track_id];
    return buffers;
}

static ITCM_TEXT void mixer_lane_run_stereo_path(uint32_t track_id,
                                       const mixer_audio_track_runtime_t *track,
                                       mixer_track_filter_t *filter,
                                       float *left,
                                       float *right,
                                       uint32_t frames)
{
    if ((track == NULL) || (filter == NULL) || (left == NULL) || (right == NULL))
    {
        return;
    }

    mixer_track_filter_process_block(filter, left, right, frames);
}

static uint8_t ITCM_TEXT mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
                                                     float *mono,
                                                     uint32_t frames,
                                                     const mixer_poly_cutoff_override_t *poly_cutoff)
{
    if((filter == NULL) || (mono == NULL))
    {
        return 0U;
    }

    if(filter->filter_mode != 2U)
        mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_MONO);

    if(filter->filter_mode == 0U)
    {
        filter->filter_env_prepared_consumed = 1U;
        return 1U;
    }

    const float cutoff_start_hz = filter->cutoff_hz;
    const float cutoff_mod_start_hz = filter->cutoff_mod_hz;
    const float resonance_start = filter->resonance;
    const float keytrack_ratio_start = filter->keytrack_ratio;
    filter->cutoff_hz = mixer_smooth_block(cutoff_start_hz,
                                           filter->cutoff_target_hz,
                                           MIXER_FILTER_BLOCK_SMOOTH);
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = mixer_smooth_block(resonance_start, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    mixer_track_filter_process_biquad_mono_block(filter, mono, frames,
                                                 cutoff_start_hz,
                                                 cutoff_mod_start_hz,
                                                 resonance_start,
                                                 keytrack_ratio_start,
                                                 poly_cutoff);

    return 1U;
}

static void mixer_multi_filter_apply_core_params(multi_voice_dsp_slot_t *slot)
{
    if (slot == NULL)
    {
        return;
    }

    if (slot->filter_mode == 2U)
    {
        fx_deluge_filter_configure(&slot->deluge, slot->morph,
                                   slot->cutoff_hz, slot->resonance);
        return;
    }

    const uint8_t enabled = (slot->filter_mode != 0U) ? 1U : 0U;
    if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
    {
        fx_biquad_filter_set_sample_rate(&slot->filter.stereo.biquad, slot->sample_rate);
        fx_biquad_filter_set_cutoff(&slot->filter.stereo.biquad, slot->cutoff_hz);
        fx_biquad_filter_set_q(&slot->filter.stereo.biquad,
                               mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        fx_biquad_filter_set_morph(&slot->filter.stereo.biquad, slot->morph);
        fx_biquad_filter_set_bypass(&slot->filter.stereo.biquad,
                                    (enabled != 0U) ? 0U : 1U);
    }
    else
    {
        fx_biquad_filter_mono_set_sample_rate(&slot->filter.mono.biquad,
                                              slot->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&slot->filter.mono.biquad, slot->cutoff_hz);
        fx_biquad_filter_mono_set_q(&slot->filter.mono.biquad,
                                    mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        fx_biquad_filter_mono_set_morph(&slot->filter.mono.biquad, slot->morph);
        fx_biquad_filter_mono_set_bypass(&slot->filter.mono.biquad,
                                         (enabled != 0U) ? 0U : 1U);
    }
}

static void mixer_multi_filter_sync_config(uint32_t track_id,
                                           multi_voice_dsp_slot_t *slot)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (slot == NULL))
    {
        return;
    }

    const mixer_track_filter_t *const source = &g_track_filters[track_id];
    const uint8_t previous_mode = slot->filter_mode;
    slot->cutoff_target_hz = source->cutoff_target_hz;
    slot->cutoff_mod_target_hz = source->cutoff_mod_target_hz;
    slot->resonance_target = source->resonance_target;
    slot->eg_amount = source->eg_amount;
    slot->keytrack = source->keytrack;
    if (slot->morph_target != source->morph_target) {
        slot->morph_target = source->morph_target;
        slot->morph_step = (slot->morph_target - slot->morph) * (1.0f / 64.0f);
        slot->morph_ramp_remaining = 64U;
    }
    slot->filter_mode = source->filter_mode;
    slot->filter_retrigger_hard = source->filter_retrigger_hard;
    if (slot->filter_env.attack != source->filter_env.attack)
        env_adsr_set_attack(&slot->filter_env, source->filter_env.attack);
    if (slot->filter_env.decay != source->filter_env.decay)
        env_adsr_set_decay(&slot->filter_env, source->filter_env.decay);
    if (slot->filter_env.sustain != source->filter_env.sustain)
        env_adsr_set_sustain(&slot->filter_env, source->filter_env.sustain);
    if (slot->filter_env.release != source->filter_env.release)
        env_adsr_set_release(&slot->filter_env, source->filter_env.release);
    if (slot->vca_env.attack != source->vca_env.attack)
        env_adsr_set_attack(&slot->vca_env, source->vca_env.attack);
    if (slot->vca_env.decay != source->vca_env.decay)
        env_adsr_set_decay(&slot->vca_env, source->vca_env.decay);
    if (slot->vca_env.sustain != source->vca_env.sustain)
        env_adsr_set_sustain(&slot->vca_env, source->vca_env.sustain);
    if (slot->vca_env.release != source->vca_env.release)
        env_adsr_set_release(&slot->vca_env, source->vca_env.release);
    slot->vca_retrigger_hard = source->vca_retrigger_hard;
    slot->filter_config_version = source->config_version;
    if(previous_mode != slot->filter_mode)
        fx_deluge_filter_init(&slot->deluge, slot->sample_rate);
    if((previous_mode != slot->filter_mode) && (slot->filter_mode == 1U))
    {
        mixer_multi_filter_apply_core_params(slot);
    }
}

static float mixer_multi_filter_compute_modulated_cutoff(
    const multi_voice_dsp_slot_t *slot,
    float base_hz,
    float modulation_hz,
    float keytrack_ratio,
    float env)
{
    float cutoff_hz = (base_hz + modulation_hz) * keytrack_ratio;
    cutoff_hz = clampf_local(cutoff_hz,
                             MIXER_FILTER_CUTOFF_MIN_HZ,
                             MIXER_FILTER_CUTOFF_MAX_HZ);
    if (slot->eg_amount == 0.0f)
    {
        return cutoff_hz;
    }
    const float env_value = clampf_local(env, 0.0f, 1.0f);
    if (slot->eg_amount >= 0.0f)
    {
        cutoff_hz += (MIXER_FILTER_CUTOFF_MAX_HZ - cutoff_hz)
                   * slot->eg_amount * env_value;
    }
    else
    {
        cutoff_hz += (cutoff_hz - MIXER_FILTER_CUTOFF_MIN_HZ)
                   * slot->eg_amount * env_value;
    }
    return clampf_local(cutoff_hz,
                        MIXER_FILTER_CUTOFF_MIN_HZ,
                        MIXER_FILTER_CUTOFF_MAX_HZ);
}

static void mixer_multi_filter_advance_morph(multi_voice_dsp_slot_t *slot, uint32_t frames)
{
    if ((slot == NULL) || (slot->morph_ramp_remaining == 0U)) return;
    if (frames >= slot->morph_ramp_remaining) {
        slot->morph = slot->morph_target;
        slot->morph_ramp_remaining = 0U;
    } else {
        slot->morph += slot->morph_step * (float)frames;
        slot->morph_ramp_remaining = (uint8_t)(slot->morph_ramp_remaining - frames);
    }
    if (slot->filter_mode == 1U) {
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
            fx_biquad_filter_set_morph(&slot->filter.stereo.biquad, slot->morph);
        else
            fx_biquad_filter_mono_set_morph(&slot->filter.mono.biquad, slot->morph);
    }
}

static void mixer_multi_filter_process_biquad(multi_voice_dsp_slot_t *slot,
                                               float *left,
                                               float *right,
                                               uint32_t frames)
{
    const float cutoff_start_hz = slot->cutoff_hz;
    const float cutoff_mod_start_hz = slot->cutoff_mod_hz;
    const float resonance_start = slot->resonance;
    const float keytrack_ratio_start = slot->keytrack_ratio;
    const uint8_t filter_env_modulated = (slot->eg_amount != 0.0f) ? 1U : 0U;
    const env_adsr_peaks_stage_t env_stage = env_adsr_stage(&slot->filter_env);
    const uint8_t env_static = (uint8_t)((filter_env_modulated == 0U)
        || (env_stage == ENV_ADSR_PEAKS_STAGE_IDLE)
        || (env_stage == ENV_ADSR_PEAKS_STAGE_SUSTAIN));
    const uint8_t transition_active = 0U;
    if ((slot->morph_ramp_remaining == 0U) && (env_static != 0U) && (transition_active == 0U))
    {
        int16_t first_value = 0;
        const int16_t terminal_value =
            env_adsr_process_advance(&slot->filter_env, frames, &first_value);
        const float env = (filter_env_modulated != 0U)
            ? ((float)first_value * (1.0f / 32767.0f)) : 0.0f;
        const float cutoff = mixer_multi_filter_compute_modulated_cutoff(
            slot,
            slot->cutoff_hz,
            slot->cutoff_mod_hz - slot->cutoff_target_hz,
            slot->keytrack_ratio,
            env);
        (void)terminal_value;
        if(slot->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&slot->deluge,
                slot->morph, cutoff, slot->resonance);
            if(slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
                fx_deluge_filter_process(&slot->deluge, left, right, frames);
            else
                fx_deluge_filter_process_mono(&slot->deluge, left, frames);
            return;
        }
        const float q = mixer_track_filter_resonance_to_biquad_q(slot->resonance);
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
        {
            fx_biquad_filter_set_params(&slot->filter.stereo.biquad, cutoff, q);
            fx_biquad_filter_process_block(&slot->filter.stereo.biquad,
                                           left,
                                           right,
                                           frames);
        }
        else
        {
            fx_biquad_filter_mono_set_params(&slot->filter.mono.biquad, cutoff, q);
            fx_biquad_filter_mono_process_block(&slot->filter.mono.biquad,
                                                left,
                                                frames);
        }
        return;
    }
    uint32_t offset = 0U;
    while (offset < frames)
    {
        uint32_t chunk = frames - offset;
        if (chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }

        int16_t first_value = 0;
        const int16_t terminal_value =
            env_adsr_process_advance(&slot->filter_env, chunk, &first_value);
        float env = 0.0f;
        if (filter_env_modulated != 0U)
        {
            env = 0.5f * ((float)first_value + (float)terminal_value)
                * (1.0f / 32767.0f);
        }
        const float progress = (float)(offset + chunk) / (float)frames;
        const float base_hz = cutoff_start_hz
            + ((slot->cutoff_hz - cutoff_start_hz) * progress);
        const float mod_absolute_hz = cutoff_mod_start_hz
            + ((slot->cutoff_mod_hz - cutoff_mod_start_hz) * progress);
        const float modulation_hz = mod_absolute_hz - slot->cutoff_target_hz;
        const float keytrack_ratio = keytrack_ratio_start
            + ((slot->keytrack_ratio - keytrack_ratio_start) * progress);
        const float resonance = resonance_start
            + ((slot->resonance - resonance_start) * progress);
        const float cutoff = mixer_multi_filter_compute_modulated_cutoff(
            slot,
            base_hz,
            modulation_hz,
            keytrack_ratio,
            env);
        mixer_multi_filter_advance_morph(slot, chunk);
        if(slot->filter_mode == 2U)
        {
            fx_deluge_filter_configure(&slot->deluge,
                slot->morph, cutoff, resonance);
            if(slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
                fx_deluge_filter_process(&slot->deluge, &left[offset], &right[offset], chunk);
            else
                fx_deluge_filter_process_mono(&slot->deluge, &left[offset], chunk);
            offset += chunk;
            continue;
        }
        const float q = mixer_track_filter_resonance_to_biquad_q(resonance);
        if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
        {
            fx_biquad_filter_set_params(&slot->filter.stereo.biquad, cutoff, q);
            fx_biquad_filter_process_block(&slot->filter.stereo.biquad,
                                           &left[offset],
                                           &right[offset],
                                           chunk);
        }
        else
        {
            fx_biquad_filter_mono_set_params(&slot->filter.mono.biquad, cutoff, q);
            fx_biquad_filter_mono_process_block(&slot->filter.mono.biquad,
                                                &left[offset],
                                                chunk);
        }
        offset += chunk;
    }
}

void mixer_multi_filter_note_on(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                uint8_t midi_note)
{
    if (slot == NULL)
    {
        return;
    }

    mixer_multi_filter_sync_config(track_id, slot);
    slot->current_note = midi_note;
    const float semitones = ((float)((int32_t)midi_note
                                     - (int32_t)MIXER_FILTER_NOTE_REF_MIDI))
                          * clampf_local(slot->keytrack, 0.0f, 1.0f);
    slot->keytrack_ratio_target = exp2f(semitones * (1.0f / 12.0f));
    env_adsr_retrigger(&slot->filter_env, slot->filter_retrigger_hard != 0U);
    slot->vca_enabled = 1U;
    slot->vca_gate = 1U;
    env_adsr_retrigger(&slot->vca_env, slot->vca_retrigger_hard != 0U);
}

void mixer_multi_filter_note_off(struct multi_voice_dsp_slot_t *slot)
{
    if (slot != NULL)
    {
        env_adsr_gate_off(&slot->filter_env);
        slot->vca_gate = 0U;
        env_adsr_gate_off(&slot->vca_env);
    }
}

uint8_t mixer_multi_voice_vca_requires_source(
    const struct multi_voice_dsp_slot_t *slot)
{
    if ((slot == NULL) || (slot->vca_enabled == 0U))
    {
        return 0U;
    }
    return (env_adsr_stage(&slot->vca_env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}

static void mixer_multi_filter_prepare_block(uint32_t track_id,
                                             multi_voice_dsp_slot_t *slot)
{
    mixer_multi_filter_sync_config(track_id, slot);
    slot->cutoff_hz = mixer_smooth_block(slot->cutoff_hz,
                                         slot->cutoff_target_hz,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->cutoff_mod_hz = mixer_smooth_block(slot->cutoff_mod_hz,
                                             slot->cutoff_mod_target_hz,
                                             MIXER_FILTER_BLOCK_SMOOTH);
    slot->resonance = mixer_smooth_block(slot->resonance,
                                         slot->resonance_target,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->keytrack_ratio = mixer_smooth_block(slot->keytrack_ratio,
                                              slot->keytrack_ratio_target,
                                              MIXER_FILTER_BLOCK_SMOOTH);
}

void mixer_multi_filter_prepare_voice_block(uint32_t track_id,
                                            struct multi_voice_dsp_slot_t *slot)
{
    mixer_multi_filter_prepare_block(track_id, slot);
}

void mixer_multi_filter_set_voice_cutoff(multi_voice_dsp_slot_t *slot, float cutoff_hz)
{
    if (slot != NULL)
    {
        /* The kernel consumes cutoff_mod_hz as an absolute modulation
         * value relative to the current prepared base. Encode the
         * voice-local absolute cutoff so that
         * base + (mod - target) == cutoff_hz. */
        slot->cutoff_mod_hz = cutoff_hz
            - slot->cutoff_hz
            + slot->cutoff_target_hz;
        slot->cutoff_mod_target_hz = slot->cutoff_mod_hz;
    }
}

void mixer_multi_filter_set_voice_resonance(multi_voice_dsp_slot_t *slot, float resonance)
{
    if (slot != NULL)
    {
        slot->resonance = resonance;
        slot->resonance_target = resonance;
    }
}

void mixer_multi_filter_set_voice_eg_amount(multi_voice_dsp_slot_t *slot, float amount)
{
    if (slot != NULL) slot->eg_amount = amount;
}

void mixer_multi_filter_set_voice_env_attack(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_attack(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_env_decay(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_decay(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_env_sustain(multi_voice_dsp_slot_t *slot, float sustain)
{
    if (slot != NULL) env_adsr_set_sustain(&slot->filter_env,
        mixer_track_filter_sustain_to_peaks(sustain));
}

void mixer_multi_filter_set_voice_env_release(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_release(&slot->filter_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_attack(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_attack(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_decay(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_decay(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_set_voice_vca_sustain(multi_voice_dsp_slot_t *slot, float sustain)
{
    if (slot != NULL) env_adsr_set_sustain(&slot->vca_env,
        mixer_track_filter_sustain_to_peaks(sustain));
}

void mixer_multi_filter_set_voice_vca_release(multi_voice_dsp_slot_t *slot, float seconds)
{
    if (slot != NULL) env_adsr_set_release(&slot->vca_env,
        mixer_track_filter_time_s_to_peaks(seconds, slot->sample_rate));
}

void mixer_multi_filter_process_prepared(uint32_t track_id,
                                         struct multi_voice_dsp_slot_t *slot,
                                         float *left,
                                         float *right,
                                         uint32_t frames)
{
    if ((slot == NULL) || (left == NULL) || (right == NULL) || (frames == 0U))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    (void)track_id;

    if(slot->filter_mode != 0U)
        mixer_multi_filter_process_biquad(slot, left, right, frames);

}

void mixer_multi_filter_process(uint32_t track_id,
                                struct multi_voice_dsp_slot_t *slot,
                                float *left,
                                float *right,
                                uint32_t frames)
{
    mixer_multi_filter_prepare_block(track_id, slot);
    mixer_multi_filter_process_prepared(track_id, slot, left, right, frames);
}

void mixer_multi_filter_process_mono_prepared(uint32_t track_id,
                                              struct multi_voice_dsp_slot_t *slot,
                                              float *mono,
                                              uint32_t frames)
{
    if ((slot == NULL) || (mono == NULL) || (frames == 0U)
        || (slot->format != (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO))
    {
        return;
    }
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    (void)track_id;
    if(slot->filter_mode != 0U)
        mixer_multi_filter_process_biquad(slot, mono, NULL, frames);

}

void mixer_multi_filter_process_mono(uint32_t track_id,
                                     struct multi_voice_dsp_slot_t *slot,
                                     float *mono,
                                     uint32_t frames)
{
    mixer_multi_filter_prepare_block(track_id, slot);
    mixer_multi_filter_process_mono_prepared(track_id, slot, mono, frames);
}

/**
 * @brief Point d'entrée clamp01.
 *
 * Role:
 * - Exécuter le traitement associé à clamp01.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp01(float v)
{
    if(v < 0.0f) return 0.0f;
    if(v > 1.0f) return 1.0f;
    return v;
}

/**
 * @brief Point d'entrée clamp_pan.
 *
 * Role:
 * - Exécuter le traitement associé à clamp_pan.
 *
 * @param pan Parametre d'entree de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp_pan(float pan)
{
    if(pan < -1.0f) return -1.0f;
    if(pan > 1.0f) return 1.0f;
    return pan;
}

static inline void mixer_advance_track_ramps(float *gain_cur,
                                             float gain_step,
                                             float *pan_cur,
                                             float pan_step,
                                             float *mute_gain_cur,
                                             float mute_target,
                                             float mute_step)
{
    *gain_cur += gain_step;
    *pan_cur += pan_step;
    if (*mute_gain_cur < mute_target)
    {
        *mute_gain_cur += mute_step;
        if (*mute_gain_cur > mute_target) *mute_gain_cur = mute_target;
    }
    else if (*mute_gain_cur > mute_target)
    {
        *mute_gain_cur -= mute_step;
        if (*mute_gain_cur < mute_target) *mute_gain_cur = mute_target;
    }
}

typedef struct
{
    float pan_l;
    float pan_r;
    float gain_l;
    float gain_r;
    float mono_gain;
    uint8_t stable;
} mixer_track_coefficient_plan_t;

static ITCM_TEXT mixer_track_coefficient_plan_t mixer_prepare_track_coefficients(
    float gain_current,
    float gain_target,
    float pan_current,
    float pan_target,
    float mute_current,
    float mute_target)
{
    mixer_track_coefficient_plan_t plan = {0};
    plan.stable = (uint8_t)((gain_current == gain_target)
        && (pan_current == pan_target)
        && (mute_current == mute_target));
    if (plan.stable == 0U)
    {
        return plan;
    }

    const float pan_for_mix = -pan_current;
    plan.pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    plan.pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    plan.gain_l = gain_current * plan.pan_l * mute_current;
    plan.gain_r = gain_current * plan.pan_r * mute_current;
    plan.mono_gain = gain_current * mute_current;
    return plan;
}

static inline void mixer_track_coefficients_at(
    const mixer_track_coefficient_plan_t *plan,
    float gain,
    float pan,
    float mute_gain,
    float *gain_l,
    float *gain_r)
{
    if (plan->stable != 0U)
    {
        *gain_l = plan->gain_l;
        *gain_r = plan->gain_r;
        return;
    }
    const float pan_for_mix = -pan;
    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    *gain_l = gain * pan_l * mute_gain;
    *gain_r = gain * pan_r * mute_gain;
}

/**
 * @brief Initialise l'état interne du mixer.
 *
 * Contexte d'appel:
 * - Init application, hors IRQ.
 */
/**
 * @brief Point d'entrée mixer_init.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static void mixer_reverb_state_reset_defaults(void)
{
    g_reverb.wet = 0.0f;
    g_reverb.room_size = 0.60f;
    g_reverb.damping = 0.72f;
    g_reverb.width = 1.0f;
    g_reverb.hpf = 0.0f;
    g_reverb.lpf = 1.0f;
    g_delay_spectral_position = 0.50f;
    g_delay_spectral_width = 1.0f;
}

static void mixer_apply_delay_spectral_window(void)
{
    spectral_window_result_t result;
    spectral_window_calculate(g_delay_spectral_position,
                              g_delay_spectral_width,
                              spectral_window_delay_limits(),
                              &result);
    fx_delay_stereo_global_set_filter_hz(result.low_cut_hz, result.high_cut_hz);
    fx_delay_dual_global_set_filter_hz(result.low_cut_hz, result.high_cut_hz);
}

void mixer_reset_runtime_state(void)
{
    g_mixer_routing_audio_generation = 0U;
    mixer_reverb_state_reset_defaults();
    fx_reverb_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    fx_reverb_global_set_wet(g_reverb.wet);
    fx_reverb_global_set_room_size(g_reverb.room_size);
    fx_reverb_global_set_damping(g_reverb.damping);
    fx_reverb_global_set_width(g_reverb.width);
    fx_reverb_global_set_hpf(g_reverb.hpf);
    fx_reverb_global_set_lpf(g_reverb.lpf);
    fx_delay_stereo_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    fx_delay_dual_global_init(MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    mixer_apply_delay_spectral_window();
    g_delay_type = (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
    fx_delay_stereo_global_clear();
    audio_xfade_set(0.0f);
    g_looper_xfade_smoothed = 0.0f;
    g_looper_xfade_prev = 0.0f;

    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
        g_tracks[t].gain = 1.0f;
        g_tracks[t].pan = 0.0f;
        g_tracks[t].gain_current = 1.0f;
        g_tracks[t].pan_current = 0.0f;
        g_tracks[t].mute_gain_current = 1.0f;
        g_tracks[t].mute = 0U;

        g_tracks[t].route_master = 1U;

        for(uint32_t i = 0; i < MIXER_INSERTS_PER_TRACK; i++)
            g_tracks[t].insert_slot[i] = -1;

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            g_tracks[t].send_level[s] = 0.0f;
            g_tracks[t].send_level_current[s] = 0.0f;
        }
        g_tracks[t].group_fx_level[0] = 0.0f;
        g_tracks[t].group_fx_level[1] = 0.0f;
        g_tracks[t].group_fx_level_current[0] = 0.0f;
        g_tracks[t].group_fx_level_current[1] = 0.0f;

        mixer_track_filter_init(&g_track_filters[t], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

    }

    for (uint32_t i = 0U; i < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET; ++i)
    {
        mixer_track_filter_init(&g_poly_filters_hot[i], MIXER_FILTER_SAMPLE_RATE_DEFAULT);
        g_poly_cutoff_override[i].effective_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
        g_poly_cutoff_override[i].valid = 0U;
    }

    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        g_send_fx_slot[s] = -1;

    mixer_external_inputs_clear();
}

void mixer_init(void)
{
    audio_rec_bus_projection_audio_init();
    audio_rec_level_snapshot_audio_init();
    mixer_track_filter_init_time_lut();
    fx_modfx_global_init();
    mixer_reset_runtime_state();
}

/**
 * @brief Définit le gain master global.
 *
 * @param gain Gain linéaire master.
 */
/**
 * @brief Point d'entrée mixer_set_master.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_master.
 *
 * @param gain Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_master(float gain)
{
    audio_float_set_master_gain(gain);
}

/**
 * @brief Lit le gain master courant.
 *
 * @return Gain master linéaire.
 */
/**
 * @brief Point d'entrée mixer_get_master.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_get_master.
 *
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float mixer_get_master(void)
{
    return audio_float_get_master_gain();
}

/**
 * @brief Point d'entrée mixer_set_track_gain.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_gain.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param gain Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_gain(uint32_t track_id, float gain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    if(gain < 0.0f)
        gain = 0.0f;

    g_tracks[track_id].gain = gain;
}

void mixer_set_track_group_fx_level(uint32_t track_id,
                                    uint32_t slot,
                                    float level)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (slot >= 2U))
        return;
    g_tracks[track_id].group_fx_level[slot] =
        (level <= 0.0f) ? 0.0f : (level >= 1.0f) ? 1.0f : level;
}

/**
 * @brief Point d'entrée mixer_get_track_gain.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_get_track_gain.
 *
 * @param track_id Parametre d'entree de l'API.
 *
 * @return Valeur de retour definie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
float mixer_get_track_gain(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return g_tracks[track_id].gain;
}

/**
 * @brief Point d'entrée mixer_set_track_pan.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_pan.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param pan Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_pan(uint32_t track_id, float pan)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].pan = clamp_pan(pan);
}

/**
 * @brief Point d'entrée mixer_set_track_mute.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_mute.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param mute Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_mute(uint32_t track_id, uint8_t mute)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    g_tracks[track_id].mute = mute ? 1U : 0U;
}

uint8_t mixer_get_track_mute(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0U;

    return g_tracks[track_id].mute;
}

/**
 * @brief Frontière de publication du routing track.
 *
 * Role:
 * - Le routing CONTROL est publié puis appliqué côté AUDIO au début du bloc.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param route Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Frontière de publication des inserts track.
 *
 * Role:
 * - Les inserts CONTROL sont publiés puis appliqués côté AUDIO au début du bloc.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param insert_idx Paramètre d'entrée de l'API.
 * @param slot Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */

/**
 * @brief Point d'entrée mixer_set_track_send_level.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_track_send_level.
 *
 * @param track_id Parametre d'entree de l'API.
 * @param send_idx Parametre d'entree de l'API.
 * @param level Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_send_level(uint32_t track_id, uint32_t send_idx, float level)
{
    if(track_id >= MIXER_MAX_TRACKS || send_idx >= MIXER_NUM_SENDS)
        return;

    g_tracks[track_id].send_level[send_idx] = clamp01(level);
}

/**
 * @brief Point d'entrée mixer_set_send_fx_slot.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_set_send_fx_slot.
 *
 * @param send_idx Parametre d'entree de l'API.
 * @param slot Parametre d'entree de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_send_fx_slot(uint32_t send_idx, int8_t slot)
{
    if(send_idx >= MIXER_NUM_SENDS)
        return;

    if(send_idx == MIXER_DELAY_SEND_INDEX)
    {
        /* Send2 is owned by the dedicated global delay path; legacy slot state is kept inactive. */
        g_send_fx_slot[send_idx] = -1;
        return;
    }

    g_send_fx_slot[send_idx] = slot;
}

void mixer_set_reverb_wet(float wet)
{
    g_reverb.wet = clamp01(wet);
    fx_reverb_global_set_wet(g_reverb.wet);
}

void mixer_set_reverb_room_size(float room_size)
{
    g_reverb.room_size = clamp01(room_size);
    fx_reverb_global_set_room_size(g_reverb.room_size);
}

void mixer_set_reverb_damping(float damping)
{
    g_reverb.damping = clamp01(damping);
    fx_reverb_global_set_damping(g_reverb.damping);
}

void mixer_set_reverb_width(float width)
{
    g_reverb.width = clamp01(width);
    fx_reverb_global_set_width(g_reverb.width);
}

void mixer_set_reverb_hpf(float hpf)
{
    g_reverb.hpf = clamp01(hpf);
    fx_reverb_global_set_hpf(g_reverb.hpf);
}

void mixer_set_reverb_lpf(float lpf)
{
    g_reverb.lpf = clamp01(lpf);
    fx_reverb_global_set_lpf(g_reverb.lpf);
}

void mixer_set_reverb_delays(uint8_t tbd)
{
    fx_reverb_global_set_delay_mode(tbd);
}

void mixer_set_delay_type(uint8_t type)
{
    const uint8_t next = (type != 0U) ? (uint8_t)MIXER_DELAY_TYPE_DUAL : (uint8_t)MIXER_DELAY_TYPE_CLASSIC;
    if(next == g_delay_type)
        return;

    g_delay_type = next;
    if(g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
    {
        fx_delay_dual_global_clear();
    }
    else
    {
        fx_delay_stereo_global_clear();
    }
}

void mixer_set_delay_mode(uint8_t mode)
{
    fx_delay_dual_global_set_mode(mode);
}

void mixer_set_delay_time(float time_s)
{
    fx_delay_stereo_global_set_time(time_s);
    fx_delay_dual_global_set_time_l(time_s);
}

void mixer_set_delay_time_r(float time_s)
{
    fx_delay_dual_global_set_time_r(time_s);
}

void mixer_set_delay_feedback(float feedback)
{
    fx_delay_stereo_global_set_feedback(feedback);
    fx_delay_dual_global_set_feedback(feedback);
}

void mixer_set_delay_spectral_position(float position)
{
    g_delay_spectral_position = clamp01(position);
    mixer_apply_delay_spectral_window();
}

void mixer_set_delay_spectral_width(float width)
{
    g_delay_spectral_width = clamp01(width);
    mixer_apply_delay_spectral_window();
}

void mixer_set_delay_pingpong(uint8_t enabled)
{
    fx_delay_stereo_global_set_pingpong(enabled);
}

void mixer_set_delay_width(float width)
{
    fx_delay_stereo_global_set_width(width);
    fx_delay_dual_global_set_width(width);
}

void mixer_set_delay_feedback_width(float width)
{
    fx_delay_dual_global_set_feedback_width(width);
}

void mixer_set_delay_mod_depth(float depth)
{
    fx_delay_dual_global_set_mod_depth(depth);
}

void mixer_set_delay_mod_rate(float rate_hz)
{
    fx_delay_dual_global_set_mod_rate(rate_hz);
}

void mixer_set_delay_reverb_send(float reverb_send)
{
    g_delay_diag_reverb_send = clamp01(reverb_send);
    fx_delay_stereo_global_set_reverb_send(reverb_send);
    fx_delay_dual_global_set_reverb_send(reverb_send);
}

void mixer_set_delay_volume(float volume)
{
    g_delay_diag_volume = clamp01(volume);
    fx_delay_stereo_global_set_volume(volume);
    fx_delay_dual_global_set_volume(volume);
}

void mixer_get_global_diag_state(mixer_global_diag_state_t *out)
{
    if(out == NULL)
        return;
    out->reverb_active = fx_reverb_global_is_active();
    out->delay_active = (g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
        ? fx_delay_dual_global_is_active() : fx_delay_stereo_global_is_active();
    out->delay_type = g_delay_type;
    out->_pad = 0U;
    out->reverb_wet = g_reverb.wet;
    out->delay_volume = g_delay_diag_volume;
    out->delay_reverb_send = g_delay_diag_reverb_send;
}

void mixer_set_track_filter_morph(uint32_t track_id, float morph)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float next = clampf_local(morph, 0.0f, 127.0f);
    if(filter->morph_target == next) return;
    filter->morph_target = next;
    filter->morph_step = (next - filter->morph) * (1.0f / 64.0f);
    filter->morph_ramp_remaining = 64U;
    filter->filter_env_prepared_consumed = 1U;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_MORPH);
}

void mixer_set_track_filter_cutoff(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float target_hz = clampf_local(cutoff_hz,
                                         MIXER_FILTER_CUTOFF_MIN_HZ,
                                         MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->cutoff_target_hz == target_hz) return;
    const float delta = target_hz - filter->cutoff_target_hz;
    filter->cutoff_target_hz = target_hz;
    filter->cutoff_mod_hz += delta;
    filter->cutoff_mod_target_hz += delta;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_CUTOFF);
}

void mixer_set_track_filter_cutoff_modulated(uint32_t track_id, float cutoff_hz)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;
    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(cutoff_hz,
                                    MIXER_FILTER_CUTOFF_MIN_HZ,
                                    MIXER_FILTER_CUTOFF_MAX_HZ);
    if (filter->cutoff_mod_target_hz == next) return;
    filter->cutoff_mod_target_hz = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_CUTOFF);
}

void mixer_set_track_filter_resonance(uint32_t track_id, float resonance)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float next = clampf_local(resonance, 0.0f, 1.0f);
    if (filter->resonance_target == next) return;
    filter->resonance_target = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_RESONANCE);
}

void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(eg_amount, -1.0f, 1.0f);
    if (filter->eg_amount == next) return;
    filter->eg_amount = next;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_EG_AMOUNT);
}

void mixer_set_track_filter_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(attack_s, filter->sample_rate);
    if (filter->filter_env.attack == next) return;
    env_adsr_set_attack(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_ATTACK);
}

void mixer_set_track_filter_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(decay_s, filter->sample_rate);
    if (filter->filter_env.decay == next) return;
    env_adsr_set_decay(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_DECAY);
}

void mixer_set_track_filter_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_peaks(sustain);
    if (filter->filter_env.sustain == next) return;
    env_adsr_set_sustain(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_SUSTAIN);
}

void mixer_set_track_filter_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(release_s, filter->sample_rate);
    if (filter->filter_env.release == next) return;
    env_adsr_set_release(&filter->filter_env, next);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_FILTER_RELEASE);
}

void mixer_set_track_filter_keytrack(uint32_t track_id, float amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(amount, 0.0f, 1.0f);
    if (filter->keytrack == next) return;
    filter->keytrack = next;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_env_reset(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    (void)enabled;
}

void mixer_set_track_filter_env_delay(uint32_t track_id, float delay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    (void)delay_s;
}

void mixer_set_track_filter_retrigger_hard(uint32_t track_id, uint8_t hard)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (hard != 0U) ? 1U : 0U;
    if (filter->filter_retrigger_hard == next) return;
    filter->filter_retrigger_hard = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_track_filter_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->current_note = midi_note;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env, filter->filter_retrigger_hard != 0U);
}

void mixer_set_track_vca_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(attack_s, filter->sample_rate);
    const float synth_next = (attack_s < 0.0f) ? 0.0f : attack_s;
    if ((filter->vca_env.attack == next)
            && (filter->synth_vca_env.attack_time == synth_next))
        return;
    if (filter->vca_env.attack != next)
        env_adsr_set_attack(&filter->vca_env, next);
    vca_env_set_attack(&filter->synth_vca_env, attack_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_ATTACK);
}

void mixer_set_track_vca_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(decay_s, filter->sample_rate);
    const float synth_next = (decay_s < 0.0f) ? 0.0f : decay_s;
    if ((filter->vca_env.decay == next)
            && (filter->synth_vca_env.decay_time == synth_next))
        return;
    if (filter->vca_env.decay != next)
        env_adsr_set_decay(&filter->vca_env, next);
    vca_env_set_decay(&filter->synth_vca_env, decay_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_DECAY);
}

void mixer_set_track_vca_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_peaks(sustain);
    const float synth_next = (sustain <= 0.0f) ? 0.0f
                           : ((sustain >= 1.0f) ? 1.0f : sustain);
    if ((filter->vca_env.sustain == next)
            && (filter->synth_vca_env.sustain == synth_next))
        return;
    if (filter->vca_env.sustain != next)
        env_adsr_set_sustain(&filter->vca_env, next);
    vca_env_set_sustain(&filter->synth_vca_env, sustain);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_SUSTAIN);
}

void mixer_set_track_vca_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(release_s, filter->sample_rate);
    const float synth_next = (release_s < 0.0f) ? 0.0f : release_s;
    if ((filter->vca_env.release == next)
            && (filter->synth_vca_env.release_time == synth_next))
        return;
    if (filter->vca_env.release != next)
        env_adsr_set_release(&filter->vca_env, next);
    vca_env_set_release(&filter->synth_vca_env, release_s);
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_VCA_RELEASE);
}

void mixer_set_track_filter_mode(uint32_t track_id, uint8_t mode)
{
    if (track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (mode > 2U) ? 2U : mode;
    if (filter->filter_mode == next)
        return;
    filter->filter_mode = next;
    if(next == 2U) fx_deluge_filter_init(&filter->deluge, filter->sample_rate);
    else mixer_track_filter_reset_dsp(filter);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_vca_enabled(uint32_t track_id, uint8_t enabled)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (enabled != 0U) ? 1U : 0U;
    const uint8_t changed = (filter->vca_enabled != next) ? 1U : 0U;
    filter->vca_enabled = next;
    if (changed != 0U) mixer_track_filter_touch_config(filter);
    if (enabled == 0U)
    {
        filter->vca_note_active = 0U;
        filter->vca_note_count = 0U;
        filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
        filter->vca_gate = 0U;
        filter->vca_env_value = 0.0f;
        env_adsr_reset(&filter->vca_env);
    }
}

void mixer_set_track_vca_retrigger_hard(uint32_t track_id, uint8_t hard)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (hard != 0U) ? 1U : 0U;
    if (filter->vca_retrigger_hard == next) return;
    filter->vca_retrigger_hard = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_track_vca_note_on(uint32_t track_id, uint8_t midi_note, uint8_t velocity)
{
    (void)velocity;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->vca_enabled = 1U;
    filter->vca_current_note = midi_note;
    if (filter->vca_note_count < 0xFFU)
    {
        filter->vca_note_count++;
    }
    if (filter->vca_note_active == 0U)
    {
        filter->vca_note_active = 1U;
        filter->vca_gate = 1U;
        env_adsr_retrigger(&filter->vca_env, filter->vca_retrigger_hard != 0U);
    }
}

void mixer_track_vca_note_off(uint32_t track_id, uint8_t midi_note)
{
    (void)midi_note;

    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if((filter->vca_enabled == 0U) || (filter->vca_note_active == 0U))
        return;

    if (filter->vca_note_count > 0U)
    {
        filter->vca_note_count--;
    }
    if (filter->vca_note_count == 0U)
    {
        filter->vca_note_active = 0U;
        filter->vca_gate = 0U;
        env_adsr_gate_off(&filter->vca_env);
    }
}

void mixer_track_vca_all_notes_off(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->vca_gate = 0U;
    filter->vca_env_value = 0.0f;
    env_adsr_reset(&filter->vca_env);
}

uint8_t mixer_track_vca_is_running(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0U;

    const mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (filter->vca_enabled == 0U)
        return 0U;

    return (env_adsr_stage(&filter->vca_env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}

uint8_t mixer_track_vca_requires_source(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 1U;

    const mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (filter->vca_enabled == 0U)
        return 1U;

    return (env_adsr_stage(&filter->vca_env) != ENV_ADSR_PEAKS_STAGE_IDLE) ? 1U : 0U;
}

float mixer_get_track_vca_env_value(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return clampf_local(g_track_filters[track_id].vca_env_value, 0.0f, 1.0f);
}

float mixer_get_track_filter_env_value(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return 0.0f;

    return clampf_local(g_track_filters[track_id].filter_env_value, 0.0f, 1.0f);
}

float mixer_prepare_track_filter_env_source(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (frames == 0U))
    {
        return 0.0f;
    }

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    const int16_t segment_first = env_adsr_value(&filter->filter_env);
    uint32_t offset = 0U;
    uint8_t count = 0U;
    int16_t terminal = segment_first;
    while ((offset < frames) && (count < (AUDIO_BLOCK_SIZE / MIXER_FILTER_UPDATE_PERIOD)))
    {
        uint32_t chunk = frames - offset;
        if (chunk > MIXER_FILTER_UPDATE_PERIOD)
        {
            chunk = MIXER_FILTER_UPDATE_PERIOD;
        }
        int16_t first = 0;
        terminal = env_adsr_process_advance(&filter->filter_env, chunk, &first);
        filter->filter_env_prepared_first[count] = first;
        filter->filter_env_prepared_terminal[count] = terminal;
        count++;
        offset += chunk;
    }

    filter->filter_env_value = (float)terminal * (1.0f / 32767.0f);
    filter->filter_env_prepared_frames = (uint16_t)frames;
    filter->filter_env_prepared_count = count;
    filter->filter_env_prepared_consumed = 0U;
    return clampf_local((float)segment_first * (1.0f / 32767.0f), 0.0f, 1.0f);
}

void mixer_track_filter_note_off(uint32_t track_id, uint8_t midi_note)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if(filter->note_active == 0U || filter->current_note != midi_note)
        return;

    filter->note_active = 0U;
    env_adsr_gate_off(&filter->filter_env);
}

void mixer_track_filter_all_notes_off(uint32_t track_id)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    filter->note_active = 0U;
    filter->current_note = MIXER_FILTER_NOTE_REF_MIDI;
    filter->keytrack_ratio_target = 1.0f;
    filter->filter_env_value = 0.0f;
    env_adsr_reset(&filter->filter_env);
}

void __attribute__((used)) mixer_external_inputs_clear(void)
{
    g_external_lane_mask = 0U;
    memset(g_external_track_enabled, 0, sizeof(g_external_track_enabled));
    memset(g_external_poly_initialized, 0, sizeof(g_external_poly_initialized));
    memset(g_external_track_format, 0, sizeof(g_external_track_format));
    memset(g_external_track_frames_valid, 0, sizeof(g_external_track_frames_valid));
}

void __attribute__((used)) mixer_submit_external_mono_native(uint32_t track_id, const float *mono, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (mono == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if (g_external_track_enabled[track_id] != 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_external_track_mono[track_id][i] = mono[i];
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MONO_NATIVE;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

void __attribute__((used)) mixer_submit_external_stereo(uint32_t track_id,
                                                        const float *left,
                                                        const float *right,
                                                        uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS) || (left == NULL) || (right == NULL))
    {
        return;
    }

    if (frames > AUDIO_BLOCK_SIZE)
    {
        frames = AUDIO_BLOCK_SIZE;
    }

    if (g_external_track_enabled[track_id] != 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < frames; ++i)
    {
        g_external_track_l[track_id][i] = left[i];
        g_external_track_r[track_id][i] = right[i];
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_mono_native(uint32_t track_id,
                                                               uint32_t frames,
                                                               float **out_mono)
{
    if (out_mono != NULL)
    {
        *out_mono = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_mono == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_mono = g_external_track_mono[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_mono_native(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MONO_NATIVE;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_multi_mono(uint32_t track_id,
                                                              uint32_t frames,
                                                              float **out_mono)
{
    if (out_mono != NULL)
    {
        *out_mono = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_mono == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_mono = g_external_track_mono[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_multi_mono(uint32_t track_id,
                                                            uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_MONO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_stereo(uint32_t track_id,
                                                          uint32_t frames,
                                                          float **out_left,
                                                          float **out_right)
{
    if (out_left != NULL)
    {
        *out_left = NULL;
    }
    if (out_right != NULL)
    {
        *out_right = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_left == NULL)
            || (out_right == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_left = g_external_track_l[track_id];
    *out_right = g_external_track_r[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_stereo(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t __attribute__((used)) mixer_begin_external_multi_stereo(uint32_t track_id,
                                                                uint32_t frames,
                                                                float **out_left,
                                                                float **out_right)
{
    if (out_left != NULL)
    {
        *out_left = NULL;
    }
    if (out_right != NULL)
    {
        *out_right = NULL;
    }

    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (out_left == NULL)
            || (out_right == NULL)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return 0U;
    }

    *out_left = g_external_track_l[track_id];
    *out_right = g_external_track_r[track_id];
    return 1U;
}

void __attribute__((used)) mixer_commit_external_multi_stereo(uint32_t track_id,
                                                              uint32_t frames)
{
    if ((track_id >= MIXER_MAX_TRACKS)
            || (frames == 0U)
            || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
    {
        return;
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

uint8_t mixer_begin_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
        return 0U;
    g_external_poly_initialized[track_id] = 0U;
    return 1U;
}

ITCM_TEXT uint8_t mixer_process_external_poly_voice_prepared(uint32_t mix_track_id,
                                                   uint32_t poly_track_id,
                                                   uint8_t voice,
                                                   float *mono,
                                                   uint32_t frames,
                                                   float voice_pan)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (mix_track_id >= MIXER_MAX_TRACKS) || (mono == NULL)
            || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE))
        return 0U;

    const uint8_t voice_slot = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
    const mixer_poly_cutoff_override_t *const poly_cutoff =
        (voice_slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            ? &g_poly_cutoff_override[voice_slot] : NULL;
    (void)mixer_track_filter_process_block_mono(filter, mono, frames, poly_cutoff);
    const float clamped_pan = clamp_pan(voice_pan);
    const float pan_for_mix = -clamped_pan;
    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    float *unit_output = g_external_track_l[mix_track_id];
    float *attenuated_output = g_external_track_r[mix_track_id];
    float attenuated_pan = pan_r;
    if (clamped_pan < 0.0f)
    {
        unit_output = g_external_track_r[mix_track_id];
        attenuated_output = g_external_track_l[mix_track_id];
        attenuated_pan = pan_l;
    }
    uint32_t i = 0U;
    const uint8_t poly_initialized =
        g_external_poly_initialized[mix_track_id];
    float vca_gain[AUDIO_BLOCK_SIZE];
    const uint32_t vca_frames = vca_env_process_block(&filter->synth_vca_env,
                                                       vca_gain,
                                                       frames);
    for (; i < vca_frames; ++i)
    {
        const float vca = vca_gain[i];
        filter->vca_env_value = vca;
        const float scaled = mono[i] * vca;
        const float attenuated = scaled * attenuated_pan;
        if (poly_initialized == 0U)
        {
            attenuated_output[i] = attenuated;
            unit_output[i] = scaled;
        }
        else
        {
            attenuated_output[i] += attenuated;
            unit_output[i] += scaled;
        }
    }
    if (poly_initialized == 0U)
    {
        if (i < frames)
        {
            memset(&g_external_track_l[mix_track_id][i],
                   0,
                   (frames - i) * sizeof(float));
            memset(&g_external_track_r[mix_track_id][i],
                   0,
                   (frames - i) * sizeof(float));
        }
        g_external_poly_initialized[mix_track_id] = 1U;
    }
    if (i < frames)
    {
        memset(&mono[i], 0, (frames - i) * sizeof(float));
        filter->vca_env_value = 0.0f;
    }
    return (vca_env_stage(&filter->synth_vca_env) != VCA_ENV_IDLE);
}

uint8_t mixer_process_external_poly_voice(uint32_t mix_track_id,
                                          uint32_t poly_track_id,
                                          uint8_t voice,
                                          float *mono,
                                          uint32_t frames,
                                          float voice_pan)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (mix_track_id >= MIXER_MAX_TRACKS)) return 0U;
    mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    return mixer_process_external_poly_voice_prepared(mix_track_id, poly_track_id,
                                                      voice, mono, frames, voice_pan);
}

void mixer_prepare_external_poly_voice(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter != NULL) && (mix_track_id < MIXER_MAX_TRACKS))
    {
        mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    }
}

void mixer_invalidate_external_poly_track(uint32_t poly_track_id)
{
    for (uint8_t voice = 0U; voice < SYNTH_POLYPHONY_MAX_VOICES; ++voice)
    {
        mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
        if (filter != NULL)
        {
            filter->config_version = UINT32_MAX;
            filter->continuous_epoch = UINT32_MAX;
            const uint8_t slot = synth_polyphony_get_slot((uint8_t)poly_track_id, voice);
            if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
                g_poly_cutoff_override[slot].valid = 0U;
        }
    }
}

void mixer_poly_voice_set_cutoff(uint8_t slot, float value)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        g_poly_cutoff_override[slot].effective_hz = value;
        g_poly_cutoff_override[slot].valid = 1U;
    }
}
void mixer_poly_voice_set_resonance(uint8_t slot, float value)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        g_poly_filters_hot[slot].resonance = value;
        g_poly_filters_hot[slot].resonance_target = value;
    }
}
void mixer_poly_voice_set_eg_amount(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) g_poly_filters_hot[slot].eg_amount = value; }
void mixer_poly_voice_set_filter_attack(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_attack(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_peaks(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_filter_decay(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_decay(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_peaks(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_filter_sustain(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_sustain(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_sustain_to_peaks(value)); }
void mixer_poly_voice_set_filter_release(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) env_adsr_set_release(&g_poly_filters_hot[slot].filter_env, mixer_track_filter_time_s_to_peaks(value, g_poly_filters_hot[slot].sample_rate)); }
void mixer_poly_voice_set_vca_attack(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_attack(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_decay(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_decay(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_sustain(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_sustain(&g_poly_filters_hot[slot].synth_vca_env, value); }
void mixer_poly_voice_set_vca_release(uint8_t slot, float value)
{ if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET) vca_env_set_release(&g_poly_filters_hot[slot].synth_vca_env, value); }

void mixer_commit_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U)
            || (g_external_poly_initialized[track_id] == 0U))
        return;
    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_POLY_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
    g_external_lane_mask |= (uint32_t)(1UL << track_id);
}

void mixer_track_poly_note_on(uint32_t poly_track_id,
                              uint32_t mix_track_id,
                              uint8_t voice,
                              uint8_t note,
                              uint8_t velocity)
{
    (void)velocity;
    if (mix_track_id >= MIXER_MAX_TRACKS)
        return;
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if (filter == NULL)
        return;
    mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    filter->current_note = note;
    filter->keytrack_ratio_target = mixer_track_filter_keytrack_ratio(filter);
    filter->note_active = 1U;
    env_adsr_retrigger(&filter->filter_env, filter->filter_retrigger_hard != 0U);
    filter->vca_current_note = note;
    filter->vca_note_count = 1U;
    filter->vca_note_active = 1U;
    filter->vca_gate = 1U;
    vca_env_retrigger(&filter->synth_vca_env,
                      filter->vca_retrigger_hard != 0U);
}

void mixer_track_poly_note_off(uint32_t poly_track_id, uint8_t voice, uint8_t note)
{
    mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
    if ((filter == NULL) || (filter->current_note != note))
        return;
    filter->note_active = 0U;
    env_adsr_gate_off(&filter->filter_env);
    filter->vca_note_active = 0U;
    filter->vca_note_count = 0U;
    filter->vca_gate = 0U;
    vca_env_gate_off(&filter->synth_vca_env);
}

void mixer_track_poly_all_notes_off(uint32_t poly_track_id)
{
    for (uint8_t voice = 0U; voice < SYNTH_POLYPHONY_MAX_VOICES; ++voice)
    {
        mixer_track_filter_t *const filter = mixer_poly_filter(poly_track_id, voice);
        if (filter != NULL)
        {
            filter->note_active = 0U;
            filter->vca_note_active = 0U;
            filter->vca_note_count = 0U;
            filter->vca_gate = 0U;
            env_adsr_gate_off(&filter->filter_env);
            vca_env_gate_off(&filter->synth_vca_env);
        }
    }
}

void mixer_synth_voice_slot_reset(uint8_t slot)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        mixer_track_filter_init(&g_poly_filters_hot[slot], MIXER_FILTER_SAMPLE_RATE_DEFAULT);
        g_poly_cutoff_override[slot].effective_hz = MIXER_FILTER_CUTOFF_MAX_HZ;
        g_poly_cutoff_override[slot].valid = 0U;
    }
}

static void mixer_copy_voice_envelope_state(mixer_track_filter_t *destination,
                                            const mixer_track_filter_t *source)
{
    destination->filter_env = source->filter_env;
    destination->vca_env = source->vca_env;
    destination->synth_vca_env = source->synth_vca_env;
    destination->vca_env_value = source->vca_env_value;
    destination->filter_env_value = source->filter_env_value;
    destination->note_active = source->note_active;
    destination->current_note = source->current_note;
    destination->vca_note_active = source->vca_note_active;
    destination->vca_note_count = source->vca_note_count;
    destination->vca_current_note = source->vca_current_note;
    destination->vca_gate = source->vca_gate;
}

#define MIXER_SEND_MASK_BIT(send_index) ((uint8_t)(1U << (send_index)))

typedef struct
{
    uint8_t count;
    uint8_t assign_mask;
    uint8_t index[MIXER_NUM_SENDS];
    float current[MIXER_NUM_SENDS];
    float step[MIXER_NUM_SENDS];
} mixer_send_plan_t;

static inline uint8_t mixer_send_destination_process_mask(uint8_t reverb_active,
                                                          uint8_t delay_active,
                                                          uint8_t modfx_active)
{
    uint8_t mask = 0U;
    if ((reverb_active != 0U) || (g_send_fx_slot[MIXER_REVERB_SEND_INDEX] >= 0))
        mask |= MIXER_SEND_MASK_BIT(MIXER_REVERB_SEND_INDEX);
    if (delay_active != 0U)
        mask |= MIXER_SEND_MASK_BIT(MIXER_DELAY_SEND_INDEX);
    if (modfx_active != 0U)
        mask |= MIXER_SEND_MASK_BIT(MIXER_MODFX_SEND_INDEX);
    return mask;
}

static inline uint8_t mixer_track_input_send_mask(
    const mixer_audio_track_runtime_t *track,
    uint8_t destination_mask)
{
    uint8_t mask = 0U;
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        const uint8_t bit = MIXER_SEND_MASK_BIT(send);
        if (((destination_mask & bit) != 0U)
                && ((track->send_level_current[send] != 0.0f)
                    || (track->send_level[send] != 0.0f)))
        {
            mask |= bit;
        }
    }
    return mask;
}

static uint8_t mixer_collect_input_send_masks(
    uint32_t lane_mask,
    uint8_t group_active,
    uint8_t destination_mask,
    uint8_t lane_send_masks[MIXER_MAX_TRACKS],
    uint8_t *group_send_mask)
{
    uint8_t mask = 0U;
    while (lane_mask != 0U)
    {
        const uint32_t lane = (uint32_t)__builtin_ctz(lane_mask);
        lane_mask &= lane_mask - 1U;
        const uint8_t lane_mask_value =
            ((group_active != 0U)
                && ((g_mixer_static_lane_flags[lane]
                    & MIXER_STATIC_GROUP_CHILD) != 0U))
            ? 0U
            : mixer_track_input_send_mask(&g_tracks[lane], destination_mask);
        lane_send_masks[lane] = lane_mask_value;
        mask |= lane_mask_value;
    }
    if (group_active != 0U)
    {
        *group_send_mask = mixer_track_input_send_mask(
            &g_tracks[MIXER_GROUP_BUS_TRACK], destination_mask);
        mask |= *group_send_mask;
    }
    return mask;
}

static inline mixer_send_plan_t mixer_prepare_send_plan(
    const mixer_audio_track_runtime_t *track,
    uint8_t lane_send_mask,
    uint8_t first_writer_mask,
    float inv_frames)
{
    mixer_send_plan_t plan = {0};
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        if ((lane_send_mask & MIXER_SEND_MASK_BIT(send)) == 0U)
            continue;

        const uint8_t plan_index = plan.count++;
        const float current = track->send_level_current[send];
        const float target = track->send_level[send];
        plan.index[plan_index] = (uint8_t)send;
        if ((first_writer_mask & MIXER_SEND_MASK_BIT(send)) != 0U)
            plan.assign_mask |= MIXER_SEND_MASK_BIT(plan_index);
        plan.current[plan_index] = current;
        if (target != current)
            plan.step[plan_index] = (target - current) * inv_frames;
    }
    return plan;
}

static inline void mixer_finish_send_levels(mixer_audio_track_runtime_t *track)
{
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
        track->send_level_current[send] = track->send_level[send];
}

static inline uint8_t mixer_group_fx_input_mask(
    const mixer_audio_track_runtime_t *track)
{
    uint8_t mask = 0U;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        if ((track->group_fx_level_current[slot] != 0.0f)
                || (track->group_fx_level[slot] != 0.0f))
            mask |= (uint8_t)(1U << slot);
    }
    return mask;
}

typedef struct
{
    float current[2];
    float step[2];
    uint8_t mask;
    uint8_t assign_mask;
} mixer_group_fx_plan_t;

static inline mixer_group_fx_plan_t mixer_prepare_group_fx_plan(
    const mixer_audio_track_runtime_t *track,
    uint8_t group_child,
    uint8_t *written_mask,
    float inv_frames)
{
    mixer_group_fx_plan_t plan = {0};
    if ((group_child == 0U) || (written_mask == NULL))
        return plan;
    plan.mask = mixer_group_fx_input_mask(track);
    plan.assign_mask = (uint8_t)(plan.mask & (uint8_t)~*written_mask);
    *written_mask |= plan.mask;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        plan.current[slot] = track->group_fx_level_current[slot];
        plan.step[slot] = (track->group_fx_level[slot] - plan.current[slot])
            * inv_frames;
    }
    return plan;
}

static inline void mixer_accumulate_group_fx_sample(
    mixer_group_fx_plan_t *plan,
    float bus_l[2][AUDIO_BLOCK_SIZE],
    float bus_r[2][AUDIO_BLOCK_SIZE],
    uint32_t frame,
    float left_trimmed,
    float right_trimmed)
{
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        const uint8_t bit = (uint8_t)(1U << slot);
        if ((plan->mask & bit) == 0U)
            continue;
        const float left = left_trimmed * plan->current[slot];
        const float right = right_trimmed * plan->current[slot];
        if ((plan->assign_mask & bit) != 0U)
        {
            bus_l[slot][frame] = left;
            bus_r[slot][frame] = right;
        }
        else
        {
            bus_l[slot][frame] += left;
            bus_r[slot][frame] += right;
        }
        plan->current[slot] += plan->step[slot];
    }
}

static inline void mixer_finish_group_fx_levels(
    mixer_audio_track_runtime_t *track,
    const mixer_group_fx_plan_t *plan)
{
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        if ((plan->mask & (uint8_t)(1U << slot)) != 0U)
            track->group_fx_level_current[slot] = track->group_fx_level[slot];
    }
}

static inline void mixer_accumulate_group_fx(
    mixer_audio_track_runtime_t *track,
    const float *left,
    const float *right,
    float bus_l[2][AUDIO_BLOCK_SIZE],
    float bus_r[2][AUDIO_BLOCK_SIZE],
    uint32_t frames,
    uint8_t *written_mask)
{
    const uint8_t mask = mixer_group_fx_input_mask(track);
    if ((mask == 0U) || (written_mask == NULL))
        return;
    const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
    for (uint8_t slot = 0U; slot < 2U; ++slot)
    {
        const uint8_t bit = (uint8_t)(1U << slot);
        if ((mask & bit) == 0U)
            continue;
        float level = track->group_fx_level_current[slot];
        const float target = track->group_fx_level[slot];
        const float step = (target - level) * inv_frames;
        if ((*written_mask & bit) == 0U)
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const float gain = level * MIXER_TRACK_NOMINAL_TRIM;
                bus_l[slot][i] = left[i] * gain;
                bus_r[slot][i] = right[i] * gain;
                level += step;
            }
            *written_mask |= bit;
        }
        else
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                const float gain = level * MIXER_TRACK_NOMINAL_TRIM;
                bus_l[slot][i] += left[i] * gain;
                bus_r[slot][i] += right[i] * gain;
                level += step;
            }
        }
        track->group_fx_level_current[slot] = target;
    }
}

static void mixer_clear_send_buffers(uint8_t clear_mask,
                                     float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
                                     float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
                                     uint32_t frames)
{
    for (uint32_t send = 0U; send < MIXER_NUM_SENDS; ++send)
    {
        if ((clear_mask & MIXER_SEND_MASK_BIT(send)) == 0U)
            continue;
        memset(send_l[send], 0, sizeof(float) * frames);
        memset(send_r[send], 0, sizeof(float) * frames);
    }
}

static void mixer_accumulate_send_plan_stereo(
    mixer_send_plan_t *plan,
    const float *left,
    const float *right,
    float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
    float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE],
    uint32_t frames,
    float trim)
{
    if (plan->assign_mask == 0U)
    {
        switch (plan->count)
        {
        case 3U:
        {
            const uint8_t send0 = plan->index[0];
            const uint8_t send1 = plan->index[1];
            const uint8_t send2 = plan->index[2];
            float gain0 = plan->current[0];
            float gain1 = plan->current[1];
            float gain2 = plan->current[2];
            const float step0 = plan->step[0];
            const float step1 = plan->step[1];
            const float step2 = plan->step[2];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send0][frame] += l * gain0;
                send_r[send0][frame] += r * gain0;
                send_l[send1][frame] += l * gain1;
                send_r[send1][frame] += r * gain1;
                send_l[send2][frame] += l * gain2;
                send_r[send2][frame] += r * gain2;
                gain0 += step0;
                gain1 += step1;
                gain2 += step2;
            }
            plan->current[0] = gain0;
            plan->current[1] = gain1;
            plan->current[2] = gain2;
            return;
        }
        case 2U:
        {
            const uint8_t send0 = plan->index[0];
            const uint8_t send1 = plan->index[1];
            float gain0 = plan->current[0];
            float gain1 = plan->current[1];
            const float step0 = plan->step[0];
            const float step1 = plan->step[1];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send0][frame] += l * gain0;
                send_r[send0][frame] += r * gain0;
                send_l[send1][frame] += l * gain1;
                send_r[send1][frame] += r * gain1;
                gain0 += step0;
                gain1 += step1;
            }
            plan->current[0] = gain0;
            plan->current[1] = gain1;
            return;
        }
        case 1U:
        {
            const uint8_t send = plan->index[0];
            float gain = plan->current[0];
            const float step = plan->step[0];
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                const float l = left[frame] * trim;
                const float r = right[frame] * trim;
                send_l[send][frame] += l * gain;
                send_r[send][frame] += r * gain;
                gain += step;
            }
            plan->current[0] = gain;
            return;
        }
        default:
            return;
        }
    }

    /* First contributors initialize their bus; mixed first/later-writer plans
     * stay outside the common all-accumulate hot kernels above. */
    for (uint32_t active = 0U; active < plan->count; ++active)
    {
        const uint8_t send = plan->index[active];
        float current = plan->current[active];
        const float step = plan->step[active];
        if ((plan->assign_mask & MIXER_SEND_MASK_BIT(active)) != 0U)
        {
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                send_l[send][frame] = left[frame] * trim * current;
                send_r[send][frame] = right[frame] * trim * current;
                current += step;
            }
        }
        else
        {
            for (uint32_t frame = 0U; frame < frames; ++frame)
            {
                send_l[send][frame] += left[frame] * trim * current;
                send_r[send][frame] += right[frame] * trim * current;
                current += step;
            }
        }
        plan->current[active] = current;
    }
}

void mixer_synth_voice_slot_copy(uint8_t source_slot, uint8_t destination_slot)
{
    if ((source_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            || (destination_slot >= SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            || (source_slot == destination_slot))
        return;
    mixer_copy_voice_envelope_state(&g_poly_filters_hot[destination_slot],
                                    &g_poly_filters_hot[source_slot]);
    g_poly_cutoff_override[destination_slot] = g_poly_cutoff_override[source_slot];
}

void mixer_track_voice_state_to_poly(uint32_t mix_track_id,
                                     uint32_t poly_track_id,
                                     uint8_t voice)
{
    mixer_track_filter_t *const destination = mixer_poly_filter(poly_track_id, voice);
    if ((destination == NULL) || (mix_track_id >= MIXER_MAX_TRACKS))
        return;
    mixer_poly_filter_sync_config(destination, &g_track_filters[mix_track_id]);
    mixer_copy_voice_envelope_state(destination, &g_track_filters[mix_track_id]);
    destination->vca_enabled = 1U;
}

void mixer_track_voice_state_from_poly(uint32_t mix_track_id,
                                       uint32_t poly_track_id,
                                       uint8_t voice)
{
    mixer_track_filter_t *const source = mixer_poly_filter(poly_track_id, voice);
    if ((source == NULL) || (mix_track_id >= MIXER_MAX_TRACKS))
        return;
    mixer_copy_voice_envelope_state(&g_track_filters[mix_track_id], source);
}

/**
 * @brief Traite un bloc de mixage final MAIN.
 *
 * @param tracks Tableau de tracks stéréo.
 * @param track_count Nombre de tracks valides.
 * @param frames Taille bloc en frames.
 *
 * Contexte d'appel:
 * - IRQ audio (hard realtime).
 */
/**
 * @brief Point d'entrée mixer_process.
 *
 * Role:
 * - Exécuter le traitement associé à mixer_process.
 *
 * @param tracks Paramètre d'entrée de l'API.
 * @param track_count Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
ITCM_TEXT void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    AUDIO_HOT ALIGN32 static float mono_pan_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float mono_pan_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_fx_l[2][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_group_fx_r[2][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float audio_rec_bus_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float audio_rec_bus_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static int32_t looper_record_i32[AUDIO_BLOCK_SIZE * AUDIO_RECORDER_CHANNELS];
    AUDIO_HOT ALIGN32 static int32_t audio_rec_bus_i32[AUDIO_BLOCK_SIZE * AUDIO_RECORDER_CHANNELS];
    static uint8_t looper_output_active[MIXER_MAX_TRACKS];

    mixer_routing_audio_apply_publication();
    control_routing_audio_apply_publication();
    audio_rec_bus_control_snapshot_t audio_rec_projection = {0};
    const uint8_t audio_rec_projection_valid =
        audio_rec_bus_projection_audio_read(&audio_rec_projection);

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    const brick_entity_id_t waveform_entity = audio_waveform_capture_get_entity();
    audio_waveform_capture_begin_block(waveform_entity);

    const uint8_t group_active = g_mixer_static_group_active;


    const uint8_t reverb_active = fx_reverb_global_is_active();
    const uint8_t delay_active = (g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            ? fx_delay_dual_global_is_active()
            : fx_delay_stereo_global_is_active();
    const uint8_t delay_reverb_send_active = (delay_active != 0U)
        ? ((g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            ? fx_delay_dual_global_reverb_send_is_active()
            : fx_delay_stereo_global_reverb_send_is_active())
        : 0U;
    const uint8_t modfx_active = fx_modfx_global_is_active();
    /* An enabled send FX remains in the process mask with zero input so its
     * history/tail keeps advancing exactly as before. Input fanout is a
     * separate decision based only on non-zero current/target lane gains. */
    const uint8_t tail_process_mask = mixer_send_destination_process_mask(
        reverb_active, delay_active, modfx_active);
    const uint32_t valid_lane_mask = (MIXER_MAX_TRACKS >= 32U)
        ? UINT32_MAX : ((uint32_t)(1UL << MIXER_MAX_TRACKS) - 1U);
    uint32_t lane_mask = (g_external_lane_mask | audio_tracks_enabled_mask())
        & valid_lane_mask;
    uint8_t lane_send_masks[MIXER_MAX_TRACKS] = {0U};
    uint8_t group_send_mask = 0U;
    const uint8_t input_send_mask = mixer_collect_input_send_masks(
        lane_mask, group_active, tail_process_mask,
        lane_send_masks, &group_send_mask);
    const uint8_t send_bus_active = (tail_process_mask != 0U) ? 1U : 0U;
    uint8_t send_written_mask = 0U;
    uint8_t group_fx_written_mask = 0U;

    memset(bus_main_l, 0, sizeof(bus_main_l));
    memset(bus_main_r, 0, sizeof(bus_main_r));
    if (group_active != 0U)
    {
        memset(bus_group_l, 0, sizeof(bus_group_l));
        memset(bus_group_r, 0, sizeof(bus_group_r));
    }
    const uint32_t ntracks = (track_count < MIXER_MAX_TRACKS) ? track_count : MIXER_MAX_TRACKS;
    const float looper_xfade_target = audio_xfade_get();
    const uint8_t looper_xfade_process_active =
        ((looper_xfade_target > MIXER_LOOPER_XFADE_EPS)
                || (g_looper_xfade_prev > MIXER_LOOPER_XFADE_EPS)
                || (g_looper_xfade_smoothed > MIXER_LOOPER_XFADE_EPS)) ? 1U : 0U;
    float looper_xfade_start = g_looper_xfade_prev;
    float looper_xfade_end = g_looper_xfade_prev;
    uint8_t looper_xfade_apply_active = 0U;
    if(looper_xfade_process_active != 0U)
    {
        looper_xfade_end = mixer_get_looper_xfade();
        looper_xfade_start = g_looper_xfade_prev;
        g_looper_xfade_prev = looper_xfade_end;

        if((mixer_looper_xfade_value_is_zero(looper_xfade_start) == 0U)
                || (mixer_looper_xfade_value_is_zero(looper_xfade_end) == 0U))
        {
            looper_xfade_apply_active = 1U;
        }
        else
        {
            g_looper_xfade_prev = 0.0f;
            g_looper_xfade_smoothed = 0.0f;
            looper_xfade_start = 0.0f;
            looper_xfade_end = 0.0f;
        }
    }
    uint8_t looper_playback_active = 0U;
    uint8_t looper_playback_mix_active = 0U;
    uint8_t looper_playback_routes_main = 0U;
    uint16_t looper_mask = brick6_looper_runtime_playing_mask();
    if (looper_mask != 0U)
    {
        memset(looper_output_active, 0, sizeof(looper_output_active));
    }
    while (looper_mask != 0U)
    {
        const uint8_t logical_track = (uint8_t)__builtin_ctz((unsigned)looper_mask);
        looper_mask &= (uint16_t)(looper_mask - 1U);
        track_audio_runtime_ctx_t ctx_value;
        const track_audio_runtime_ctx_t *const ctx =
            (audio_note_engine_adapter_audio_ctx_snapshot(
                logical_track, &ctx_value) != 0U) ? &ctx_value : NULL;
        if((ctx != 0)
                && (ctx->audio_binding.mix_track_id < MIXER_MAX_TRACKS)
                && ((g_mixer_static_lane_flags[ctx->audio_binding.mix_track_id]
                    & MIXER_STATIC_LOOPER) != 0U)
                && (g_tracks[ctx->audio_binding.mix_track_id].mute == 0U)
                && (brick6_looper_runtime_is_playing(logical_track) != 0U))
        {
            looper_output_active[logical_track] = 1U;
            looper_playback_active = 1U;
            if((g_mixer_static_lane_flags[ctx->audio_binding.mix_track_id]
                    & MIXER_STATIC_ROUTE_MAIN) != 0U)
            {
                looper_playback_routes_main = 1U;
            }
        }
    }
    looper_playback_mix_active =
        ((looper_playback_active != 0U) && (looper_xfade_apply_active != 0U)) ? 1U : 0U;
    uint8_t looper_record_track = 0U;
    const uint8_t looper_record_active = mixer_looper_record_capture_is_active(&looper_record_track);
    const uint8_t audio_rec_capture_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && ((audio_rec_projection.source_flags & AUDIO_REC_BUS_CAPTURE_ENABLED) != 0U));
    const uint8_t audio_rec_track_routes_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && (audio_rec_projection.source_entity_mask != 0U));
    const uint8_t audio_rec_bus_active = (uint8_t)(
        (audio_rec_projection_valid != 0U)
        && ((audio_rec_capture_active != 0U)
            || (audio_rec_projection.source_entity_mask != 0U)
            || ((audio_rec_projection.source_flags
                & (AUDIO_REC_BUS_SOURCE_LINE_DIRECT
                    | AUDIO_REC_BUS_SOURCE_MIC_LOGICAL)) != 0U)));
    if((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
    {
        memset(looper_bus_main_l, 0, sizeof(looper_bus_main_l));
        memset(looper_bus_main_r, 0, sizeof(looper_bus_main_r));
    }
    if(looper_record_active != 0U)
    {
        memset(looper_record_l, 0, sizeof(looper_record_l));
        memset(looper_record_r, 0, sizeof(looper_record_r));
    }
    if(audio_rec_bus_active != 0U)
    {
        memset(audio_rec_bus_l, 0, sizeof(audio_rec_bus_l));
        memset(audio_rec_bus_r, 0, sizeof(audio_rec_bus_r));
    }

    while (lane_mask != 0U)
    {
        const uint32_t t = (uint32_t)__builtin_ctz(lane_mask);
        lane_mask &= lane_mask - 1U;
        mixer_audio_track_runtime_t *mt = &g_tracks[t];
        const uint8_t static_flags = g_mixer_static_lane_flags[t];
        const brick_entity_id_t source_entity =
            audio_note_engine_adapter_entity_for_mix_lane((uint8_t)t);
        const uint8_t group_child = (uint8_t)(
            (static_flags & MIXER_STATIC_GROUP_CHILD) != 0U);
        const brick_entity_id_t audio_fx_entity = source_entity;
        const uint8_t audio_fx_active = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_ACTIVE) != 0U);
        const uint8_t filter_deferred = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_PRE_FILTER) != 0U);
        const uint8_t route_main = (uint8_t)(
            (static_flags & MIXER_STATIC_ROUTE_MAIN) != 0U);
        const uint8_t requires_stereo = (uint8_t)(
            (static_flags & MIXER_STATIC_REQUIRES_STEREO) != 0U);
        const uint8_t insert_stage = (uint8_t)(
            (static_flags & MIXER_STATIC_INSERT_STAGE) != 0U);
        const uint8_t audio_fx_comp = (uint8_t)(
            (static_flags & MIXER_STATIC_AUDIO_FX_COMP) != 0U);
        const uint8_t audio_fx_post = (uint8_t)(
            (audio_fx_active != 0U)
            && (audio_fx_comp == 0U));
        const audio_fx_sample_plan_handle_t audio_fx_sample_plan =
            (audio_fx_post != 0U)
                ? audio_fx_runtime_get_sample_plan(audio_fx_entity) : NULL;
        float *const dry_bus_l = (group_child != 0U) ? bus_group_l : bus_main_l;
        float *const dry_bus_r = (group_child != 0U) ? bus_group_r : bus_main_r;
        const uint8_t hw_enabled = (t < ntracks) ? tracks[t].enabled : 0U;
        const mixer_lane_plan_t lane_plan = mixer_build_lane_plan(t,
                                                                  mt,
                                                                  &g_track_filters[t],
                                                                  hw_enabled,
                                                                  g_external_track_enabled[t],
                                                                  g_external_track_format[t],
                                                                  g_external_track_frames_valid[t],
                                                                  frames);
        if (lane_plan.active == 0U)
            continue;
        float *L = NULL;
        float *R = NULL;
        float *mono = NULL;
        const uint8_t is_mono_native_lane = (lane_plan.exec_kind == MIXER_LANE_EXEC_MONO_NATIVE) ? 1U : 0U;
        const uint8_t waveform_capture_lane =
            ((waveform_entity != BRICK_ENTITY_INVALID_ID)
             && (source_entity == waveform_entity)) ? 1U : 0U;
        const uint8_t multi_prefiltered =
            ((lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)
             || (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_STEREO)) ? 1U : 0U;

        if (is_mono_native_lane != 0U)
        {
            mixer_lane_buffers_t buffers = {0};
            if (waveform_capture_lane != 0U)
            {
                audio_waveform_capture_tap_reference_mono_block(
                    g_external_track_mono[t], frames);
            }
            if (multi_prefiltered != 0U)
            {
                buffers.mono = g_external_track_mono[t];
            }
            else
            {
                if (filter_deferred == 0U)
                    buffers = mixer_lane_run_mono_native_path(t,
                                                               mt,
                                                               &g_track_filters[t],
                                                               frames);
                else
                    buffers.mono = g_external_track_mono[t];
            }
            mono = buffers.mono;
        }
        else
        {
            const mixer_lane_buffers_t buffers = mixer_lane_prepare_stereo_buffers(t,
                                                                                   &lane_plan,
                                                                                   tracks);
            L = buffers.left;
            R = buffers.right;
            mixer_lane_accumulate_external_source(t, &lane_plan, L, R);
            if (waveform_capture_lane != 0U)
            {
                audio_waveform_capture_tap_reference_stereo_block(L, R, frames);
            }
            if ((filter_deferred == 0U)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO))
                mixer_lane_run_stereo_path(t, mt, &g_track_filters[t], L, R, frames);
        }
        const uint8_t waveform_capture_samples = (uint8_t)(
            (waveform_capture_lane != 0U)
            && (audio_waveform_capture_needs_final_samples() != 0U));


        /*
         * Common mono-native fan-out. Keep the historical L/R path when
         * an insert, diagnostic, or auxiliary capture/looper route needs the
         * materialized stereo buffers.  The direct path preserves the same
         * post-pan L/R values and the existing stereo send contracts, but
         * accumulates every destination while the source sample is live.
         */
        uint8_t direct_mono_fanout = 0U;
        const uint8_t looper_record_route_active =
            (looper_record_active != 0U)
                ? mixer_lane_routes_to_looper(looper_record_track, (uint8_t)t)
                : 0U;
        if ((is_mono_native_lane != 0U)
                && (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                && (audio_rec_track_routes_active == 0U)
                && (looper_record_route_active == 0U)
                && (looper_playback_mix_active == 0U))
        {
            direct_mono_fanout = (requires_stereo == 0U) ? 1U : 0U;
        }

        if (direct_mono_fanout != 0U)
        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            const uint8_t track_vca_enabled =
                (g_track_filters[t].vca_enabled != 0U) ? 1U : 0U;
            float vca_segment_gain[AUDIO_BLOCK_SIZE];
            uint32_t vca_frames = 0U;
            if (track_vca_enabled != 0U)
            {
                vca_frames = env_adsr_process_vca_block(&g_track_filters[t].vca_env,
                                                        vca_segment_gain,
                                                        frames);
                for (uint32_t i = vca_frames; i < frames; ++i)
                {
                    vca_segment_gain[i] = 0.0f;
                }
            }
            const uint8_t first_writer_mask = (uint8_t)(
                lane_send_masks[t] & (uint8_t)~send_written_mask);
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                mt, lane_send_masks[t], first_writer_mask, inv_frames);
            mixer_group_fx_plan_t group_fx_plan = mixer_prepare_group_fx_plan(
                mt, group_child, &group_fx_written_mask, inv_frames);

            for (uint32_t i = 0U; i < frames; ++i)
            {
                /* Keep the historical ramp and pan equations/order. */
                float pan_l = coefficient_plan.pan_l;
                float pan_r = coefficient_plan.pan_r;
                float mono_gain = coefficient_plan.mono_gain;
                if (coefficient_plan.stable == 0U)
                {
                    const float pan_for_mix = -pan_cur;
                    pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    mono_gain = gain_cur * mute_gain_cur;
                }
                float vca_gain = 1.0f;
                if (track_vca_enabled != 0U)
                {
                    vca_gain = vca_segment_gain[i];
                    g_track_filters[t].vca_env_value = vca_gain;
                }

                float sample_pre_fader = mono[i] * vca_gain;
                if (audio_fx_comp != 0U)
                {
                    sample_pre_fader = fx_chain_process_audio_fx_comp_mono_sample(
                        audio_fx_entity, sample_pre_fader);
                }
                const float sample_processed = sample_pre_fader * mono_gain;
                float left = sample_processed * pan_l;
                float right = sample_processed * pan_r;
                if (audio_fx_post != 0U)
                {
                    audio_fx_runtime_process_stereo_sample_prepared(
                        audio_fx_sample_plan, &left, &right);
                }
                if (waveform_capture_samples != 0U)
                {
                    audio_waveform_capture_tap_stereo_sample(left, right);
                }
                const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                if (group_fx_plan.mask != 0U)
                    mixer_accumulate_group_fx_sample(&group_fx_plan,
                        bus_group_fx_l, bus_group_fx_r, i,
                        left_trimmed, right_trimmed);
                if (send_plan.assign_mask == 0U)
                {
                    switch (send_plan.count)
                    {
                    case 3U:
                    {
                        const uint8_t send = send_plan.index[2];
                        send_l[send][i] += left_trimmed * send_plan.current[2];
                        send_r[send][i] += right_trimmed * send_plan.current[2];
                        send_plan.current[2] += send_plan.step[2];
                    }
                    /* fall through */
                    case 2U:
                    {
                        const uint8_t send = send_plan.index[1];
                        send_l[send][i] += left_trimmed * send_plan.current[1];
                        send_r[send][i] += right_trimmed * send_plan.current[1];
                        send_plan.current[1] += send_plan.step[1];
                    }
                    /* fall through */
                    case 1U:
                    {
                        const uint8_t send = send_plan.index[0];
                        send_l[send][i] += left_trimmed * send_plan.current[0];
                        send_r[send][i] += right_trimmed * send_plan.current[0];
                        send_plan.current[0] += send_plan.step[0];
                        break;
                    }
                    default:
                        break;
                    }
                }
                else
                {
                    for (uint32_t active = 0U; active < send_plan.count; ++active)
                    {
                        const uint8_t send = send_plan.index[active];
                        const float send_gain = send_plan.current[active];
                        if ((send_plan.assign_mask & MIXER_SEND_MASK_BIT(active)) != 0U)
                        {
                            send_l[send][i] = left_trimmed * send_gain;
                            send_r[send][i] = right_trimmed * send_gain;
                        }
                        else
                        {
                            send_l[send][i] += left_trimmed * send_gain;
                            send_r[send][i] += right_trimmed * send_gain;
                        }
                        send_plan.current[active] += send_plan.step[active];
                    }
                }

                if (route_main != 0U)
                {
                    dry_bus_l[i] += left_trimmed;
                    dry_bus_r[i] += right_trimmed;
                }

                if (coefficient_plan.stable == 0U)
                {
                    mixer_advance_track_ramps(&gain_cur, gain_step,
                                              &pan_cur, pan_step,
                                              &mute_gain_cur, mute_target, mute_step);
                }
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
            if (send_bus_active != 0U)
                mixer_finish_send_levels(mt);
            mixer_finish_group_fx_levels(mt, &group_fx_plan);
            send_written_mask |= lane_send_masks[t];
            continue;
        }
        /*
         * POLY_STEREO final fan-out.  The external poly buffers are already
         * accumulated L/R, so keep them read-only and feed every active final
         * destination from one pass.  Auxiliary capture/looper routes and
         * diagnostics keep the historical materialized-buffer path.
         */
        uint8_t poly_stereo_fanout = 0U;
        if ((lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                && (audio_rec_track_routes_active == 0U)
                && (looper_record_route_active == 0U)
                && (looper_playback_mix_active == 0U)
                && (route_main != 0U)
                && (modfx_active == 0U)
                && ((input_send_mask & MIXER_SEND_MASK_BIT(MIXER_MODFX_SEND_INDEX)) == 0U)
                )
        {
            poly_stereo_fanout = (requires_stereo == 0U) ? 1U : 0U;
        }

        if (poly_stereo_fanout != 0U)
        {
            /* No legacy insert is active in this fast path, but the
             * mono-compatible entity Audio FX must run after the per-frame
             * gain/pan coefficients, just like the regular insert path. */
            const brick_entity_id_t audio_fx_entity =
                (source_entity < BRICK_ENTITY_CAPACITY)
                    ? (brick_entity_id_t)source_entity
                    : BRICK_ENTITY_INVALID_ID;
            enum
            {
                POLY_FANOUT_MAIN = 1U,
                POLY_FANOUT_REVERB = 2U,
                POLY_FANOUT_DELAY = 4U
            };
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            float send_cur[MIXER_NUM_SENDS] = {0.0f};
            float send_step[MIXER_NUM_SENDS] = {0.0f};
            uint8_t fanout_mode = POLY_FANOUT_MAIN;
            uint8_t fanout_assign_mask = 0U;
            mixer_group_fx_plan_t group_fx_plan = mixer_prepare_group_fx_plan(
                mt, group_child, &group_fx_written_mask, inv_frames);

            if (send_bus_active != 0U)
            {
                const mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                    mt, lane_send_masks[t],
                    (uint8_t)(lane_send_masks[t] & (uint8_t)~send_written_mask),
                    inv_frames);
                fanout_assign_mask = send_plan.assign_mask;
                for (uint32_t active = 0U; active < send_plan.count; ++active)
                {
                    const uint8_t send = send_plan.index[active];
                    send_cur[send] = send_plan.current[active];
                    send_step[send] = send_plan.step[active];
                    fanout_mode |= (send == MIXER_REVERB_SEND_INDEX)
                        ? POLY_FANOUT_REVERB : POLY_FANOUT_DELAY;
                }
            }

            switch (fanout_mode)
            {
                case POLY_FANOUT_MAIN:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_REVERB:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_REVERB_SEND_INDEX] += send_step[MIXER_REVERB_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_DELAY:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_DELAY_SEND_INDEX] += send_step[MIXER_DELAY_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                case POLY_FANOUT_MAIN | POLY_FANOUT_REVERB | POLY_FANOUT_DELAY:
                    for (uint32_t i = 0U; i < frames; ++i)
                    {
                        float gain_l = 0.0f;
                        float gain_r = 0.0f;
                        mixer_track_coefficients_at(&coefficient_plan,
                                                    gain_cur,
                                                    pan_cur,
                                                    mute_gain_cur,
                                                    &gain_l,
                                                    &gain_r);
                        float left = L[i];
                        float right = R[i];
                        if (audio_fx_comp != 0U)
                        {
                            fx_chain_process_audio_fx_comp_stereo_sample(
                                audio_fx_entity, &left, &right);
                        }
                        left *= gain_l;
                        right *= gain_r;
                        if (audio_fx_post != 0U)
                        {
                            audio_fx_runtime_process_stereo_sample_prepared(
                                audio_fx_sample_plan, &left, &right);
                        }
                        if (waveform_capture_samples != 0U)
                        {
                            audio_waveform_capture_tap_stereo_sample(left, right);
                        }
                        const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;
                        if (group_fx_plan.mask != 0U)
                            mixer_accumulate_group_fx_sample(&group_fx_plan,
                                bus_group_fx_l, bus_group_fx_r, i,
                                left_trimmed, right_trimmed);
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(0U)) != 0U)
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                            send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        }
                        if ((fanout_assign_mask & MIXER_SEND_MASK_BIT(1U)) != 0U)
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] = left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] = right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        else
                        {
                            send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                            send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        }
                        dry_bus_l[i] += left_trimmed;
                        dry_bus_r[i] += right_trimmed;
                        send_cur[MIXER_REVERB_SEND_INDEX] += send_step[MIXER_REVERB_SEND_INDEX];
                        send_cur[MIXER_DELAY_SEND_INDEX] += send_step[MIXER_DELAY_SEND_INDEX];
                        if (coefficient_plan.stable == 0U)
                        {
                            mixer_advance_track_ramps(&gain_cur, gain_step,
                                                      &pan_cur, pan_step,
                                                      &mute_gain_cur, mute_target, mute_step);
                        }
                    }
                    break;

                default:
                    break;
            }


            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
            if (send_bus_active != 0U)
                mixer_finish_send_levels(mt);
            mixer_finish_group_fx_levels(mt, &group_fx_plan);
            send_written_mask |= lane_send_masks[t];
            continue;
        }

        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;
            const mixer_track_coefficient_plan_t coefficient_plan =
                mixer_prepare_track_coefficients(gain_cur,
                                                  mt->gain,
                                                  pan_cur,
                                                  mt->pan,
                                                  mute_gain_cur,
                                                  mute_target);
            const uint8_t track_vca_enabled =
                ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                    && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                    && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                    && (g_track_filters[t].vca_enabled != 0U)) ? 1U : 0U;
            float vca_segment_gain[AUDIO_BLOCK_SIZE];
            uint32_t vca_frames = 0U;
            if (track_vca_enabled != 0U)
            {
                vca_frames = env_adsr_process_vca_block(&g_track_filters[t].vca_env,
                                                        vca_segment_gain,
                                                        frames);
                for (uint32_t i = vca_frames; i < frames; ++i)
                {
                    vca_segment_gain[i] = 0.0f;
                }
            }

            for (uint32_t i = 0U; i < frames; ++i)
            {
                float vca_gain = 1.0f;
                if (track_vca_enabled != 0U)
                {
                    vca_gain = vca_segment_gain[i];
                    g_track_filters[t].vca_env_value = vca_gain;
                }
                if (is_mono_native_lane != 0U)
                {
                    const float vca_sample = mono[i] * vca_gain;
                    mono[i] = vca_sample;
                    mono_pan_l[i] = vca_sample;
                    mono_pan_r[i] = vca_sample;
                }
                else
                {
                    L[i] *= vca_gain;
                    R[i] *= vca_gain;
                }
            }

            const brick_entity_id_t audio_fx_entity =
                (source_entity < BRICK_ENTITY_CAPACITY)
                    ? (brick_entity_id_t)source_entity
                    : BRICK_ENTITY_INVALID_ID;
            if (is_mono_native_lane != 0U)
            {
                L = mono_pan_l;
                R = mono_pan_r;
            }
            if (insert_stage != 0U)
            {
                fx_chain_process_track_inserts_pre_fader(
                    audio_fx_entity,
                    t,
                    mt->insert_slot,
                    MIXER_INSERTS_PER_TRACK,
                    audio_fx_comp,
                    L,
                    R,
                    frames);
            }

            gain_cur = mt->gain_current;
            pan_cur = mt->pan_current;
            mute_gain_cur = mt->mute_gain_current;
            for (uint32_t i = 0U; i < frames; ++i)
            {
                /* Standard user convention: pan<0 => left, pan>0 => right.
                 * Runtime output stage wiring is mirrored, so mixer pan is compensated here. */
                float pan_l = coefficient_plan.pan_l;
                float pan_r = coefficient_plan.pan_r;
                float gain_l = coefficient_plan.gain_l;
                float gain_r = coefficient_plan.gain_r;
                float mono_gain = coefficient_plan.mono_gain;
                if (coefficient_plan.stable == 0U)
                {
                    const float pan_for_mix = -pan_cur;
                    pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    gain_l = gain_cur * pan_l * mute_gain_cur;
                    gain_r = gain_cur * pan_r * mute_gain_cur;
                    mono_gain = gain_cur * mute_gain_cur;
                }
                if (is_mono_native_lane != 0U)
                {
                    mono_pan_l[i] = L[i] * (mono_gain * pan_l);
                    mono_pan_r[i] = R[i] * (mono_gain * pan_r);
                }
                else
                {
                    L[i] *= gain_l;
                    R[i] *= gain_r;
                }
                if (coefficient_plan.stable == 0U)
                {
                    mixer_advance_track_ramps(&gain_cur, gain_step,
                                              &pan_cur, pan_step,
                                              &mute_gain_cur, mute_target, mute_step);
                }
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
        }

        if (is_mono_native_lane != 0U)
        {
            L = mono_pan_l;
            R = mono_pan_r;
        }

        /* engine -> filter -> VCA -> legacy inserts/COMP -> track fader/mute
         * -> pan -> remaining Audio FX -> sends/bus. */
        if ((audio_fx_post != 0U) || (filter_deferred != 0U))
        {
            audio_fx_runtime_process_before_filter(audio_fx_entity,L,R,frames);
            if (filter_deferred != 0U)
                mixer_lane_run_stereo_path(t,mt,&g_track_filters[t],L,R,frames);
            audio_fx_runtime_process_after_filter(audio_fx_entity,L,R,frames);
        }
        if (waveform_capture_samples != 0U)
        {
            audio_waveform_capture_tap_stereo_block(L, R, frames);
        }

        if (group_child != 0U)
        {
            mixer_accumulate_group_fx(mt, L, R,
                                      bus_group_fx_l, bus_group_fx_r,
                                      frames, &group_fx_written_mask);
        }

        {
            const uint8_t source_track = (source_entity < BRICK_ENTITY_CAPACITY)
                ? source_entity : (uint8_t)t;
            if((source_track < MIXER_MAX_TRACKS)
                    && ((static_flags & MIXER_STATIC_LOOPER) != 0U))
            {
                if((audio_rec_bus_active != 0U)
                        && (source_track < BRICK_ENTITY_CAPACITY)
                        && ((audio_rec_projection.source_entity_mask
                            & (uint16_t)(1U << source_track)) != 0U))
                {
                    for(uint32_t i = 0U; i < frames; ++i)
                    {
                        audio_rec_bus_l[i] += L[i];
                        audio_rec_bus_r[i] += R[i];
                    }
                }

                if((looper_playback_mix_active != 0U) && (looper_output_active[source_track] != 0U))
                {
                    if(route_main != 0U)
                    {
                        for(uint32_t i = 0U; i < frames; ++i)
                        {
                            looper_bus_main_l[i] += L[i] * MIXER_TRACK_NOMINAL_TRIM;
                            looper_bus_main_r[i] += R[i] * MIXER_TRACK_NOMINAL_TRIM;
                        }
                    }
                }
                continue;
            }
            if((looper_record_active != 0U)
                    && (source_track < MIXER_MAX_TRACKS)
                    && (source_track != looper_record_track)
                    && (control_routing_audio_get_looper_source(looper_record_track, source_track) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    looper_record_l[i] += L[i];
                    looper_record_r[i] += R[i];
                }
            }

            if((audio_rec_bus_active != 0U)
                    && (source_track < BRICK_ENTITY_CAPACITY)
                    && ((audio_rec_projection.source_entity_mask
                        & (uint16_t)(1U << source_track)) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    audio_rec_bus_l[i] += L[i];
                    audio_rec_bus_r[i] += R[i];
                }
            }
        }

        if(send_bus_active != 0U)
        {
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                mt, lane_send_masks[t],
                (uint8_t)(lane_send_masks[t] & (uint8_t)~send_written_mask),
                (frames > 0U) ? (1.0f / (float)frames) : 0.0f);
            mixer_accumulate_send_plan_stereo(&send_plan, L, R,
                                              send_l, send_r, frames,
                                              MIXER_TRACK_NOMINAL_TRIM);
            mixer_finish_send_levels(mt);
            send_written_mask |= lane_send_masks[t];
        }

        if(route_main != 0U)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                dry_bus_l[i] += l_nom;
                dry_bus_r[i] += r_nom;
            }
        }
    }

    if (group_active != 0U)
    {
        mixer_audio_track_runtime_t *const group = &g_tracks[MIXER_GROUP_BUS_TRACK];
        const uint8_t group_static_flags =
            g_mixer_static_lane_flags[MIXER_GROUP_BUS_TRACK];

        /* AUDIO-owned GROUP order:
         * child dry and local A/B sends are post child-MIX.  A/B are processed
         * independently by the master's two kernels, reinjected into dry,
         * then master filter -> master MIX -> inserts -> global sends/dry.
         */
        for (uint8_t slot = 0U; slot < 2U; ++slot)
        {
            const uint8_t bit = (uint8_t)(1U << slot);
            if (((group_fx_written_mask & bit) != 0U)
                    && (audio_fx_runtime_process_parallel_slot(
                        BRICK_ENTITY_GROUP_MASTER_ID,
                        (audio_fx_slot_t)slot,
                        bus_group_fx_l[slot], bus_group_fx_r[slot],
                        frames) != 0U))
            {
                for (uint32_t i = 0U; i < frames; ++i)
                {
                    bus_group_l[i] += bus_group_fx_l[slot][i];
                    bus_group_r[i] += bus_group_fx_r[slot][i];
                }
            }
        }
        if (waveform_entity == BRICK_ENTITY_GROUP_MASTER_ID)
        {
            audio_waveform_capture_tap_reference_stereo_block(bus_group_l,
                                                               bus_group_r,
                                                               frames);
        }
        if ((group_static_flags & MIXER_STATIC_AUDIO_FX_PRE_FILTER) != 0U)
        {
            fx_chain_process_audio_fx_pre_filter_stereo(
                BRICK_ENTITY_GROUP_MASTER_ID,
                bus_group_l,
                bus_group_r,
                frames);
        }
        mixer_lane_run_stereo_path(MIXER_GROUP_BUS_TRACK,
                                   group,
                                   &g_track_filters[MIXER_GROUP_BUS_TRACK],
                                   bus_group_l,
                                   bus_group_r,
                                   frames);

        if ((group_static_flags & MIXER_STATIC_INSERT_STAGE) != 0U)
        {
            fx_chain_process_track_inserts_pre_fader(
                BRICK_ENTITY_GROUP_MASTER_ID,
                MIXER_GROUP_BUS_TRACK,
                group->insert_slot,
                MIXER_INSERTS_PER_TRACK,
                (uint8_t)((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_COMP) != 0U),
                bus_group_l,
                bus_group_r,
                frames);
        }

        float gain_cur = group->gain_current;
        float pan_cur = group->pan_current;
        float mute_gain_cur = group->mute_gain_current;
        const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
        const float gain_step = (group->gain - gain_cur) * inv_frames;
        const float pan_step = (group->pan - pan_cur) * inv_frames;
        const float mute_target = (group->mute != 0U) ? 0.0f : 1.0f;
        const float mute_step = 1.0f / 240.0f;
        const mixer_track_coefficient_plan_t coefficient_plan =
            mixer_prepare_track_coefficients(gain_cur,
                                              group->gain,
                                              pan_cur,
                                              group->pan,
                                              mute_gain_cur,
                                              mute_target);
        for (uint32_t i = 0U; i < frames; ++i)
        {
            float gain_l = 0.0f;
            float gain_r = 0.0f;
            mixer_track_coefficients_at(&coefficient_plan,
                                        gain_cur,
                                        pan_cur,
                                        mute_gain_cur,
                                        &gain_l,
                                        &gain_r);
            bus_group_l[i] *= gain_l;
            bus_group_r[i] *= gain_r;
            if (coefficient_plan.stable == 0U)
                mixer_advance_track_ramps(&gain_cur, gain_step,
                                          &pan_cur, pan_step,
                                          &mute_gain_cur, mute_target, mute_step);
        }
        group->gain_current = group->gain;
        group->pan_current = group->pan;
        group->mute_gain_current = mute_gain_cur;

        if (((group_static_flags & MIXER_STATIC_AUDIO_FX_ACTIVE) != 0U)
                && ((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_PRE_FILTER) == 0U)
                && ((group_static_flags
                    & MIXER_STATIC_AUDIO_FX_COMP) == 0U))
        {
            fx_chain_process_audio_fx_post_fader(BRICK_ENTITY_GROUP_MASTER_ID,
                                                 bus_group_l,
                                                 bus_group_r,
                                                 frames);
        }
        if ((waveform_entity == BRICK_ENTITY_GROUP_MASTER_ID)
                && (audio_waveform_capture_needs_final_samples() != 0U))
        {
            audio_waveform_capture_tap_stereo_block(bus_group_l,
                                                   bus_group_r,
                                                   frames);
        }

        if (send_bus_active != 0U)
        {
            mixer_send_plan_t send_plan = mixer_prepare_send_plan(
                group, group_send_mask,
                (uint8_t)(group_send_mask & (uint8_t)~send_written_mask),
                inv_frames);
            mixer_accumulate_send_plan_stereo(&send_plan,
                                              bus_group_l, bus_group_r,
                                              send_l, send_r, frames, 1.0f);
            mixer_finish_send_levels(group);
            send_written_mask |= group_send_mask;
        }

        if ((group_static_flags & MIXER_STATIC_ROUTE_MAIN) != 0U)
        {
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += bus_group_l[i];
                bus_main_r[i] += bus_group_r[i];
            }
        }
    }

    if(looper_record_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * AUDIO_RECORDER_CHANNELS;
            looper_record_i32[out] = mixer_looper_float_to_pcm24(looper_record_l[i]);
            looper_record_i32[out + 1U] = mixer_looper_float_to_pcm24(looper_record_r[i]);
        }
        (void)brick6_looper_runtime_capture_from_irq(looper_record_track,
                                                     looper_record_i32,
                                                     frames);
    }

    mixer_external_inputs_clear();

    if(send_bus_active != 0U)
    {
        uint8_t zero_input_mask = tail_process_mask;
        if (delay_reverb_send_active != 0U)
            zero_input_mask |= MIXER_SEND_MASK_BIT(MIXER_REVERB_SEND_INDEX);
        const uint8_t clear_mask = (uint8_t)(
            zero_input_mask & (uint8_t)~send_written_mask);
        if (clear_mask != 0U)
            mixer_clear_send_buffers(clear_mask, send_l, send_r, frames);

        if(delay_active != 0U)
        {
            if(g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            {
                fx_delay_dual_global_process_block_add(send_l[MIXER_DELAY_SEND_INDEX],
                                                   send_r[MIXER_DELAY_SEND_INDEX],
                                                   bus_main_l,
                                                   bus_main_r,
                                                   (delay_reverb_send_active != 0U)
                                                       ? delay_reverb_l : NULL,
                                                   (delay_reverb_send_active != 0U)
                                                       ? delay_reverb_r : NULL,
                                                   frames);
            }
            else
            {
                fx_delay_stereo_global_process_block_add(send_l[MIXER_DELAY_SEND_INDEX],
                                                     send_r[MIXER_DELAY_SEND_INDEX],
                                                     bus_main_l,
                                                     bus_main_r,
                                                     (delay_reverb_send_active != 0U)
                                                         ? delay_reverb_l : NULL,
                                                     (delay_reverb_send_active != 0U)
                                                         ? delay_reverb_r : NULL,
                                                     frames);
            }
            if (delay_reverb_send_active != 0U)
            {
                for (uint32_t i = 0U; i < frames; ++i)
                {
                    send_l[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_l[i];
                    send_r[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_r[i];
                }
            }
        }

        if(reverb_active != 0U)
        {
            fx_reverb_global_process_block_add(
                send_l[MIXER_REVERB_SEND_INDEX],
                send_r[MIXER_REVERB_SEND_INDEX],
                bus_main_l,
                bus_main_r,
                frames);
        }

        if (modfx_active != 0U)
        {
            fx_modfx_global_process_block(send_l[MIXER_MODFX_SEND_INDEX],
                                          send_r[MIXER_MODFX_SEND_INDEX], frames);
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += send_l[MIXER_MODFX_SEND_INDEX][i];
                bus_main_r[i] += send_r[MIXER_MODFX_SEND_INDEX][i];
            }
        }

        const int8_t send_fx_slot = g_send_fx_slot[MIXER_REVERB_SEND_INDEX];
        if (send_fx_slot >= 0)
        {
            fx_chain_process_global_slot((uint32_t)send_fx_slot,
                                          send_l[MIXER_REVERB_SEND_INDEX],
                                          send_r[MIXER_REVERB_SEND_INDEX],
                                          frames);
            for (uint32_t i = 0U; i < frames; ++i)
            {
                bus_main_l[i] += send_l[MIXER_REVERB_SEND_INDEX][i];
                bus_main_r[i] += send_r[MIXER_REVERB_SEND_INDEX][i];
            }
        }
    }

    if(looper_xfade_apply_active != 0U)
    {
        if((mixer_looper_xfade_value_is_full(looper_xfade_start) != 0U)
                && (mixer_looper_xfade_value_is_full(looper_xfade_end) != 0U))
        {
            if((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
            {
                memcpy(bus_main_l, looper_bus_main_l, sizeof(float) * frames);
                memcpy(bus_main_r, looper_bus_main_r, sizeof(float) * frames);
            }
            else
            {
                memset(bus_main_l, 0, sizeof(float) * frames);
                memset(bus_main_r, 0, sizeof(float) * frames);
            }
        }
        else if(mixer_looper_xfade_values_are_stable(looper_xfade_start, looper_xfade_end) != 0U)
        {
            float xfade = looper_xfade_end;
            if(xfade < 0.0f)
            {
                xfade = 0.0f;
            }
            else if(xfade > 1.0f)
            {
                xfade = 1.0f;
            }

            const float live_gain = 1.0f - xfade;
            const float loop_gain = xfade;
            for(uint32_t i = 0U; i < frames; ++i)
            {
                const float loop_main_l = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_l[i]
                    : 0.0f;
                const float loop_main_r = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_r[i]
                    : 0.0f;
                bus_main_l[i] = (bus_main_l[i] * live_gain) + (loop_main_l * loop_gain);
                bus_main_r[i] = (bus_main_r[i] * live_gain) + (loop_main_r * loop_gain);
            }

        }
        else
        {
            const float xfade_step = (frames > 1U)
                ? ((looper_xfade_end - looper_xfade_start) / (float)(frames - 1U))
                : 0.0f;
            float xfade = (frames > 1U) ? looper_xfade_start : looper_xfade_end;
            for(uint32_t i = 0U; i < frames; ++i)
            {
                if(xfade < 0.0f)
                {
                    xfade = 0.0f;
                }
                else if(xfade > 1.0f)
                {
                    xfade = 1.0f;
                }

                const float live_gain = 1.0f - xfade;
                const float loop_main_l = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_l[i]
                    : 0.0f;
                const float loop_main_r = ((looper_playback_mix_active != 0U) && (looper_playback_routes_main != 0U))
                    ? looper_bus_main_r[i]
                    : 0.0f;
                bus_main_l[i] = (bus_main_l[i] * live_gain) + (loop_main_l * xfade);
                bus_main_r[i] = (bus_main_r[i] * live_gain) + (loop_main_r * xfade);
                xfade += xfade_step;
            }
        }
    }

    /* One authoritative master dynamics slot, post returns/Looper crossfade. */
    fx_slot_t *const master_slot = fx_pool_get_slot(2U);
    if ((master_slot != NULL) && (master_slot->active != 0U)
            && (master_slot->state != NULL))
    {
        fx_chain_process_global_slot(2U, bus_main_l, bus_main_r, frames);
    }

    if((audio_rec_bus_active != 0U)
            && ((audio_rec_projection.source_flags
                & AUDIO_REC_BUS_SOURCE_LINE_DIRECT) != 0U))
    {
        const audio_physical_inputs_t *const inputs =
            audio_io_get_current_physical_inputs();
        for(uint32_t i = 0U; i < frames; ++i)
        {
            audio_rec_bus_l[i] += inputs->line.left[i];
            audio_rec_bus_r[i] += inputs->line.right[i];
        }
    }

    uint32_t audio_rec_peak_abs_pcm24 = 0U;
    if(audio_rec_bus_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * AUDIO_RECORDER_CHANNELS;
            const int32_t pcm_l = mixer_looper_float_to_pcm24(audio_rec_bus_l[i]);
            const int32_t pcm_r = mixer_looper_float_to_pcm24(audio_rec_bus_r[i]);
            const uint32_t abs_l = (pcm_l < 0) ? (uint32_t)(-pcm_l) : (uint32_t)pcm_l;
            const uint32_t abs_r = (pcm_r < 0) ? (uint32_t)(-pcm_r) : (uint32_t)pcm_r;
            if(abs_l > audio_rec_peak_abs_pcm24)
            {
                audio_rec_peak_abs_pcm24 = abs_l;
            }
            if(abs_r > audio_rec_peak_abs_pcm24)
            {
                audio_rec_peak_abs_pcm24 = abs_r;
            }
            if(audio_rec_capture_active != 0U)
            {
                audio_rec_bus_i32[out] = pcm_l;
                audio_rec_bus_i32[out + 1U] = pcm_r;
            }
        }
        if(audio_rec_capture_active != 0U)
        {
            (void)sample_capture_push_audio_block_from_irq(audio_rec_bus_i32, frames);
        }
    }
    audio_rec_level_snapshot_audio_publish(audio_rec_peak_abs_pcm24);

    if(track_count > 0U)
    {
        memcpy(tracks[0].L, bus_main_l, sizeof(float) * frames);
        memcpy(tracks[0].R, bus_main_r, sizeof(float) * frames);
    }

}
