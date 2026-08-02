/**
 * @file mixer.c
 * @brief Moteur de mixage final track-based (gain/pan/mute/routing/inserts/sends).
 *
 * Rôle du module:
 * - Maintenir l'état runtime des tracks du mixer.
 * - Effectuer le mix final MAIN/CUE avec inserts et send FX.
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
#include "Audio/audio_track_diag.h"

#include "Audio/audio_xfade.h"
#include "env_adsr.h"
#include "fx_biquad_filter.h"
#include "fx_delay_dual.h"
#include "fx_delay_stereo.h"
#include "fx_reverb.h"
#include "Audio/spectral_window.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/synth_polyphony.h"
#include "Core/track_runtime.h"
#include "Audio/multi_voice_dsp.h"

#if defined(BRICK6_VARIANT_LOWCOST)
#define MIXER_HAS_CUE_BUS 0
#else
#define MIXER_HAS_CUE_BUS 1
#endif
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
    uint8_t route_cue;

    int8_t insert_slot[MIXER_INSERTS_PER_TRACK];
    float send_level[MIXER_NUM_SENDS];
    float send_level_current[MIXER_NUM_SENDS];
} mixer_track_t;

typedef struct {
    fx_biquad_filter_t biquad;
    fx_biquad_filter_mono_t biquad_mono;
    env_adsr_t filter_env;
    env_adsr_t vca_env;
    fx_dj_eq3_t eq3;
    fx_dj_eq3_mono_t eq3_mono;
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
    uint8_t note_active;
    uint8_t current_note;
    uint8_t vca_enabled;
    uint8_t vca_note_active;
    uint8_t vca_note_count;
    uint8_t vca_current_note;
    uint8_t vca_gate;
    uint8_t filter_retrigger_hard;
    uint8_t vca_retrigger_hard;
    float vca_env_value;
    float filter_env_value;
    int16_t filter_env_prepared_first[AUDIO_BLOCK_SIZE / 8U];
    int16_t filter_env_prepared_terminal[AUDIO_BLOCK_SIZE / 8U];
    uint16_t filter_env_prepared_frames;
    uint8_t filter_env_prepared_count;
    uint8_t filter_env_prepared_consumed;
    uint8_t type;
    uint32_t config_version;
} mixer_track_filter_t;

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
static mixer_track_filter_t g_track_filters[MIXER_MAX_TRACKS];
static uint32_t g_mixer_filter_config_version;
AUDIO_HOT static mixer_track_filter_t g_poly_filters_hot[SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET];
static AUDIO_HOT float g_external_track_mono[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_l[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static AUDIO_HOT float g_external_track_r[MIXER_MAX_TRACKS][AUDIO_BLOCK_SIZE];
static uint8_t g_external_track_enabled[MIXER_MAX_TRACKS];
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
                                                     uint32_t frames);
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
    if (dst->config_version == src->config_version)
    {
        return;
    }
    const uint8_t previous_type = dst->type;
    dst->sample_rate = src->sample_rate;
    dst->cutoff_target_hz = src->cutoff_target_hz;
    dst->cutoff_mod_target_hz = src->cutoff_mod_target_hz;
    dst->resonance_target = src->resonance_target;
    dst->eg_amount = src->eg_amount;
    dst->keytrack = src->keytrack;
    dst->eq_low_target_db = src->eq_low_target_db;
    dst->eq_mid_target_db = src->eq_mid_target_db;
    dst->eq_high_target_db = src->eq_high_target_db;
    dst->type = src->type;
    /*
     * A poly voice always needs its own amplitude envelope.  The track-level
     * VCA enable flag only selects the mono track processor; using it here
     * leaves free-running oscillators permanently audible and prevents the
     * allocator from ever observing an IDLE release.
     */
    dst->vca_enabled = 1U;
    dst->filter_retrigger_hard = src->filter_retrigger_hard;
    dst->vca_retrigger_hard = src->vca_retrigger_hard;
    env_adsr_set_attack(&dst->filter_env, src->filter_env.attack);
    env_adsr_set_decay(&dst->filter_env, src->filter_env.decay);
    env_adsr_set_sustain(&dst->filter_env, src->filter_env.sustain);
    env_adsr_set_release(&dst->filter_env, src->filter_env.release);
    env_adsr_set_attack(&dst->vca_env, src->vca_env.attack);
    env_adsr_set_decay(&dst->vca_env, src->vca_env.decay);
    env_adsr_set_sustain(&dst->vca_env, src->vca_env.sustain);
    env_adsr_set_release(&dst->vca_env, src->vca_env.release);
    dst->config_version = src->config_version;
    if (previous_type != dst->type)
    {
        dst->filter_env_prepared_consumed = 1U;
        mixer_track_filter_apply_core_params(dst);
    }
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
#define MIXER_REVERB_PREDELAY_MAX_S 0.090f
#define MIXER_ENV_ADSR_MAX_SEGMENT_SECONDS 30.0f
#define MIXER_EQ3_NUM_STAGES 3U
typedef struct
{
    float wet;
    float size;
    float decay;
    float damp;
    float pre_delay;
    float spectral_position;
    float spectral_width;
} mixer_reverb_state_t;

