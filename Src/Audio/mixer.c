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
 * - Appelle: fx_chain_process_slot(), audio_float_set_master_gain().
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
#include "Audio/audio_track_diag.h"

#include "Audio/audio_xfade.h"
#include "env_adsr.h"
#include "vca_env.h"
#include "fx_biquad_filter.h"
#include "fx_delay_dual.h"
#include "fx_delay_stereo.h"
#include "fx_reverb.h"
#include "Audio/spectral_window.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Audio/multi_voice_dsp.h"

#include "Storage/multi_record_writer.h"
#include "Storage/sample_capture.h"
#include "UI/ui_core_runtime_bridge.h"

#include <math.h>
#include <string.h>

#include "fx_chain.h"
#include "fx_pool.h"
#include "memory_layout.h"

typedef struct {
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
} mixer_track_t;

typedef enum
{
    MIXER_FILTER_DSP_STEREO = 0U,
    MIXER_FILTER_DSP_MONO = 1U
} mixer_filter_dsp_format_t;

typedef enum
{
    MIXER_CONT_CUTOFF = 0,
    MIXER_CONT_RESONANCE,
    MIXER_CONT_EQ_LOW,
    MIXER_CONT_EQ_MID,
    MIXER_CONT_EQ_HIGH,
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

typedef struct {
    union {
        fx_biquad_filter_t biquad;
        fx_biquad_filter_mono_t biquad_mono;
    };
    union {
        fx_dj_eq3_t eq3;
        fx_dj_eq3_mono_t eq3_mono;
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
    float eq_low_db;
    float eq_low_target_db;
    float eq_mid_db;
    float eq_mid_target_db;
    float eq_high_db;
    float eq_high_target_db;
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
    uint8_t synth_vca_type;
    uint8_t filter_env_prepared_count;
    uint8_t filter_env_prepared_consumed;
    uint8_t type;
    uint8_t dsp_format;
} mixer_track_filter_t;

_Static_assert(_Alignof(mixer_track_filter_t) >= 32U,
               "mixer track filter DSP storage must stay 32-byte aligned");
_Static_assert((sizeof(mixer_track_filter_t) % 32U) == 0U,
               "mixer track filter array stride must preserve DSP alignment");

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

static mixer_track_t g_tracks[MIXER_MAX_TRACKS];
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

static void mixer_track_filter_process_biquad_stereo_block(mixer_track_filter_t *filter,
                                                           float *left,
                                                           float *right,
                                                           uint32_t frames,
                                                           float cutoff_start_hz,
                                                           float cutoff_mod_start_hz,
                                                           float resonance_start,
                                                           float keytrack_ratio_start);
static uint8_t mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
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

static void mixer_poly_filter_sync_config(mixer_track_filter_t *dst,
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
    const uint8_t type_changed = (uint8_t)((full != 0U) && (dst->type != src->type));
    if (full != 0U)
    {
        dst->sample_rate = src->sample_rate;
        dst->keytrack = src->keytrack;
        dst->type = src->type;
        dst->filter_retrigger_hard = src->filter_retrigger_hard;
        dst->vca_retrigger_hard = src->vca_retrigger_hard;
        dst->synth_vca_type = src->synth_vca_type;
        vca_env_set_type(&dst->synth_vca_env,
                         (vca_env_type_t)dst->synth_vca_type);
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
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_RESONANCE]
            != src->continuous_version[MIXER_CONT_RESONANCE]))
    {
        dst->resonance_target = src->resonance_target;
        dst->continuous_version[MIXER_CONT_RESONANCE] = src->continuous_version[MIXER_CONT_RESONANCE];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_EQ_LOW]
            != src->continuous_version[MIXER_CONT_EQ_LOW]))
    {
        dst->eq_low_target_db = src->eq_low_target_db;
        dst->continuous_version[MIXER_CONT_EQ_LOW] = src->continuous_version[MIXER_CONT_EQ_LOW];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_EQ_MID]
            != src->continuous_version[MIXER_CONT_EQ_MID]))
    {
        dst->eq_mid_target_db = src->eq_mid_target_db;
        dst->continuous_version[MIXER_CONT_EQ_MID] = src->continuous_version[MIXER_CONT_EQ_MID];
    }
    if ((full != 0U) || (dst->continuous_version[MIXER_CONT_EQ_HIGH]
            != src->continuous_version[MIXER_CONT_EQ_HIGH]))
    {
        dst->eq_high_target_db = src->eq_high_target_db;
        dst->continuous_version[MIXER_CONT_EQ_HIGH] = src->continuous_version[MIXER_CONT_EQ_HIGH];
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
    if (type_changed != 0U)
    {
        dst->filter_env_prepared_consumed = 1U;
        mixer_track_filter_apply_core_params(dst);
    }
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
#define MIXER_LOOPER_RECORD_CLIENT_ID 0U
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
#define MIXER_EQ3_NUM_STAGES 3U
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

static uint8_t mixer_track_is_looper_ctx(const track_runtime_ctx_t *ctx)
{
    return (uint8_t)((ctx != 0)
            && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER));
}