static AUDIO_HOT mixer_reverb_state_t g_reverb = {
    .wet = 0.0f,
    .size = 0.0f,
    .decay = 0.50f,
    .damp = 0.70f,
    .pre_delay = 0.50f,
    .spectral_position = 0.50f,
    .spectral_width = 1.0f,
};

static float g_delay_spectral_position = 0.50f;
static float g_delay_spectral_width = 1.0f;

static float mixer_reverb_predelay_ui_to_seconds(float v)
{
    const float clamped = (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
    return clamped * MIXER_REVERB_PREDELAY_MAX_S;
}


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

static uint8_t mixer_track_is_looper(uint8_t logical_track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(logical_track);
    return (uint8_t)((ctx != 0)
            && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_LOOPER));
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

static void mixer_track_filter_apply_core_params(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    fx_biquad_filter_set_cutoff(&filter->biquad, filter->cutoff_hz);
    fx_biquad_filter_set_q(&filter->biquad, mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    if(mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type) != 0U)
    {
        fx_biquad_filter_set_mode(&filter->biquad,
                                  mixer_track_filter_type_to_biquad_mode((mixer_track_filter_type_t)filter->type));
    }
    fx_biquad_filter_set_bypass(&filter->biquad,
                                (mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type) != 0U) ? 0U : 1U);

    fx_dj_eq3_set_gains_db(&filter->eq3,
                           filter->eq_low_db,
                           filter->eq_mid_db,
                           filter->eq_high_db);
    fx_dj_eq3_set_bypass(&filter->eq3, (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3) ? 0U : 1U);
    fx_dj_eq3_mono_set_low_db(&filter->eq3_mono, filter->eq_low_db);
    fx_dj_eq3_mono_set_mid_db(&filter->eq3_mono, filter->eq_mid_db);
    fx_dj_eq3_mono_set_high_db(&filter->eq3_mono, filter->eq_high_db);
    fx_dj_eq3_mono_set_bypass(&filter->eq3_mono, (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3) ? 0U : 1U);

    fx_biquad_filter_mono_set_sample_rate(&filter->biquad_mono, filter->sample_rate);
    fx_biquad_filter_mono_set_cutoff(&filter->biquad_mono, filter->cutoff_hz);
    fx_biquad_filter_mono_set_q(&filter->biquad_mono, mixer_track_filter_resonance_to_biquad_q(filter->resonance));
    if(mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type) != 0U)
    {
        fx_biquad_filter_mono_set_mode(&filter->biquad_mono,
                                       mixer_track_filter_type_to_biquad_mode((mixer_track_filter_type_t)filter->type));
    }
    fx_biquad_filter_mono_set_bypass(&filter->biquad_mono,
                                     (mixer_track_filter_type_is_biquad((mixer_track_filter_type_t)filter->type) != 0U) ? 0U : 1U);
}

static void mixer_track_filter_reset_dsp(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    fx_biquad_filter_init(&filter->biquad, filter->sample_rate);
    fx_biquad_filter_mono_init(&filter->biquad_mono, filter->sample_rate);
    fx_dj_eq3_init(&filter->eq3, filter->sample_rate, 300.0f, 1000.0f, 0.8f, 4000.0f);
    fx_dj_eq3_mono_init(&filter->eq3_mono, filter->sample_rate, 300.0f, 1000.0f, 0.8f, 4000.0f);
    fx_biquad_filter_reset(&filter->biquad);
    fx_biquad_filter_mono_reset(&filter->biquad_mono);
    mixer_track_filter_apply_core_params(filter);
}

static void mixer_track_filter_rebind_dsp_storage(mixer_track_filter_t *filter)
{
    if(filter == NULL)
        return;

    filter->eq3.inst_l.numStages = MIXER_EQ3_NUM_STAGES;
    filter->eq3.inst_l.pCoeffs = filter->eq3.coeffs;
    filter->eq3.inst_l.pState = filter->eq3.state_l;
    filter->eq3.inst_r.numStages = MIXER_EQ3_NUM_STAGES;
    filter->eq3.inst_r.pCoeffs = filter->eq3.coeffs;
    filter->eq3.inst_r.pState = filter->eq3.state_r;
    filter->eq3_mono.inst.numStages = MIXER_EQ3_NUM_STAGES;
    filter->eq3_mono.inst.pCoeffs = filter->eq3_mono.coeffs;
    filter->eq3_mono.inst.pState = filter->eq3_mono.state;
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

    fx_biquad_filter_mono_init(&filter->biquad_mono, filter->sample_rate);
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
    track->route_cue = 0U;

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
            for (uint32_t i = 0U; i < frames; i += MIXER_FILTER_UPDATE_PERIOD)
            {
                uint32_t chunk = frames - i;
                if (chunk > MIXER_FILTER_UPDATE_PERIOD)
                {
                    chunk = MIXER_FILTER_UPDATE_PERIOD;
                }
                const float progress = (float)(i + chunk) / (float)frames;
                fx_dj_eq3_set_gains_db(
                    &filter->eq3,
                    eq_low_start_db + ((filter->eq_low_db - eq_low_start_db) * progress),
                    eq_mid_start_db + ((filter->eq_mid_db - eq_mid_start_db) * progress),
                    eq_high_start_db + ((filter->eq_high_db - eq_high_start_db) * progress));
                fx_dj_eq3_process_block(&filter->eq3, &left[i], &right[i], chunk);
            }
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
        const float env_first = (float)first_value * (1.0f / 32767.0f);
        const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
        const float env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
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
                                                         float keytrack_ratio_start)
{
    uint32_t i = 0U;
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
        const float env_first = (float)first_value * (1.0f / 32767.0f);
        const float env_terminal = (float)terminal_value * (1.0f / 32767.0f);
        const float env = (prepared != 0U) ? env_first : (0.5f * (env_first + env_terminal));
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
        fx_biquad_filter_mono_set_params(
                &filter->biquad_mono,
                mixer_track_filter_compute_modulated_cutoff(filter, base_hz,
                                                            modulation_hz,
                                                            keytrack_ratio, env),
                mixer_track_filter_resonance_to_biquad_q(resonance));

        fx_biquad_filter_mono_process_block(&filter->biquad_mono, &mono[i], chunk);
        i += chunk;
    }
    filter->filter_env_prepared_consumed = 1U;
}

static void mixer_track_filter_sync_stereo_state_from_mono(mixer_track_filter_t *filter)
{
    if (filter == NULL)
    {
        return;
    }

    filter->biquad.ic1eq_l = filter->biquad_mono.ic1eq;
    filter->biquad.ic2eq_l = filter->biquad_mono.ic2eq;
    filter->biquad.ic1eq_r = filter->biquad_mono.ic1eq;
    filter->biquad.ic2eq_r = filter->biquad_mono.ic2eq;
    filter->biquad.current = filter->biquad_mono.current;
    filter->biquad.cutoff_hz = filter->biquad_mono.cutoff_hz;
    filter->biquad.q = filter->biquad_mono.q;
    filter->biquad.mode = filter->biquad_mono.mode;
    filter->biquad.previous_mode = filter->biquad_mono.previous_mode;
    filter->biquad.mode_xfade_remaining = filter->biquad_mono.mode_xfade_remaining;
    filter->biquad.bypass_xfade_remaining = filter->biquad_mono.bypass_xfade_remaining;
    filter->biquad.bypass_mix = filter->biquad_mono.bypass_mix;
    filter->biquad.bypass = filter->biquad_mono.bypass;
    filter->biquad.reset_after_bypass = filter->biquad_mono.reset_after_bypass;
    filter->biquad.mode_via_dry = filter->biquad_mono.mode_via_dry;
}

static void mixer_track_filter_sync_stereo_state_from_mono_eq3(mixer_track_filter_t *filter)
{
    if (filter == NULL)
    {
        return;
    }

    memcpy(filter->eq3.state_l, filter->eq3_mono.state, sizeof(filter->eq3_mono.state));
    memcpy(filter->eq3.state_r, filter->eq3_mono.state, sizeof(filter->eq3_mono.state));
    memcpy(filter->eq3.coeffs, filter->eq3_mono.coeffs, sizeof(filter->eq3_mono.coeffs));
    memcpy(filter->eq3.coeffs_pending, filter->eq3_mono.coeffs_pending, sizeof(filter->eq3_mono.coeffs_pending));
    filter->eq3.sample_rate = filter->eq3_mono.sample_rate;
    filter->eq3.low_freq = filter->eq3_mono.low_freq;
    filter->eq3.mid_freq = filter->eq3_mono.mid_freq;
    filter->eq3.high_freq = filter->eq3_mono.high_freq;
    filter->eq3.mid_q = filter->eq3_mono.mid_q;
    filter->eq3.low_db = filter->eq3_mono.low_db;
    filter->eq3.mid_db = filter->eq3_mono.mid_db;
    filter->eq3.high_db = filter->eq3_mono.high_db;
    filter->eq3.bypass = filter->eq3_mono.bypass;
    filter->eq3.coeffs_pending_update = filter->eq3_mono.coeffs_pending_update;
}

static uint8_t mixer_track_filter_supports_mono_native_path(const mixer_track_filter_t *filter)
{
    if (filter == NULL)
    {
        return 0U;
    }

    return ((filter->type == (uint8_t)MIXER_TRACK_FILTER_OFF)
            || (filter->type == (uint8_t)MIXER_TRACK_FILTER_EQ3)
            || (filter->type == (uint8_t)MIXER_TRACK_FILTER_LP_BI)
            || (filter->type == (uint8_t)MIXER_TRACK_FILTER_HP_BI)
            || (filter->type == (uint8_t)MIXER_TRACK_FILTER_BP_BI)) ? 1U : 0U;
}