static uint8_t mixer_track_is_looper(uint8_t logical_track)
{
    return mixer_track_is_looper_ctx(track_runtime_get_ctx(logical_track));
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

static uint8_t mixer_track_filter_type_is_biquad(mixer_track_filter_type_t type)
{
    return ((type == MIXER_TRACK_FILTER_LP_BI)
         || (type == MIXER_TRACK_FILTER_HP_BI)
         || (type == MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;
}

static fx_biquad_filter_mode_t mixer_track_filter_type_to_biquad_mode(mixer_track_filter_type_t type)
{
    switch(type)
    {
        case MIXER_TRACK_FILTER_HP_BI:
            return FX_BIQUAD_FILTER_MODE_HP;

        case MIXER_TRACK_FILTER_BP_BI:
            return FX_BIQUAD_FILTER_MODE_BP;

        case MIXER_TRACK_FILTER_LP_BI:
        default:
            return FX_BIQUAD_FILTER_MODE_LP;
    }
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
    if(filter == NULL)
        return;

    if (filter->dsp_format == (uint8_t)MIXER_FILTER_DSP_MONO)
    {
        filter->eq3_mono.inst.numStages = MIXER_EQ3_NUM_STAGES;
        filter->eq3_mono.inst.pCoeffs = filter->eq3_mono.coeffs;
        filter->eq3_mono.inst.pState = filter->eq3_mono.state;
        return;
    }

    filter->eq3.inst_l.numStages = MIXER_EQ3_NUM_STAGES;
    filter->eq3.inst_l.pCoeffs = filter->eq3.coeffs;
    filter->eq3.inst_l.pState = filter->eq3.state_l;
    filter->eq3.inst_r.numStages = MIXER_EQ3_NUM_STAGES;
    filter->eq3.inst_r.pCoeffs = filter->eq3.coeffs;
    filter->eq3.inst_r.pState = filter->eq3.state_r;
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
    mono.mode_xfade_remaining = stereo->mode_xfade_remaining;
    mono.bypass_xfade_remaining = stereo->bypass_xfade_remaining;
    mono.bypass_mix = stereo->bypass_mix;
    mono.mode = stereo->mode;
    mono.previous_mode = stereo->previous_mode;
    mono.bypass = stereo->bypass;
    mono.reset_after_bypass = stereo->reset_after_bypass;
    mono.mode_via_dry = stereo->mode_via_dry;
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
    stereo.mode_xfade_remaining = mono->mode_xfade_remaining;
    stereo.bypass_xfade_remaining = mono->bypass_xfade_remaining;
    stereo.bypass_mix = mono->bypass_mix;
    stereo.mode = mono->mode;
    stereo.previous_mode = mono->previous_mode;
    stereo.bypass = mono->bypass;
    stereo.reset_after_bypass = mono->reset_after_bypass;
    stereo.mode_via_dry = mono->mode_via_dry;
    filter->biquad = stereo;
}

static void mixer_track_filter_convert_eq3_to_mono(mixer_track_filter_t *filter)
{
    const fx_dj_eq3_t *const stereo = &filter->eq3;
    fx_dj_eq3_mono_t mono = {0};
    memcpy(mono.coeffs, stereo->coeffs, sizeof(mono.coeffs));
    memcpy(mono.coeffs_pending, stereo->coeffs_pending, sizeof(mono.coeffs_pending));
    memcpy(mono.state, stereo->state_l, sizeof(mono.state));
    mono.sample_rate = stereo->sample_rate;
    mono.low_freq = stereo->low_freq;
    mono.mid_freq = stereo->mid_freq;
    mono.high_freq = stereo->high_freq;
    mono.mid_q = stereo->mid_q;
    mono.low_db = stereo->low_db;
    mono.mid_db = stereo->mid_db;
    mono.high_db = stereo->high_db;
    mono.bypass = stereo->bypass;
    mono.coeffs_pending_update = stereo->coeffs_pending_update;
    filter->eq3_mono = mono;
}

static void mixer_track_filter_convert_eq3_to_stereo(mixer_track_filter_t *filter)
{
    const fx_dj_eq3_mono_t *const mono = &filter->eq3_mono;
    fx_dj_eq3_t stereo = {0};
    memcpy(stereo.coeffs, mono->coeffs, sizeof(stereo.coeffs));
    memcpy(stereo.coeffs_pending, mono->coeffs_pending, sizeof(stereo.coeffs_pending));
    memcpy(stereo.state_l, mono->state, sizeof(mono->state));
    memcpy(stereo.state_r, mono->state, sizeof(mono->state));
    stereo.sample_rate = mono->sample_rate;
    stereo.low_freq = mono->low_freq;
    stereo.mid_freq = mono->mid_freq;
    stereo.high_freq = mono->high_freq;
    stereo.mid_q = mono->mid_q;
    stereo.low_db = mono->low_db;
    stereo.mid_db = mono->mid_db;
    stereo.high_db = mono->high_db;
    stereo.bypass = mono->bypass;
    stereo.coeffs_pending_update = mono->coeffs_pending_update;
    filter->eq3 = stereo;
}

static void mixer_track_filter_set_dsp_format(mixer_track_filter_t *filter,
                                               mixer_filter_dsp_format_t format)
{
    if ((filter == NULL) || (filter->dsp_format == (uint8_t)format))
        return;

    if (format == MIXER_FILTER_DSP_MONO)
    {
        mixer_track_filter_convert_biquad_to_mono(filter);
        mixer_track_filter_convert_eq3_to_mono(filter);
    }
    else
    {
        mixer_track_filter_convert_biquad_to_stereo(filter);
        mixer_track_filter_convert_eq3_to_stereo(filter);
    }
    filter->dsp_format = (uint8_t)format;
    mixer_track_filter_rebind_dsp_storage(filter);
}

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    const uint8_t is_biquad =
        mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type);
    const uint8_t is_eq3 =
        (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;
    if (filter->dsp_format == (uint8_t)MIXER_FILTER_DSP_MONO)
    {
        fx_biquad_filter_mono_set_sample_rate(&filter->biquad_mono, filter->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&filter->biquad_mono, filter->cutoff_hz);
        fx_biquad_filter_mono_set_q(
            &filter->biquad_mono,
            mixer_track_filter_resonance_to_biquad_q(filter->resonance));
        if(is_biquad != 0U)
        {
            fx_biquad_filter_mono_set_mode(
                &filter->biquad_mono,
                mixer_track_filter_type_to_biquad_mode(
                    (mixer_track_filter_type_t)filter->type));
        }
        fx_biquad_filter_mono_set_bypass(&filter->biquad_mono,
                                         (is_biquad != 0U) ? 0U : 1U);
        fx_dj_eq3_mono_set_gains_db(&filter->eq3_mono,
                                    filter->eq_low_db,
                                    filter->eq_mid_db,
                                    filter->eq_high_db);
        fx_dj_eq3_mono_set_bypass(&filter->eq3_mono,
                                  (is_eq3 != 0U) ? 0U : 1U);
        return;
    }

    fx_biquad_filter_set_cutoff(&filter->biquad, filter->cutoff_hz);
    fx_biquad_filter_set_q(
        &filter->biquad,
        mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    if(is_biquad != 0U)
    {
        fx_biquad_filter_set_mode(
            &filter->biquad,
            mixer_track_filter_type_to_biquad_mode(
                (mixer_track_filter_type_t)filter->type));
    }
    fx_biquad_filter_set_bypass(&filter->biquad,
                                (is_biquad != 0U) ? 0U : 1U);

    fx_dj_eq3_set_gains_db(&filter->eq3,
                           filter->eq_low_db,
                           filter->eq_mid_db,
                           filter->eq_high_db);
    fx_dj_eq3_set_bypass(&filter->eq3, (is_eq3 != 0U) ? 0U : 1U);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    filter->dsp_format = (uint8_t)MIXER_FILTER_DSP_STEREO;
    fx_biquad_filter_init(&filter->biquad, filter->sample_rate);
    fx_dj_eq3_init(&filter->eq3, filter->sample_rate,
                   300.0f, 1000.0f, 0.8f, 4000.0f);
    fx_biquad_filter_reset(&filter->biquad);
    mixer_track_filter_rebind_dsp_storage(filter);
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
    filter->eq_low_db = 0.0f;
    filter->eq_low_target_db = 0.0f;
    filter->eq_mid_db = 0.0f;
    filter->eq_mid_target_db = 0.0f;
    filter->eq_high_db = 0.0f;
    filter->eq_high_target_db = 0.0f;
    filter->type = (uint8_t)MIXER_TRACK_FILTER_OFF;
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
    filter->synth_vca_type = (uint8_t)VCA_ENV_TYPE_DAISY;

    mixer_track_filter_reset_dsp(filter);
}

static void mixer_track_state_reset(mixer_track_t *track)
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
    static mixer_track_t previous_tracks[MIXER_MAX_TRACKS];
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

    mixer_external_inputs_clear();
}

void mixer_rebind_track_state(uint8_t previous_mix_track, uint8_t next_mix_track)
{
    mixer_track_t previous_track = { 0 };
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
}

void mixer_snap_track_runtime_state(uint32_t track_id)
{
    if (track_id >= MIXER_MAX_TRACKS)
    {
        return;
    }

    mixer_track_t *const track = &g_tracks[track_id];
    mixer_track_filter_t *const filter = &g_track_filters[track_id];

    track->gain_current = track->gain;
    track->pan_current = track->pan;
    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
    {
        track->send_level_current[s] = track->send_level[s];
    }

    filter->cutoff_hz = filter->cutoff_target_hz;
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = filter->resonance_target;
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    filter->eq_low_db = filter->eq_low_target_db;
    filter->eq_mid_db = filter->eq_mid_target_db;
    filter->eq_high_db = filter->eq_high_target_db;
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_process_block(mixer_track_filter_t *filter,
                                             float *left,
                                             float *right,
                                             uint32_t frames)
{
    if((filter == NULL) || (left == NULL) || (right == NULL))
        return;

    mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_STEREO);

    if((filter->type == (uint8_t)MIXER_TRACK_FILTER_OFF)
            && (filter->biquad.bypass_xfade_remaining == 0U))
    {
        filter->filter_env_prepared_consumed = 1U;
        return;
    }

    const float cutoff_start_hz = filter->cutoff_hz;
    const float cutoff_mod_start_hz = filter->cutoff_mod_hz;
    const float resonance_start = filter->resonance;
    const float keytrack_ratio_start = filter->keytrack_ratio;
    const float eq_low_start_db = filter->eq_low_db;
    const float eq_mid_start_db = filter->eq_mid_db;
    const float eq_high_start_db = filter->eq_high_db;
    filter->cutoff_hz = mixer_smooth_block(cutoff_start_hz,
                                           filter->cutoff_target_hz,
                                           MIXER_FILTER_BLOCK_SMOOTH);
    filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
    filter->resonance = mixer_smooth_block(resonance_start, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
    filter->keytrack_ratio = filter->keytrack_ratio_target;
    filter->eq_low_db = mixer_smooth_block(eq_low_start_db, filter->eq_low_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_mid_db = mixer_smooth_block(eq_mid_start_db, filter->eq_mid_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_high_db = mixer_smooth_block(eq_high_start_db, filter->eq_high_target_db, MIXER_FILTER_BLOCK_SMOOTH);

    switch((mixer_track_filter_type_t)filter->type)
    {
        case MIXER_TRACK_FILTER_EQ3:
            fx_dj_eq3_set_gains_db(&filter->eq3,
                                   filter->eq_low_db,
                                   filter->eq_mid_db,
                                   filter->eq_high_db);
            fx_dj_eq3_process_block(&filter->eq3, left, right, frames);
            filter->filter_env_prepared_consumed = 1U;
            break;

        case MIXER_TRACK_FILTER_LP_BI:
        case MIXER_TRACK_FILTER_HP_BI:
        case MIXER_TRACK_FILTER_BP_BI:
        case MIXER_TRACK_FILTER_OFF:
            mixer_track_filter_process_biquad_stereo_block(filter, left, right, frames,
                                                           cutoff_start_hz,
                                                           cutoff_mod_start_hz,
                                                           resonance_start,
                                                           keytrack_ratio_start);
            break;

        default:
            break;
    }
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
    if ((mixer_track_filter_env_control_is_static(filter, frames) != 0U)
            && (filter->biquad.mode_xfade_remaining == 0U)
            && (filter->biquad.bypass_xfade_remaining == 0U))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float modulation_hz = filter->cutoff_mod_hz
            - filter->cutoff_target_hz;
        fx_biquad_filter_set_params(
            &filter->biquad,
            mixer_track_filter_compute_modulated_cutoff(filter,
                                                         filter->cutoff_hz,
                                                         modulation_hz,
                                                         filter->keytrack_ratio,
                                                         env),
            mixer_track_filter_resonance_to_biquad_q(filter->resonance));
        fx_biquad_filter_process_block(&filter->biquad, left, right, frames);
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
        fx_biquad_filter_set_params(
                &filter->biquad,
                mixer_track_filter_compute_modulated_cutoff(filter, base_hz,
                                                            modulation_hz,
                                                            keytrack_ratio, env),
                mixer_track_filter_resonance_to_biquad_q(resonance));

        fx_biquad_filter_process_block(&filter->biquad, &left[i], &right[i], chunk);
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
    if ((control_static != 0U)
            && (filter->biquad_mono.mode_xfade_remaining == 0U)
            && (filter->biquad_mono.bypass_xfade_remaining == 0U))
    {
        const float env = mixer_track_filter_static_env_value(filter, frames);
        const float base_hz = (poly_cutoff_valid != 0U)
            ? poly_cutoff_hz : filter->cutoff_hz;
        const float modulation_hz = (poly_cutoff_valid != 0U)
            ? 0.0f : (filter->cutoff_mod_hz - filter->cutoff_target_hz);
        const float cutoff_arg = mixer_track_filter_compute_modulated_cutoff(filter,
            base_hz, modulation_hz, filter->keytrack_ratio, env);
        const float q_arg = mixer_track_filter_resonance_to_biquad_q(filter->resonance);
        fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);
        fx_biquad_filter_mono_process_block(&filter->biquad_mono, mono, frames);
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
        const float q_arg = mixer_track_filter_resonance_to_biquad_q(resonance);
        fx_biquad_filter_mono_set_params(&filter->biquad_mono, cutoff_arg, q_arg);

        fx_biquad_filter_mono_process_block(&filter->biquad_mono, &mono[i], chunk);
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static mixer_lane_plan_t mixer_build_lane_plan(uint32_t track_id,
                                               const mixer_track_t *track,
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

static void mixer_lane_accumulate_external_source(uint32_t track_id,
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

static mixer_lane_buffers_t mixer_lane_run_mono_native_path(uint32_t track_id,
                                                            const mixer_track_t *track,
                                                            mixer_track_filter_t *filter,
                                                            uint32_t frames,
                                                            uint8_t diag_lane)
{
    mixer_lane_buffers_t buffers = {0};

    if ((track == NULL) || (filter == NULL))
    {
        return buffers;
    }

    if (diag_lane != 0U)
    {
        audio_track_diag_measure_mono(AUDIO_TRACK_DIAG_FILTER_IN,
                                      g_external_track_mono[track_id],
                                      frames);
        audio_track_diag_filter_scope(filter->type != (uint8_t)MIXER_TRACK_FILTER_OFF);
    }
    (void)mixer_track_filter_process_block_mono(filter, g_external_track_mono[track_id], frames, NULL);
    if (diag_lane != 0U)
    {
        audio_track_diag_filter_scope(0U);
        audio_track_diag_measure_mono(AUDIO_TRACK_DIAG_FILTER_OUT,
                                      g_external_track_mono[track_id],
                                      frames);
    }

    buffers.mono = g_external_track_mono[track_id];
    return buffers;
}

static void mixer_lane_run_stereo_path(uint32_t track_id,
                                       const mixer_track_t *track,
                                       mixer_track_filter_t *filter,
                                       float *left,
                                       float *right,
                                       uint32_t frames,
                                       uint8_t diag_lane)
{
    if ((track == NULL) || (filter == NULL) || (left == NULL) || (right == NULL))
    {
        return;
    }

    if (diag_lane != 0U)
    {
        audio_track_diag_measure_stereo(AUDIO_TRACK_DIAG_FILTER_IN, left, right, frames);
        audio_track_diag_filter_scope(filter->type != (uint8_t)MIXER_TRACK_FILTER_OFF);
    }
    mixer_track_filter_process_block(filter, left, right, frames);
    if (diag_lane != 0U)
    {
        audio_track_diag_filter_scope(0U);
        audio_track_diag_measure_stereo(AUDIO_TRACK_DIAG_FILTER_OUT, left, right, frames);
    }
}

static uint8_t mixer_track_filter_process_block_mono(mixer_track_filter_t *filter,
                                                     float *mono,
                                                     uint32_t frames,
                                                     const mixer_poly_cutoff_override_t *poly_cutoff)
{
    if((filter == NULL) || (mono == NULL))
    {
        return 0U;
    }

    mixer_track_filter_set_dsp_format(filter, MIXER_FILTER_DSP_MONO);

    if((filter->type == (uint8_t)MIXER_TRACK_FILTER_OFF)
            && (filter->biquad_mono.bypass_xfade_remaining == 0U))
    {
        filter->filter_env_prepared_consumed = 1U;
        return 1U;
    }

    if (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3)
    {
        const float eq_low_start_db = filter->eq_low_db;
        const float eq_mid_start_db = filter->eq_mid_db;
        const float eq_high_start_db = filter->eq_high_db;
        filter->cutoff_hz = mixer_smooth_block(filter->cutoff_hz,
                                               filter->cutoff_target_hz,
                                               MIXER_FILTER_BLOCK_SMOOTH);
        filter->cutoff_mod_hz = filter->cutoff_mod_target_hz;
        filter->resonance = mixer_smooth_block(filter->resonance, filter->resonance_target, MIXER_FILTER_BLOCK_SMOOTH);
        filter->eq_low_db = mixer_smooth_block(eq_low_start_db, filter->eq_low_target_db, MIXER_FILTER_BLOCK_SMOOTH);
        filter->eq_mid_db = mixer_smooth_block(eq_mid_start_db, filter->eq_mid_target_db, MIXER_FILTER_BLOCK_SMOOTH);
        filter->eq_high_db = mixer_smooth_block(eq_high_start_db, filter->eq_high_target_db, MIXER_FILTER_BLOCK_SMOOTH);
        fx_dj_eq3_mono_set_gains_db(&filter->eq3_mono,
                                    filter->eq_low_db,
                                    filter->eq_mid_db,
                                    filter->eq_high_db);
        fx_dj_eq3_mono_process_block(&filter->eq3_mono, mono, frames);
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
    filter->eq_low_db = mixer_smooth_block(filter->eq_low_db, filter->eq_low_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_mid_db = mixer_smooth_block(filter->eq_mid_db, filter->eq_mid_target_db, MIXER_FILTER_BLOCK_SMOOTH);
    filter->eq_high_db = mixer_smooth_block(filter->eq_high_db, filter->eq_high_target_db, MIXER_FILTER_BLOCK_SMOOTH);
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

    const uint8_t is_biquad = mixer_track_filter_type_is_biquad(slot->filter_type);
    const uint8_t is_eq3 = (slot->filter_type == MIXER_TRACK_FILTER_EQ3) ? 1U : 0U;
    if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
    {
        fx_biquad_filter_set_sample_rate(&slot->filter.stereo.biquad, slot->sample_rate);
        fx_biquad_filter_set_cutoff(&slot->filter.stereo.biquad, slot->cutoff_hz);
        fx_biquad_filter_set_q(&slot->filter.stereo.biquad,
                               mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        if (is_biquad != 0U)
        {
            fx_biquad_filter_set_mode(&slot->filter.stereo.biquad,
                                      mixer_track_filter_type_to_biquad_mode(slot->filter_type));
        }
        fx_biquad_filter_set_bypass(&slot->filter.stereo.biquad,
                                    (is_biquad != 0U) ? 0U : 1U);
        fx_dj_eq3_set_gains_db(&slot->filter.stereo.eq3,
                               slot->eq_low_db,
                               slot->eq_mid_db,
                               slot->eq_high_db);
        fx_dj_eq3_set_bypass(&slot->filter.stereo.eq3, (is_eq3 == 0U) ? 1U : 0U);
    }
    else
    {
        fx_biquad_filter_mono_set_sample_rate(&slot->filter.mono.biquad,
                                              slot->sample_rate);
        fx_biquad_filter_mono_set_cutoff(&slot->filter.mono.biquad, slot->cutoff_hz);
        fx_biquad_filter_mono_set_q(&slot->filter.mono.biquad,
                                    mixer_track_filter_resonance_to_biquad_q(slot->resonance));
        if (is_biquad != 0U)
        {
            fx_biquad_filter_mono_set_mode(&slot->filter.mono.biquad,
                                           mixer_track_filter_type_to_biquad_mode(slot->filter_type));
        }
        fx_biquad_filter_mono_set_bypass(&slot->filter.mono.biquad,
                                         (is_biquad != 0U) ? 0U : 1U);
        fx_dj_eq3_mono_set_gains_db(&slot->filter.mono.eq3,
                                    slot->eq_low_db,
                                    slot->eq_mid_db,
                                    slot->eq_high_db);
        fx_dj_eq3_mono_set_bypass(&slot->filter.mono.eq3, (is_eq3 == 0U) ? 1U : 0U);
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
    const mixer_track_filter_type_t previous_type = slot->filter_type;
    slot->cutoff_target_hz = source->cutoff_target_hz;
    slot->cutoff_mod_target_hz = source->cutoff_mod_target_hz;
    slot->resonance_target = source->resonance_target;
    slot->eg_amount = source->eg_amount;
    slot->keytrack = source->keytrack;
    slot->eq_low_target_db = source->eq_low_target_db;
    slot->eq_mid_target_db = source->eq_mid_target_db;
    slot->eq_high_target_db = source->eq_high_target_db;
    slot->filter_type = source->type;
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
    if (previous_type != slot->filter_type)
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
    const uint8_t transition_active =
        (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
        ? (uint8_t)((slot->filter.stereo.biquad.mode_xfade_remaining != 0U)
            || (slot->filter.stereo.biquad.bypass_xfade_remaining != 0U))
        : (uint8_t)((slot->filter.mono.biquad.mode_xfade_remaining != 0U)
            || (slot->filter.mono.biquad.bypass_xfade_remaining != 0U));
    if ((env_static != 0U) && (transition_active == 0U))
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
        const float q = mixer_track_filter_resonance_to_biquad_q(slot->resonance);
        (void)terminal_value;
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
    slot->eq_low_db = mixer_smooth_block(slot->eq_low_db,
                                         slot->eq_low_target_db,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->eq_mid_db = mixer_smooth_block(slot->eq_mid_db,
                                         slot->eq_mid_target_db,
                                         MIXER_FILTER_BLOCK_SMOOTH);
    slot->eq_high_db = mixer_smooth_block(slot->eq_high_db,
                                          slot->eq_high_target_db,
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

    const uint8_t filter_is_off =
        (slot->filter_type == MIXER_TRACK_FILTER_OFF) ? 1U : 0U;
    const uint8_t filter_bypass_complete =
        ((slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
         && (slot->filter.stereo.biquad.bypass_xfade_remaining == 0U))
        || ((slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_MONO)
            && (slot->filter.mono.biquad.bypass_xfade_remaining == 0U));
    if ((filter_is_off == 0U) || (filter_bypass_complete == 0U))
    {
        if (slot->filter_type == MIXER_TRACK_FILTER_EQ3)
        {
            if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
            {
                fx_dj_eq3_set_gains_db(&slot->filter.stereo.eq3,
                                       slot->eq_low_db,
                                       slot->eq_mid_db,
                                       slot->eq_high_db);
                fx_dj_eq3_process_block(&slot->filter.stereo.eq3,
                                        left,
                                        right,
                                        frames);
            }
            else
            {
                fx_dj_eq3_mono_set_gains_db(&slot->filter.mono.eq3,
                                            slot->eq_low_db,
                                            slot->eq_mid_db,
                                            slot->eq_high_db);
                fx_dj_eq3_mono_process_block(&slot->filter.mono.eq3,
                                             left,
                                             frames);
            }
        }
        else if ((mixer_track_filter_type_is_biquad(slot->filter_type) != 0U)
                 || (slot->filter_type == MIXER_TRACK_FILTER_OFF))
        {
            mixer_multi_filter_process_biquad(slot, left, right, frames);
        }
    }

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
    const uint8_t filter_is_off =
        (slot->filter_type == MIXER_TRACK_FILTER_OFF) ? 1U : 0U;
    const uint8_t filter_bypass_complete =
        (slot->filter.mono.biquad.bypass_xfade_remaining == 0U) ? 1U : 0U;
    if ((filter_is_off == 0U) || (filter_bypass_complete == 0U))
    {
        if (slot->filter_type == MIXER_TRACK_FILTER_EQ3)
        {
            fx_dj_eq3_mono_set_gains_db(&slot->filter.mono.eq3,
                                        slot->eq_low_db,
                                        slot->eq_mid_db,
                                        slot->eq_high_db);
            fx_dj_eq3_mono_process_block(&slot->filter.mono.eq3,
                                         mono,
                                         frames);
        }
        else if ((mixer_track_filter_type_is_biquad(slot->filter_type) != 0U)
                 || (slot->filter_type == MIXER_TRACK_FILTER_OFF))
        {
            mixer_multi_filter_process_biquad(slot, mono, NULL, frames);
        }
    }

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
 * Rôle:
 * - Exécuter le traitement associé à clamp01.
 *
 * @param v Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à clamp_pan.
 *
 * @param pan Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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

static mixer_track_coefficient_plan_t mixer_prepare_track_coefficients(
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
 * Rôle:
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

        mixer_track_filter_init(&g_track_filters[t], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

        if(t < MAX_TRACKS)
            track_set_gain(t, 1.0f);
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
    mixer_track_filter_init_time_lut();
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_master.
 *
 * @param gain Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_get_master.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_gain.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param gain Paramètre d'entrée de l'API.
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
    if(track_id < MAX_TRACKS)
        track_set_gain(track_id, gain);
}

/**
 * @brief Point d'entrée mixer_get_track_gain.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_get_track_gain.
 *
 * @param track_id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_pan.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param pan Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_mute.
 *
 * @param track_id Paramètre d'entrée de l'API.
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
 * @brief Point d'entrée mixer_set_track_route.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_route.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param route Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_route(uint32_t track_id, mixer_route_t route)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    route = ((route & MIXER_ROUTE_MASTER) != 0U) ? MIXER_ROUTE_MASTER : MIXER_ROUTE_NONE;
    g_tracks[track_id].route_master = ((route & MIXER_ROUTE_MASTER) != 0U) ? 1U : 0U;
}

/**
 * @brief Point d'entrée mixer_set_track_insert_slot.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_insert_slot.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param insert_idx Paramètre d'entrée de l'API.
 * @param slot Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_set_track_insert_slot(uint32_t track_id, uint32_t insert_idx, int8_t slot)
{
    if(track_id >= MIXER_MAX_TRACKS || insert_idx >= MIXER_INSERTS_PER_TRACK)
        return;

    g_tracks[track_id].insert_slot[insert_idx] = slot;
}

/**
 * @brief Point d'entrée mixer_set_track_send_level.
 *
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_track_send_level.
 *
 * @param track_id Paramètre d'entrée de l'API.
 * @param send_idx Paramètre d'entrée de l'API.
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_set_send_fx_slot.
 *
 * @param send_idx Paramètre d'entrée de l'API.
 * @param slot Paramètre d'entrée de l'API.
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

void mixer_set_track_filter_type(uint32_t track_id, mixer_track_filter_type_t type)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    if(type > MIXER_TRACK_FILTER_BP_BI)
        type = MIXER_TRACK_FILTER_BP_BI;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if(filter->type == (uint8_t)type)
    {
        return;
    }

    filter->type = (uint8_t)type;
    filter->filter_env_prepared_consumed = 1U;
    mixer_track_filter_apply_core_params(filter);
    mixer_track_filter_touch_config(filter);
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

void mixer_set_track_filter_eq_low(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if (filter->eq_low_target_db == gain_db) return;
    filter->eq_low_target_db = gain_db;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_EQ_LOW);
}

void mixer_set_track_filter_eq_mid(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if (filter->eq_mid_target_db == gain_db) return;
    filter->eq_mid_target_db = gain_db;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_EQ_MID);
}

void mixer_set_track_filter_eq_high(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if (filter->eq_high_target_db == gain_db) return;
    filter->eq_high_target_db = gain_db;
    mixer_track_filter_touch_continuous(filter, MIXER_CONT_EQ_HIGH);
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

void mixer_set_track_vca_env_type(uint32_t track_id, uint8_t type)
{
    if (track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint8_t next = (type == (uint8_t)VCA_ENV_TYPE_LINEAR)
                       ? (uint8_t)VCA_ENV_TYPE_LINEAR
                       : (uint8_t)VCA_ENV_TYPE_DAISY;
    if (filter->synth_vca_type == next)
        return;
    filter->synth_vca_type = next;
    vca_env_set_type(&filter->synth_vca_env, (vca_env_type_t)next);
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

uint8_t mixer_process_external_poly_voice_prepared(uint32_t mix_track_id,
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
    vca_env_set_type(&filter->synth_vca_env,
                     (vca_env_type_t)filter->synth_vca_type);
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
 * Rôle:
 * - Exécuter le traitement associé à mixer_process.
 *
 * @param tracks Paramètre d'entrée de l'API.
 * @param track_count Paramètre d'entrée de l'API.
 * @param frames Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void mixer_process(StereoTrack *tracks, uint32_t track_count, uint32_t frames)
{
    AUDIO_HOT ALIGN32 static float mono_pan_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float mono_pan_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_l[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float send_r[MIXER_NUM_SENDS][AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float reverb_return_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float reverb_return_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_return_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_return_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float delay_reverb_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_record_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_main_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float sample_capture_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float sample_capture_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static int32_t looper_record_i32[AUDIO_BLOCK_SIZE * MULTI_RECORD_WRITER_CHANNELS];
    AUDIO_HOT ALIGN32 static int32_t sample_capture_i32[AUDIO_BLOCK_SIZE * MULTI_RECORD_WRITER_CHANNELS];
    static uint8_t looper_output_active[MIXER_MAX_TRACKS];

    if(frames > AUDIO_BLOCK_SIZE)
        frames = AUDIO_BLOCK_SIZE;

    const uint8_t diag_enabled = audio_track_diag_is_enabled();
    if (diag_enabled != 0U)
    {
        audio_track_diag_set_lane_active(0U);
    }

    uint8_t send_fx_active = 0U;
    for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
    {
        if(s == MIXER_DELAY_SEND_INDEX)
        {
            continue;
        }
        if(g_send_fx_slot[s] >= 0)
        {
            send_fx_active = 1U;
            break;
        }
    }
    const uint8_t reverb_active = fx_reverb_global_is_active();
    const uint8_t delay_active = (g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            ? fx_delay_dual_global_is_active()
            : fx_delay_stereo_global_is_active();
    const uint8_t send_bus_active = ((send_fx_active != 0U) || (reverb_active != 0U) || (delay_active != 0U)) ? 1U : 0U;

    memset(bus_main_l, 0, sizeof(bus_main_l));
    memset(bus_main_r, 0, sizeof(bus_main_r));
    if(send_bus_active != 0U)
    {
        memset(send_l, 0, sizeof(send_l));
        memset(send_r, 0, sizeof(send_r));
    }
    memset(looper_output_active, 0, sizeof(looper_output_active));

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
    while (looper_mask != 0U)
    {
        const uint8_t logical_track = (uint8_t)__builtin_ctz((unsigned)looper_mask);
        looper_mask &= (uint16_t)(looper_mask - 1U);
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(logical_track);
        if((ctx != 0)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS)
                && (mixer_track_is_looper_ctx(ctx) != 0U)
                && (g_tracks[ctx->mix_track_id].mute == 0U)
                && (brick6_looper_runtime_is_playing(logical_track) != 0U))
        {
            looper_output_active[logical_track] = 1U;
            looper_playback_active = 1U;
            if(g_tracks[ctx->mix_track_id].route_master != 0U)
            {
                looper_playback_routes_main = 1U;
            }
        }
    }
    looper_playback_mix_active =
        ((looper_playback_active != 0U) && (looper_xfade_apply_active != 0U)) ? 1U : 0U;
    uint8_t looper_record_track = 0U;
    uint8_t diag_active_tracks = 0U;
    const uint8_t looper_record_active = mixer_looper_record_capture_is_active(&looper_record_track);
    const uint8_t sample_capture_active = sample_capture_audio_hook_is_enabled();
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
    if(sample_capture_active != 0U)
    {
        memset(sample_capture_l, 0, sizeof(sample_capture_l));
        memset(sample_capture_r, 0, sizeof(sample_capture_r));
    }

    const uint32_t valid_lane_mask = (MIXER_MAX_TRACKS >= 32U)
        ? UINT32_MAX : ((uint32_t)(1UL << MIXER_MAX_TRACKS) - 1U);
    uint32_t lane_mask = (g_external_lane_mask | audio_tracks_enabled_mask())
        & valid_lane_mask;
    while (lane_mask != 0U)
    {
        const uint32_t t = (uint32_t)__builtin_ctz(lane_mask);
        lane_mask &= lane_mask - 1U;
        mixer_track_t *mt = &g_tracks[t];
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
        if (diag_enabled != 0U)
        {
            diag_active_tracks++;
        }

        float *L = NULL;
        float *R = NULL;
        float *mono = NULL;
        const uint8_t diag_lane = ((diag_enabled != 0U)
            && (audio_track_diag_is_selected_mix_track((uint8_t)t) != 0U)) ? 1U : 0U;
        const uint8_t is_mono_native_lane = (lane_plan.exec_kind == MIXER_LANE_EXEC_MONO_NATIVE) ? 1U : 0U;
        const uint8_t multi_prefiltered =
            ((lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_MONO)
             || (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MULTI_STEREO)) ? 1U : 0U;

        if (is_mono_native_lane != 0U)
        {
            if (diag_lane != 0U)
            {
                audio_track_diag_set_lane_active(1U);
                audio_track_diag_set_filter_active(
                    (multi_prefiltered == 0U)
                        && (g_track_filters[t].type != (uint8_t)MIXER_TRACK_FILTER_OFF));
                audio_track_diag_measure_mono(AUDIO_TRACK_DIAG_ENG,
                                              g_external_track_mono[t],
                                              frames);
            }
            mixer_lane_buffers_t buffers = {0};
            if (multi_prefiltered != 0U)
            {
                buffers.mono = g_external_track_mono[t];
            }
            else
            {
                buffers = mixer_lane_run_mono_native_path(t,
                                                           mt,
                                                           &g_track_filters[t],
                                                           frames,
                                                           diag_lane);
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
            if (diag_lane != 0U)
            {
                audio_track_diag_set_lane_active(1U);
                audio_track_diag_set_filter_active(
                    (multi_prefiltered == 0U)
                        && (g_track_filters[t].type != (uint8_t)MIXER_TRACK_FILTER_OFF));
                audio_track_diag_measure_stereo(AUDIO_TRACK_DIAG_ENG, L, R, frames);
            }
            if ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO))
                mixer_lane_run_stereo_path(t, mt, &g_track_filters[t], L, R, frames, diag_lane);
        }

        /*
         * Common mono-native fan-out. Keep the historical L/R path when
         * an insert, diagnostic, or auxiliary capture/looper route needs the
         * materialized stereo buffers.  The direct path preserves the same
         * post-pan L/R values and the existing stereo send contracts, but
         * accumulates every destination while the source sample is live.
         */
        uint8_t direct_mono_fanout = 0U;
        if ((is_mono_native_lane != 0U)
                && (lane_plan.ext_format == MIXER_EXTERNAL_FORMAT_MONO_NATIVE)
                && (diag_lane == 0U)
                && (sample_capture_active == 0U)
                && (looper_record_active == 0U)
                && (looper_playback_mix_active == 0U))
        {
            uint8_t has_track_insert = 0U;
            for (uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
            {
                if (mt->insert_slot[insert] >= 0)
                {
                    has_track_insert = 1U;
                    break;
                }
            }
            direct_mono_fanout = (has_track_insert == 0U) ? 1U : 0U;
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
            float send_cur[MIXER_NUM_SENDS] = {0.0f};
            float send_step[MIXER_NUM_SENDS] = {0.0f};
            uint8_t send_enabled[MIXER_NUM_SENDS] = {0U};

            if (send_bus_active != 0U)
            {
                for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
                {
                    send_cur[s] = mt->send_level_current[s];
                    send_step[s] = (mt->send_level[s] - send_cur[s]) * inv_frames;
                    send_enabled[s] = (((s != MIXER_DELAY_SEND_INDEX)
                                        && (g_send_fx_slot[s] >= 0))
                        || ((reverb_active != 0U) && (s == MIXER_REVERB_SEND_INDEX))
                        || ((delay_active != 0U) && (s == MIXER_DELAY_SEND_INDEX))) ? 1U : 0U;
                }
            }

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

                const float sample_processed =
                    mono[i] * (mono_gain * vca_gain);
                const float left = sample_processed * pan_l;
                const float right = sample_processed * pan_r;
                const float left_trimmed = left * MIXER_TRACK_NOMINAL_TRIM;
                const float right_trimmed = right * MIXER_TRACK_NOMINAL_TRIM;

                if (send_bus_active != 0U)
                {
                    for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
                    {
                        if ((send_enabled[s] != 0U)
                                && !((send_cur[s] <= 0.0f)
                                    && (mt->send_level[s] <= 0.0f)))
                        {
                            send_l[s][i] += left_trimmed * send_cur[s];
                            send_r[s][i] += right_trimmed * send_cur[s];
                            send_cur[s] += send_step[s];
                        }
                    }
                }

                if (mt->route_master != 0U)
                {
                    bus_main_l[i] += left_trimmed;
                    bus_main_r[i] += right_trimmed;
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
            {
                for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
                {
                    mt->send_level_current[s] = mt->send_level[s];
                }
            }
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
                && (diag_lane == 0U)
                && (sample_capture_active == 0U)
                && (looper_record_active == 0U)
                && (looper_playback_mix_active == 0U)
                && (mt->route_master != 0U)
                )
        {
            uint8_t has_track_insert = 0U;
            for (uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
            {
                if (mt->insert_slot[insert] >= 0)
                {
                    has_track_insert = 1U;
                    break;
                }
            }
            poly_stereo_fanout = (has_track_insert == 0U) ? 1U : 0U;
        }

        if (poly_stereo_fanout != 0U)
        {
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

            if (send_bus_active != 0U)
            {
                for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
                {
                    const uint8_t send_enabled = (((s != MIXER_DELAY_SEND_INDEX)
                                                    && (g_send_fx_slot[s] >= 0))
                        || ((reverb_active != 0U) && (s == MIXER_REVERB_SEND_INDEX))
                        || ((delay_active != 0U) && (s == MIXER_DELAY_SEND_INDEX))) ? 1U : 0U;
                    send_cur[s] = mt->send_level_current[s];
                    send_step[s] = (mt->send_level[s] - send_cur[s]) * inv_frames;
                    if ((send_enabled != 0U)
                            && !((send_cur[s] <= 0.0f) && (mt->send_level[s] <= 0.0f)))
                    {
                        fanout_mode |= (s == MIXER_REVERB_SEND_INDEX)
                            ? POLY_FANOUT_REVERB : POLY_FANOUT_DELAY;
                    }
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
                        const float left_trimmed = L[i] * gain_l * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = R[i] * gain_r * MIXER_TRACK_NOMINAL_TRIM;
                        bus_main_l[i] += left_trimmed;
                        bus_main_r[i] += right_trimmed;
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
                        const float left_trimmed = L[i] * gain_l * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = R[i] * gain_r * MIXER_TRACK_NOMINAL_TRIM;
                        send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        bus_main_l[i] += left_trimmed;
                        bus_main_r[i] += right_trimmed;
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
                        const float left_trimmed = L[i] * gain_l * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = R[i] * gain_r * MIXER_TRACK_NOMINAL_TRIM;
                        send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        bus_main_l[i] += left_trimmed;
                        bus_main_r[i] += right_trimmed;
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
                        const float left_trimmed = L[i] * gain_l * MIXER_TRACK_NOMINAL_TRIM;
                        const float right_trimmed = R[i] * gain_r * MIXER_TRACK_NOMINAL_TRIM;
                        send_l[MIXER_REVERB_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        send_r[MIXER_REVERB_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_REVERB_SEND_INDEX];
                        send_l[MIXER_DELAY_SEND_INDEX][i] += left_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        send_r[MIXER_DELAY_SEND_INDEX][i] += right_trimmed * send_cur[MIXER_DELAY_SEND_INDEX];
                        bus_main_l[i] += left_trimmed;
                        bus_main_r[i] += right_trimmed;
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
            {
                for (uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
                {
                    mt->send_level_current[s] = mt->send_level[s];
                }
            }
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

            if (diag_lane != 0U)
            {
                for(uint32_t i = 0; i < frames; i++)
                {
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
                    float vca_gain = 1.0f;
                    if (track_vca_enabled != 0U)
                    {
                        vca_gain = vca_segment_gain[i];
                        g_track_filters[t].vca_env_value = vca_gain;
                    }
                    if (is_mono_native_lane != 0U)
                    {
                        audio_track_diag_measure_sample(AUDIO_TRACK_DIAG_DSP,
                                                       mono[i] * vca_gain,
                                                       mono[i] * vca_gain);
                        mono[i] *= (mono_gain * vca_gain);
                        mono_pan_l[i] = mono[i] * pan_l;
                        mono_pan_r[i] = mono[i] * pan_r;
                    }
                    else
                    {
                        audio_track_diag_measure_sample(AUDIO_TRACK_DIAG_DSP,
                                                       L[i] * vca_gain,
                                                       R[i] * vca_gain);
                        L[i] *= (gain_l * vca_gain);
                        R[i] *= (gain_r * vca_gain);
                    }

                    if (coefficient_plan.stable == 0U)
                    {
                        mixer_advance_track_ramps(&gain_cur, gain_step,
                                                  &pan_cur, pan_step,
                                                  &mute_gain_cur, mute_target, mute_step);
                    }
                }
            }
            else
            {
                for(uint32_t i = 0; i < frames; i++)
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
                    float vca_gain = 1.0f;
                    if (track_vca_enabled != 0U)
                    {
                        vca_gain = vca_segment_gain[i];
                        g_track_filters[t].vca_env_value = vca_gain;
                    }
                    if (is_mono_native_lane != 0U)
                    {
                        mono[i] *= (mono_gain * vca_gain);
                        mono_pan_l[i] = mono[i] * pan_l;
                        mono_pan_r[i] = mono[i] * pan_r;
                    }
                    else
                    {
                        L[i] *= (gain_l * vca_gain);
                        R[i] *= (gain_r * vca_gain);
                    }
                    if (coefficient_plan.stable == 0U)
                    {
                        mixer_advance_track_ramps(&gain_cur, gain_step,
                                                  &pan_cur, pan_step,
                                                  &mute_gain_cur, mute_target, mute_step);
                    }
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

        /*
         * One functional order for every lane:
         * engine -> filter -> VCA/track volume/pan -> track inserts -> sends/bus.
         * Mono-native lanes use the same stereo insert contract after panning.
         */
        for(uint32_t insert = 0U; insert < MIXER_INSERTS_PER_TRACK; ++insert)
        {
            const int8_t slot = mt->insert_slot[insert];
            if(slot >= 0)
            {
                fx_chain_process_slot_for_track(t, (uint32_t)slot, L, R, frames);
            }
        }
        if(diag_lane != 0U)
        {
            for(uint32_t i = 0U; i < frames; ++i)
            {
                audio_track_diag_measure_sample(AUDIO_TRACK_DIAG_BUS,
                    (mt->route_master != 0U)
                        ? L[i] * MIXER_TRACK_NOMINAL_TRIM : 0.0f,
                    (mt->route_master != 0U)
                        ? R[i] * MIXER_TRACK_NOMINAL_TRIM : 0.0f);
            }
        }

        {
            uint8_t source_track = (uint8_t)t;
            (void)track_runtime_get_logical_track_for_mix_track((uint8_t)t, &source_track);
            if((source_track < MIXER_MAX_TRACKS) && (mixer_track_is_looper(source_track) != 0U))
            {
                if((sample_capture_active != 0U)
                        && (sample_capture_model_source_track_is_enabled(source_track) != 0U))
                {
                    for(uint32_t i = 0U; i < frames; ++i)
                    {
                        sample_capture_l[i] += L[i];
                        sample_capture_r[i] += R[i];
                    }
                }

                if((looper_playback_mix_active != 0U) && (looper_output_active[source_track] != 0U))
                {
                    if(mt->route_master != 0U)
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
                    && (ui_core_runtime_bridge_get_looper_route_enabled(looper_record_track, source_track) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    looper_record_l[i] += L[i];
                    looper_record_r[i] += R[i];
                }
            }

            if((sample_capture_active != 0U)
                    && (source_track < MIXER_MAX_TRACKS)
                    && (sample_capture_model_source_track_is_enabled(source_track) != 0U))
            {
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    sample_capture_l[i] += L[i];
                    sample_capture_r[i] += R[i];
                }
            }
        }

        if(send_bus_active != 0U)
        {
            float send_cur[MIXER_NUM_SENDS];
            float send_step[MIXER_NUM_SENDS];
            for(uint32_t s = 0U; s < MIXER_NUM_SENDS; ++s)
            {
                send_cur[s] = mt->send_level_current[s];
                send_step[s] = (mt->send_level[s] - send_cur[s]) * ((frames > 0U) ? (1.0f / (float)frames) : 0.0f);
            }

            for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
            {
                const uint8_t send_enabled = (((s != MIXER_DELAY_SEND_INDEX) && (g_send_fx_slot[s] >= 0))
                        || ((reverb_active != 0U) && (s == MIXER_REVERB_SEND_INDEX))
                        || ((delay_active != 0U) && (s == MIXER_DELAY_SEND_INDEX))) ? 1U : 0U;
                if(send_enabled != 0U)
                {
                    if((send_cur[s] <= 0.0f) && (mt->send_level[s] <= 0.0f))
                        continue;

                    for(uint32_t i = 0; i < frames; i++)
                    {
                        const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                        const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                        send_l[s][i] += l_nom * send_cur[s];
                        send_r[s][i] += r_nom * send_cur[s];
                        send_cur[s] += send_step[s];
                    }
                }

                mt->send_level_current[s] = mt->send_level[s];
            }
        }

        if(mt->route_master)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                bus_main_l[i] += l_nom;
                bus_main_r[i] += r_nom;
            }
        }
    }

    if (diag_enabled != 0U)
    {
        audio_global_diag_set_active_tracks(diag_active_tracks);
        if(send_bus_active != 0U)
        {
            audio_global_diag_measure_three(AUDIO_GLOBAL_DIAG_DRY_SUM,
                                            bus_main_l, bus_main_r,
                                            AUDIO_GLOBAL_DIAG_SEND1,
                                            send_l[MIXER_REVERB_SEND_INDEX],
                                            send_r[MIXER_REVERB_SEND_INDEX],
                                            AUDIO_GLOBAL_DIAG_SEND2,
                                            send_l[MIXER_DELAY_SEND_INDEX],
                                            send_r[MIXER_DELAY_SEND_INDEX],
                                            frames);
        }
        else
        {
            audio_global_diag_measure_stereo(AUDIO_GLOBAL_DIAG_DRY_SUM,
                                             bus_main_l, bus_main_r, frames);
            audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_SEND1,
                                              AUDIO_GLOBAL_DIAG_STATE_BYPASS);
            audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_SEND2,
                                              AUDIO_GLOBAL_DIAG_STATE_BYPASS);
        }
        audio_track_diag_end_block(frames);
    }

    if(looper_record_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * MULTI_RECORD_WRITER_CHANNELS;
            looper_record_i32[out] = mixer_looper_float_to_pcm24(looper_record_l[i]);
            looper_record_i32[out + 1U] = mixer_looper_float_to_pcm24(looper_record_r[i]);
        }
        brick6_looper_runtime_preroll_capture_from_irq(looper_record_track,
                                                       looper_record_i32,
                                                       frames);
        (void)multi_record_writer_push_audio_block_from_irq(MIXER_LOOPER_RECORD_CLIENT_ID,
                                                            looper_record_i32,
                                                            frames);
    }

    mixer_external_inputs_clear();

    if(send_bus_active != 0U)
    {
        if(delay_active != 0U)
        {
            if(g_delay_type == (uint8_t)MIXER_DELAY_TYPE_DUAL)
            {
                fx_delay_dual_global_process_block(send_l[MIXER_DELAY_SEND_INDEX],
                                                   send_r[MIXER_DELAY_SEND_INDEX],
                                                   delay_return_l,
                                                   delay_return_r,
                                                   delay_reverb_l,
                                                   delay_reverb_r,
                                                   frames);
            }
            else
            {
                fx_delay_stereo_global_process_block(send_l[MIXER_DELAY_SEND_INDEX],
                                                     send_r[MIXER_DELAY_SEND_INDEX],
                                                     delay_return_l,
                                                     delay_return_r,
                                                     delay_reverb_l,
                                                     delay_reverb_r,
                                                     frames);
            }
            if(diag_enabled != 0U)
            {
                audio_global_diag_measure_stereo(AUDIO_GLOBAL_DIAG_DELAY_RETURN,
                                                 delay_return_l,
                                                 delay_return_r,
                                                 frames);
            }
            for(uint32_t i = 0; i < frames; i++)
            {
                bus_main_l[i] += delay_return_l[i];
                bus_main_r[i] += delay_return_r[i];
                send_l[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_l[i];
                send_r[MIXER_REVERB_SEND_INDEX][i] += delay_reverb_r[i];
            }
        }
        else if(diag_enabled != 0U)
        {
            audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_DELAY_RETURN,
                                              AUDIO_GLOBAL_DIAG_STATE_BYPASS);
        }

        if(reverb_active != 0U)
        {
            if(diag_enabled == 0U)
            {
                fx_reverb_global_process_block_add(
                    send_l[MIXER_REVERB_SEND_INDEX],
                    send_r[MIXER_REVERB_SEND_INDEX],
                    bus_main_l,
                    bus_main_r,
                    frames);
            }
            else
            {
                fx_reverb_global_process_block(send_l[MIXER_REVERB_SEND_INDEX],
                                               send_r[MIXER_REVERB_SEND_INDEX],
                                               reverb_return_l,
                                               reverb_return_r,
                                               frames);
                if(diag_enabled != 0U)
                {
                    audio_global_diag_measure_stereo(AUDIO_GLOBAL_DIAG_REVERB_RETURN,
                                                     reverb_return_l,
                                                     reverb_return_r,
                                                     frames);
                }

                for(uint32_t i = 0; i < frames; i++)
                {
                    bus_main_l[i] += reverb_return_l[i];
                    bus_main_r[i] += reverb_return_r[i];
                }
            }
        }
        else if(diag_enabled != 0U)
        {
            audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_REVERB_RETURN,
                                              AUDIO_GLOBAL_DIAG_STATE_BYPASS);
        }

        for(uint32_t s = 0; s < MIXER_NUM_SENDS; s++)
        {
            if(s == MIXER_DELAY_SEND_INDEX)
            {
                continue;
            }
            const int8_t slot = g_send_fx_slot[s];
            if(slot >= 0)
            {
                fx_chain_process_slot((uint32_t)slot, send_l[s], send_r[s], frames);
                for(uint32_t i = 0; i < frames; i++)
                {
                    bus_main_l[i] += send_l[s][i];
                    bus_main_r[i] += send_r[s][i];
                }
            }
        }
    }
    else if(diag_enabled != 0U)
    {
        audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_DELAY_RETURN,
                                          AUDIO_GLOBAL_DIAG_STATE_BYPASS);
        audio_global_diag_set_stage_state(AUDIO_GLOBAL_DIAG_REVERB_RETURN,
                                          AUDIO_GLOBAL_DIAG_STATE_BYPASS);
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
    fx_chain_process_slot(2U, bus_main_l, bus_main_r, frames);

    if(sample_capture_active != 0U)
    {
        for(uint32_t i = 0U; i < frames; ++i)
        {
            const uint32_t out = i * MULTI_RECORD_WRITER_CHANNELS;
            sample_capture_i32[out] = mixer_looper_float_to_pcm24(sample_capture_l[i]);
            sample_capture_i32[out + 1U] = mixer_looper_float_to_pcm24(sample_capture_r[i]);
        }
        (void)sample_capture_push_audio_block_from_irq(sample_capture_i32, frames);
    }

    if(track_count > 0U)
    {
        if(diag_enabled != 0U)
        {
            audio_global_diag_measure_stereo(AUDIO_GLOBAL_DIAG_POST_RETURNS,
                                             bus_main_l, bus_main_r, frames);
        }
        memcpy(tracks[0].L, bus_main_l, sizeof(float) * frames);
        memcpy(tracks[0].R, bus_main_r, sizeof(float) * frames);
    }

}