static uint8_t mixer_track_insert_is_mono_native_compatible(int8_t slot)
{
    if (slot < 0)
    {
        return 1U;
    }

    fx_slot_t *fx_slot = fx_pool_get_slot((uint32_t)slot);
    if ((fx_slot == NULL) || (fx_slot->active == 0U))
    {
        return 1U;
    }

    return ((fx_type_t)fx_slot->type == FX_SAT) ? 1U : 0U;
}

static uint8_t mixer_track_supports_mono_native_path(const mixer_track_t *track,
                                                     const mixer_track_filter_t *filter)
{
    if ((track == NULL) || (filter == NULL))
    {
        return 0U;
    }

    if (mixer_track_filter_supports_mono_native_path(filter) == 0U)
    {
        return 0U;
    }

    for (uint32_t i = 0U; i < MIXER_INSERTS_PER_TRACK; ++i)
    {
        if (mixer_track_insert_is_mono_native_compatible(track->insert_slot[i]) == 0U)
        {
            return 0U;
        }
    }

    return 1U;
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
        if (mixer_track_supports_mono_native_path(track, filter) != 0U)
        {
            plan.exec_kind = MIXER_LANE_EXEC_MONO_NATIVE;
        }
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
                                                              StereoTrack *tracks,
                                                              uint32_t frames,
                                                              float *ext_mono_l,
                                                              float *ext_mono_r)
{
    mixer_lane_buffers_t buffers = {0};

    if ((plan == NULL) || (tracks == NULL) || (ext_mono_l == NULL) || (ext_mono_r == NULL))
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

    if (plan->source_kind == MIXER_LANE_SOURCE_EXT_MONO_NATIVE)
    {
        for (uint32_t i = 0U; i < plan->ext_frames; ++i)
        {
            const float s = g_external_track_mono[track_id][i];
            ext_mono_l[i] = s;
            ext_mono_r[i] = s;
        }
        for (uint32_t i = plan->ext_frames; i < frames; ++i)
        {
            ext_mono_l[i] = 0.0f;
            ext_mono_r[i] = 0.0f;
        }
        buffers.left = ext_mono_l;
        buffers.right = ext_mono_r;
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
                                                            float *ext_mono_l,
                                                            float *ext_mono_r,
                                                            uint8_t diag_lane)
{
    mixer_lane_buffers_t buffers = {0};

    if ((track == NULL) || (filter == NULL) || (ext_mono_l == NULL) || (ext_mono_r == NULL))
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
    (void)mixer_track_filter_process_block_mono(filter, g_external_track_mono[track_id], frames);
    if (diag_lane != 0U)
    {
        audio_track_diag_filter_scope(0U);
        audio_track_diag_measure_mono(AUDIO_TRACK_DIAG_FILTER_OUT,
                                      g_external_track_mono[track_id],
                                      frames);
    }

    (void)ext_mono_l;
    (void)ext_mono_r;
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
                                                     uint32_t frames)
{
    if((filter == NULL) || (mono == NULL))
    {
        return 0U;
    }

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
        for (uint32_t i = 0U; i < frames; i += MIXER_FILTER_UPDATE_PERIOD)
        {
            uint32_t chunk = frames - i;
            if (chunk > MIXER_FILTER_UPDATE_PERIOD)
            {
                chunk = MIXER_FILTER_UPDATE_PERIOD;
            }
            const float progress = (float)(i + chunk) / (float)frames;
            fx_dj_eq3_mono_set_gains_db(
                &filter->eq3_mono,
                eq_low_start_db + ((filter->eq_low_db - eq_low_start_db) * progress),
                eq_mid_start_db + ((filter->eq_mid_db - eq_mid_start_db) * progress),
                eq_high_start_db + ((filter->eq_high_db - eq_high_start_db) * progress));
            fx_dj_eq3_mono_process_block(&filter->eq3_mono, &mono[i], chunk);
        }
        filter->filter_env_prepared_consumed = 1U;
        mixer_track_filter_sync_stereo_state_from_mono_eq3(filter);
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
                                                keytrack_ratio_start);
    mixer_track_filter_sync_stereo_state_from_mono(filter);

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
    if (slot->filter_config_version == source->config_version)
    {
        return;
    }

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
    env_adsr_set_attack(&slot->filter_env, source->filter_env.attack);
    env_adsr_set_decay(&slot->filter_env, source->filter_env.decay);
    env_adsr_set_sustain(&slot->filter_env, source->filter_env.sustain);
    env_adsr_set_release(&slot->filter_env, source->filter_env.release);
    env_adsr_set_attack(&slot->vca_env, source->vca_env.attack);
    env_adsr_set_decay(&slot->vca_env, source->vca_env.decay);
    env_adsr_set_sustain(&slot->vca_env, source->vca_env.sustain);
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
        const float env = 0.5f * ((float)first_value + (float)terminal_value)
                        * (1.0f / 32767.0f);
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

void mixer_multi_filter_process(uint32_t track_id,
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
            for (uint32_t offset = 0U; offset < frames; offset += MIXER_FILTER_UPDATE_PERIOD)
            {
                uint32_t chunk = frames - offset;
                if (chunk > MIXER_FILTER_UPDATE_PERIOD)
                {
                    chunk = MIXER_FILTER_UPDATE_PERIOD;
                }
                if (slot->format == (uint8_t)MULTI_VOICE_DSP_FORMAT_STEREO)
                {
                    fx_dj_eq3_set_gains_db(&slot->filter.stereo.eq3,
                                           slot->eq_low_db,
                                           slot->eq_mid_db,
                                           slot->eq_high_db);
                    fx_dj_eq3_process_block(&slot->filter.stereo.eq3,
                                            &left[offset],
                                            &right[offset],
                                            chunk);
                }
                else
                {
                    fx_dj_eq3_mono_set_gains_db(&slot->filter.mono.eq3,
                                                slot->eq_low_db,
                                                slot->eq_mid_db,
                                                slot->eq_high_db);
                    fx_dj_eq3_mono_process_block(&slot->filter.mono.eq3,
                                                 &left[offset],
                                                 chunk);
                }
            }
        }
        else if ((mixer_track_filter_type_is_biquad(slot->filter_type) != 0U)
                 || (slot->filter_type == MIXER_TRACK_FILTER_OFF))
        {
            mixer_multi_filter_process_biquad(slot, left, right, frames);
        }
    }

    if (slot->vca_enabled != 0U)
    {
        for (uint32_t frame = 0U; frame < frames; ++frame)
        {
            const float vca = (float)env_adsr_process_step(&slot->vca_env)
                            * (1.0f / 32767.0f);
            left[frame] *= vca;
            right[frame] *= vca;
        }
    }
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
    g_reverb.size = 0.0f;
    g_reverb.decay = 0.50f;
    g_reverb.damp = 0.70f;
    g_reverb.pre_delay = 0.50f;
    g_reverb.spectral_position = 0.50f;
    g_reverb.spectral_width = 1.0f;
    g_delay_spectral_position = 0.50f;
    g_delay_spectral_width = 1.0f;
}

static void mixer_apply_reverb_spectral_window(void)
{
    spectral_window_result_t result;
    spectral_window_calculate(g_reverb.spectral_position,
                              g_reverb.spectral_width,
                              spectral_window_reverb_limits(),
                              &result);
    fx_reverb_global_set_filter_hz(result.low_cut_hz, result.high_cut_hz);
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
    fx_reverb_global_set_size(g_reverb.size);
    fx_reverb_global_set_decay(g_reverb.decay);
    fx_reverb_global_set_damp(g_reverb.damp);
    fx_reverb_global_set_predelay(mixer_reverb_predelay_ui_to_seconds(g_reverb.pre_delay));
    mixer_apply_reverb_spectral_window();
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
        g_tracks[t].route_cue = 0U;

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
        mixer_track_filter_init(&g_poly_filters_hot[i], MIXER_FILTER_SAMPLE_RATE_DEFAULT);

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

#if defined(BRICK6_VARIANT_LOWCOST)
    route = ((route & MIXER_ROUTE_MASTER) != 0U) ? MIXER_ROUTE_MASTER : MIXER_ROUTE_NONE;
#endif
    g_tracks[track_id].route_master = ((route & MIXER_ROUTE_MASTER) != 0U) ? 1U : 0U;
    g_tracks[track_id].route_cue = ((route & MIXER_ROUTE_CUE) != 0U) ? 1U : 0U;
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

void mixer_set_reverb_size(float size)
{
    g_reverb.size = clamp01(size);
    fx_reverb_global_set_size(g_reverb.size);
}

void mixer_set_reverb_decay(float decay)
{
    g_reverb.decay = clamp01(decay);
    fx_reverb_global_set_decay(g_reverb.decay);
}

void mixer_set_reverb_damp(float damp)
{
    g_reverb.damp = clamp01(damp);
    fx_reverb_global_set_damp(g_reverb.damp);
}

void mixer_set_reverb_tank_size(uint8_t max_size)
{
    fx_reverb_global_set_tank_size(max_size);
}

void mixer_set_reverb_pre_delay(float pre_delay)
{
    g_reverb.pre_delay = clamp01(pre_delay);
    fx_reverb_global_set_predelay(mixer_reverb_predelay_ui_to_seconds(g_reverb.pre_delay));
}

void mixer_set_reverb_spectral_position(float position)
{
    g_reverb.spectral_position = clamp01(position);
    mixer_apply_reverb_spectral_window();
}

void mixer_set_reverb_spectral_width(float width)
{
    g_reverb.spectral_width = clamp01(width);
    mixer_apply_reverb_spectral_window();
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
    mixer_track_filter_touch_config(filter);
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
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_resonance(uint32_t track_id, float resonance)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    const float next = clampf_local(resonance, 0.0f, 1.0f);
    if (filter->resonance_target == next) return;
    filter->resonance_target = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_eg_amount(uint32_t track_id, float eg_amount)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const float next = clampf_local(eg_amount, -1.0f, 1.0f);
    if (filter->eg_amount == next) return;
    filter->eg_amount = next;
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_attack(uint32_t track_id, float attack_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(attack_s, filter->sample_rate);
    if (filter->filter_env.attack == next) return;
    env_adsr_set_attack(&filter->filter_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(decay_s, filter->sample_rate);
    if (filter->filter_env.decay == next) return;
    env_adsr_set_decay(&filter->filter_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_peaks(sustain);
    if (filter->filter_env.sustain == next) return;
    env_adsr_set_sustain(&filter->filter_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(release_s, filter->sample_rate);
    if (filter->filter_env.release == next) return;
    env_adsr_set_release(&filter->filter_env, next);
    mixer_track_filter_touch_config(filter);
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
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_eq_mid(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if (filter->eq_mid_target_db == gain_db) return;
    filter->eq_mid_target_db = gain_db;
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_filter_eq_high(uint32_t track_id, float gain_db)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *filter = &g_track_filters[track_id];
    if (filter->eq_high_target_db == gain_db) return;
    filter->eq_high_target_db = gain_db;
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
    if (filter->vca_env.attack == next) return;
    env_adsr_set_attack(&filter->vca_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_vca_decay(uint32_t track_id, float decay_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(decay_s, filter->sample_rate);
    if (filter->vca_env.decay == next) return;
    env_adsr_set_decay(&filter->vca_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_vca_sustain(uint32_t track_id, float sustain)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_sustain_to_peaks(sustain);
    if (filter->vca_env.sustain == next) return;
    env_adsr_set_sustain(&filter->vca_env, next);
    mixer_track_filter_touch_config(filter);
}

void mixer_set_track_vca_release(uint32_t track_id, float release_s)
{
    if(track_id >= MIXER_MAX_TRACKS)
        return;

    mixer_track_filter_t *const filter = &g_track_filters[track_id];
    const uint16_t next = mixer_track_filter_time_s_to_peaks(release_s, filter->sample_rate);
    if (filter->vca_env.release == next) return;
    env_adsr_set_release(&filter->vca_env, next);
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
    memset(g_external_track_enabled, 0, sizeof(g_external_track_enabled));
    memset(g_external_track_format, 0, sizeof(g_external_track_format));
    memset(g_external_track_frames_valid, 0, sizeof(g_external_track_frames_valid));
}

void __attribute__((used)) mixer_submit_external_mono(uint32_t track_id, const float *mono, uint32_t frames)
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
        g_external_track_l[track_id][i] = mono[i];
        g_external_track_r[track_id][i] = mono[i];
    }

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
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
}

void __attribute__((used)) mixer_submit_external_multi_stereo(uint32_t track_id,
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

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
}

void __attribute__((used)) mixer_submit_external_multi_mono(uint32_t track_id,
                                                            const float *mono,
                                                            uint32_t frames)
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

    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_MULTI_MONO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
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
}

uint8_t mixer_begin_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
        return 0U;
    memset(g_external_track_l[track_id], 0, frames * sizeof(float));
    memset(g_external_track_r[track_id], 0, frames * sizeof(float));
    return 1U;
}

uint8_t mixer_process_external_poly_voice(uint32_t mix_track_id,
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

    mixer_poly_filter_sync_config(filter, &g_track_filters[mix_track_id]);
    (void)mixer_track_filter_process_block_mono(filter, mono, frames);
    const float pan_for_mix = -clamp_pan(voice_pan);
    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
    for (uint32_t i = 0U; i < frames; ++i)
    {
        const float vca =
            (float)env_adsr_process_step(&filter->vca_env) * (1.0f / 32767.0f);
        filter->vca_env_value = vca;
        g_external_track_l[mix_track_id][i] += mono[i] * vca * pan_l;
        g_external_track_r[mix_track_id][i] += mono[i] * vca * pan_r;
    }
    return (env_adsr_stage(&filter->vca_env) != ENV_ADSR_PEAKS_STAGE_IDLE);
}

void mixer_commit_external_poly(uint32_t track_id, uint32_t frames)
{
    if ((track_id >= 8U) || (frames == 0U) || (frames > AUDIO_BLOCK_SIZE)
            || (g_external_track_enabled[track_id] != 0U))
        return;
    g_external_track_format[track_id] = MIXER_EXTERNAL_FORMAT_POLY_STEREO;
    g_external_track_frames_valid[track_id] = (uint16_t)frames;
    g_external_track_enabled[track_id] = 1U;
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
    env_adsr_retrigger(&filter->vca_env, filter->vca_retrigger_hard != 0U);
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
    env_adsr_gate_off(&filter->vca_env);
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
            env_adsr_gate_off(&filter->vca_env);
        }
    }
}

void mixer_synth_voice_slot_reset(uint8_t slot)
{
    if (slot < SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
    {
        mixer_track_filter_init(&g_poly_filters_hot[slot], MIXER_FILTER_SAMPLE_RATE_DEFAULT);
    }
}

/**
 * @brief Traite un bloc de mixage final MAIN/CUE.
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
    AUDIO_HOT ALIGN32 static float ext_mono_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float ext_mono_r[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_main_r[AUDIO_BLOCK_SIZE];
#if MIXER_HAS_CUE_BUS
    AUDIO_HOT ALIGN32 static float bus_cue_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float bus_cue_r[AUDIO_BLOCK_SIZE];
#endif
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
#if MIXER_HAS_CUE_BUS
    AUDIO_HOT ALIGN32 static float looper_bus_cue_l[AUDIO_BLOCK_SIZE];
    AUDIO_HOT ALIGN32 static float looper_bus_cue_r[AUDIO_BLOCK_SIZE];
#endif
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
#if MIXER_HAS_CUE_BUS
    memset(bus_cue_l, 0, sizeof(bus_cue_l));
    memset(bus_cue_r, 0, sizeof(bus_cue_r));
#endif
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
#if MIXER_HAS_CUE_BUS
    uint8_t looper_playback_routes_cue = 0U;
#endif
    for(uint8_t logical_track = 0U; logical_track < MIXER_MAX_TRACKS; ++logical_track)
    {
        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(logical_track);
        if((ctx != 0)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS)
                && (mixer_track_is_looper(logical_track) != 0U)
                && (g_tracks[ctx->mix_track_id].mute == 0U)
                && (brick6_looper_runtime_is_playing(logical_track) != 0U))
        {
            looper_output_active[logical_track] = 1U;
            looper_playback_active = 1U;
            if(g_tracks[ctx->mix_track_id].route_master != 0U)
            {
                looper_playback_routes_main = 1U;
            }
#if MIXER_HAS_CUE_BUS
            if(g_tracks[ctx->mix_track_id].route_cue != 0U)
            {
                looper_playback_routes_cue = 1U;
            }
#endif
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
#if MIXER_HAS_CUE_BUS
    if((looper_playback_mix_active != 0U) && (looper_playback_routes_cue != 0U))
    {
        memset(looper_bus_cue_l, 0, sizeof(looper_bus_cue_l));
        memset(looper_bus_cue_r, 0, sizeof(looper_bus_cue_r));
    }
#endif
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

    for(uint32_t t = 0; t < MIXER_MAX_TRACKS; t++)
    {
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
                                                           ext_mono_l,
                                                           ext_mono_r,
                                                           diag_lane);
            }
            mono = buffers.mono;
        }
        else
        {
            const mixer_lane_buffers_t buffers = mixer_lane_prepare_stereo_buffers(t,
                                                                                   &lane_plan,
                                                                                   tracks,
                                                                                   frames,
                                                                                   ext_mono_l,
                                                                                   ext_mono_r);
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

        {
            float gain_cur = mt->gain_current;
            float pan_cur = mt->pan_current;
            float mute_gain_cur = mt->mute_gain_current;
            const float inv_frames = (frames > 0U) ? (1.0f / (float)frames) : 0.0f;
            const float gain_step = (mt->gain - gain_cur) * inv_frames;
            const float pan_step = (mt->pan - pan_cur) * inv_frames;
            const float mute_target = (mt->mute != 0U) ? 0.0f : 1.0f;
            const float mute_step = 1.0f / 240.0f;

            if (diag_lane != 0U)
            {
                for(uint32_t i = 0; i < frames; i++)
                {
                    const float pan_for_mix = -pan_cur;
                    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    const float gain_l = gain_cur * pan_l * mute_gain_cur;
                    const float gain_r = gain_cur * pan_r * mute_gain_cur;
                    const float vca_gain = ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                            && (g_track_filters[t].vca_enabled != 0U))
                            ? ((float)env_adsr_process_step(&g_track_filters[t].vca_env) * (1.0f / 32767.0f))
                            : 1.0f;
                    if ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                            && (g_track_filters[t].vca_enabled != 0U))
                    {
                        g_track_filters[t].vca_env_value = vca_gain;
                    }
                    if (is_mono_native_lane != 0U)
                    {
                        audio_track_diag_measure_sample(AUDIO_TRACK_DIAG_DSP,
                                                       mono[i] * vca_gain,
                                                       mono[i] * vca_gain);
                        mono[i] *= (gain_cur * mute_gain_cur * vca_gain);
                        ext_mono_l[i] = mono[i] * pan_l;
                        ext_mono_r[i] = mono[i] * pan_r;
                    }
                    else
                    {
                        audio_track_diag_measure_sample(AUDIO_TRACK_DIAG_DSP,
                                                       L[i] * vca_gain,
                                                       R[i] * vca_gain);
                        L[i] *= (gain_l * vca_gain);
                        R[i] *= (gain_r * vca_gain);
                    }

                    gain_cur += gain_step;
                    pan_cur += pan_step;
                    if (mute_gain_cur < mute_target)
                    {
                        mute_gain_cur += mute_step;
                        if (mute_gain_cur > mute_target) mute_gain_cur = mute_target;
                    }
                    else if (mute_gain_cur > mute_target)
                    {
                        mute_gain_cur -= mute_step;
                        if (mute_gain_cur < mute_target) mute_gain_cur = mute_target;
                    }
                }
            }
            else
            {
                for(uint32_t i = 0; i < frames; i++)
                {
                    /* Standard user convention: pan<0 => left, pan>0 => right.
                     * Runtime output stage wiring is mirrored, so mixer pan is compensated here. */
                    const float pan_for_mix = -pan_cur;
                    const float pan_l = (pan_for_mix <= 0.0f) ? 1.0f : (1.0f - pan_for_mix);
                    const float pan_r = (pan_for_mix >= 0.0f) ? 1.0f : (1.0f + pan_for_mix);
                    const float gain_l = gain_cur * pan_l * mute_gain_cur;
                    const float gain_r = gain_cur * pan_r * mute_gain_cur;
                    const float vca_gain = ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                            && (g_track_filters[t].vca_enabled != 0U))
                            ? ((float)env_adsr_process_step(&g_track_filters[t].vca_env) * (1.0f / 32767.0f))
                            : 1.0f;
                    if ((lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_POLY_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_STEREO)
                            && (lane_plan.ext_format != MIXER_EXTERNAL_FORMAT_MULTI_MONO)
                            && (g_track_filters[t].vca_enabled != 0U))
                    {
                        g_track_filters[t].vca_env_value = vca_gain;
                    }
                    if (is_mono_native_lane != 0U)
                    {
                        mono[i] *= (gain_cur * mute_gain_cur * vca_gain);
                        ext_mono_l[i] = mono[i] * pan_l;
                        ext_mono_r[i] = mono[i] * pan_r;
                    }
                    else
                    {
                        L[i] *= (gain_l * vca_gain);
                        R[i] *= (gain_r * vca_gain);
                    }
                    gain_cur += gain_step;
                    pan_cur += pan_step;
                    if (mute_gain_cur < mute_target)
                    {
                        mute_gain_cur += mute_step;
                        if (mute_gain_cur > mute_target) mute_gain_cur = mute_target;
                    }
                    else if (mute_gain_cur > mute_target)
                    {
                        mute_gain_cur -= mute_step;
                        if (mute_gain_cur < mute_target) mute_gain_cur = mute_target;
                    }
                }
            }

            mt->gain_current = mt->gain;
            mt->pan_current = mt->pan;
            mt->mute_gain_current = mute_gain_cur;
        }

        if (is_mono_native_lane != 0U)
        {
            L = ext_mono_l;
            R = ext_mono_r;
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
#if MIXER_HAS_CUE_BUS
                    if(mt->route_cue != 0U)
                    {
                        for(uint32_t i = 0U; i < frames; ++i)
                        {
                            looper_bus_cue_l[i] += L[i] * MIXER_TRACK_NOMINAL_TRIM;
                            looper_bus_cue_r[i] += R[i] * MIXER_TRACK_NOMINAL_TRIM;
                        }
                    }
#endif
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

        if(mt->route_master && mt->route_cue)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                bus_main_l[i] += l_nom;
                bus_main_r[i] += r_nom;
#if MIXER_HAS_CUE_BUS
                bus_cue_l[i] += l_nom;
                bus_cue_r[i] += r_nom;
#endif
            }
        }
        else if(mt->route_master)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                bus_main_l[i] += l_nom;
                bus_main_r[i] += r_nom;
            }
        }
#if MIXER_HAS_CUE_BUS
        else if(mt->route_cue)
        {
            for(uint32_t i = 0; i < frames; i++)
            {
                const float l_nom = L[i] * MIXER_TRACK_NOMINAL_TRIM;
                const float r_nom = R[i] * MIXER_TRACK_NOMINAL_TRIM;
                bus_cue_l[i] += l_nom;
                bus_cue_r[i] += r_nom;
            }
        }
#endif
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
        const uint8_t cue_xfade_active =
#if MIXER_HAS_CUE_BUS
            ((looper_playback_mix_active != 0U) && (looper_playback_routes_cue != 0U)) ? 1U : 0U;
#else
            0U;
#endif

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
            if(cue_xfade_active != 0U)
            {
#if MIXER_HAS_CUE_BUS
                memcpy(bus_cue_l, looper_bus_cue_l, sizeof(float) * frames);
                memcpy(bus_cue_r, looper_bus_cue_r, sizeof(float) * frames);
#endif
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

            if(cue_xfade_active != 0U)
            {
#if MIXER_HAS_CUE_BUS
                for(uint32_t i = 0U; i < frames; ++i)
                {
                    bus_cue_l[i] = (bus_cue_l[i] * live_gain) + (looper_bus_cue_l[i] * loop_gain);
                    bus_cue_r[i] = (bus_cue_r[i] * live_gain) + (looper_bus_cue_r[i] * loop_gain);
                }
#endif
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
                if(cue_xfade_active != 0U)
                {
#if MIXER_HAS_CUE_BUS
                    bus_cue_l[i] = (bus_cue_l[i] * live_gain) + (looper_bus_cue_l[i] * xfade);
                    bus_cue_r[i] = (bus_cue_r[i] * live_gain) + (looper_bus_cue_r[i] * xfade);
#endif
                }
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

#if MIXER_HAS_CUE_BUS
    if(track_count > 1U)
    {
        memcpy(tracks[1].L, bus_cue_l, sizeof(float) * frames);
        memcpy(tracks[1].R, bus_cue_r, sizeof(float) * frames);
    }
#endif
}
