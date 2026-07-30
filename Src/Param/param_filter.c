#include "Param/param_filter.h"
#include "param_store.h"
#include "ui_core.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_runtime.h"
#include "Core/track_sound_state.h"
#include "Mod/mod_lfo_v1.h"
#include "mixer.h"
#include <math.h>
#include <string.h>

static float clamp_value(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float filter_ui127_clamp(float v)
{
    return clamp_value(v, 0.0f, 127.0f);
}

static float filter_ui127_to_unit(float v)
{
    return filter_ui127_clamp(v) * (1.0f / 127.0f);
}

enum
{
    FILTER_EXP2_LUT_SIZE = 256U
};

#define FILTER_LOG2_CUTOFF_RATIO 9.6438561897747247f
#define FILTER_LOG2_TIME_RATIO   12.287712379549449f

static float g_filter_exp2_lut[FILTER_EXP2_LUT_SIZE + 1U];
static uint8_t g_filter_exp2_lut_ready = 0U;

static void filter_init_exp2_lut(void)
{
    if (g_filter_exp2_lut_ready != 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i <= FILTER_EXP2_LUT_SIZE; ++i)
    {
        g_filter_exp2_lut[i] = powf(2.0f, (float)i * (1.0f / (float)FILTER_EXP2_LUT_SIZE));
    }

    g_filter_exp2_lut_ready = 1U;
}

static float filter_exp2_lut(float x)
{
    if (x <= 0.0f)
    {
        return 1.0f;
    }

    uint32_t integer = (uint32_t)x;
    if (integer > 15U)
    {
        integer = 15U;
    }

    const float frac = x - (float)integer;
    const float pos = frac * (float)FILTER_EXP2_LUT_SIZE;
    uint32_t index = (uint32_t)pos;
    if (index >= FILTER_EXP2_LUT_SIZE)
    {
        index = FILTER_EXP2_LUT_SIZE - 1U;
    }

    const float lerp = pos - (float)index;
    const float a = g_filter_exp2_lut[index];
    const float b = g_filter_exp2_lut[index + 1U];
    const float mantissa = a + ((b - a) * lerp);
    return mantissa * (float)(1UL << integer);
}

static float filter_ui127_to_cutoff_hz(float v)
{
    const float t = filter_ui127_to_unit(v);
    const float min_hz = 20.0f;

    return min_hz * filter_exp2_lut(FILTER_LOG2_CUTOFF_RATIO * t);
}

static float filter_ui127_to_resonance(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_eg_amount(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_time_s(float v, float min_s, float max_s)
{
    const float t = filter_ui127_to_unit(v);

    (void)max_s;
    return min_s * filter_exp2_lut(FILTER_LOG2_TIME_RATIO * t);
}

static float filter_ui127_to_attack_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_decay_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_sustain(float v)
{
    return filter_ui127_to_unit(v);
}

static float filter_ui127_to_release_s(float v)
{
    return filter_ui127_to_time_s(v, 0.001f, 5.0f);
}

static float filter_ui127_to_keytrack(float v)
{
    return filter_ui127_to_unit(v);
}

static uint8_t filter_ui127_to_bool(float v)
{
    return (filter_ui127_clamp(v) >= 63.5f) ? 1U : 0U;
}

static float filter_eq_ui127_to_db(float v)
{
    const float clamped = filter_ui127_clamp(v);

    if(clamped <= 64.0f)
    {
        return -80.0f + ((clamped / 64.0f) * 80.0f);
    }

    return ((clamped - 64.0f) / 63.0f) * 12.0f;
}

typedef track_sound_state_t filter_ui_state_t;


void param_filter_init(void)
{
    filter_init_exp2_lut();
    track_sound_state_init();
    track_tone_sound_state_init();
}

uint8_t param_filter_is_param(param_id_t id)
{
    switch (id)
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
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t filter_mod_locked_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    const ui_track_family_t family = ui_get_track_family(active_track);
    const ui_track_type_t type = ui_get_track_type(active_track);

    return (ui_track_family_is_input(family) && (type == UI_TRACK_TYPE_AUDIO)) ? 1U : 0U;

}

static uint8_t resolve_filter_target_track(uint32_t *out_track_id)
{
    track_runtime_resolved_track_t resolved;
    if ((out_track_id == NULL)
            || (track_runtime_resolve_track(ui_get_active_track(), &resolved) == 0U)
            || (resolved.has_filter_target == 0U))
    {
        return 0U;
    }

    *out_track_id = (uint32_t)resolved.filter_track_id;
    return 1U;
}

static uint8_t resolve_filter_target_track_for_ui_track(uint8_t ui_track, uint32_t *out_track_id)
{
    track_runtime_resolved_track_t resolved;
    if ((out_track_id == NULL)
            || (track_runtime_resolve_track(ui_track, &resolved) == 0U)
            || (resolved.has_filter_target == 0U))
    {
        return 0U;
    }

    *out_track_id = (uint32_t)resolved.filter_track_id;
    return 1U;
}

static filter_ui_state_t *resolve_filter_ui_state_for_track(uint8_t track)
{
    return track_sound_state_get(track);
}

typedef struct
{
    uint32_t target_track;
    filter_ui_state_t *state;
} param_filter_apply_target_t;

static uint8_t param_filter_resolve_target(uint8_t track,
                                           param_id_t id,
                                           uint8_t require_shadow_state,
                                           param_filter_apply_target_t *out_target)
{
    if (out_target == NULL)
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->state = resolve_filter_ui_state_for_track(track);
    if ((require_shadow_state != 0U) && (out_target->state == NULL))
    {
        return 0U;
    }

    (void)id;
    return resolve_filter_target_track_for_ui_track(track, &out_target->target_track);
}

static uint8_t param_filter_apply_runtime(param_id_t id,
                                          float clamped,
                                          const param_filter_apply_target_t *target)
{
    if (target == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_FILTER_TYPE:
            mixer_set_track_filter_type(target->target_track, (mixer_track_filter_type_t)((uint32_t)(clamp_value(clamped, 0.0f, 4.0f) + 0.5f)));
            return 1U;
        case PARAM_FILTER_CUTOFF:
            mixer_set_track_filter_cutoff(target->target_track, filter_ui127_to_cutoff_hz(clamped));
            return 1U;
        case PARAM_FILTER_RESONANCE:
            mixer_set_track_filter_resonance(target->target_track, filter_ui127_to_resonance(clamped));
            return 1U;
        case PARAM_FILTER_EG_AMT:
            mixer_set_track_filter_eg_amount(target->target_track, filter_ui127_to_eg_amount(clamped));
            return 1U;
        case PARAM_FILTER_ATTACK:
            mixer_set_track_filter_attack(target->target_track, filter_ui127_to_attack_s(clamped));
            return 1U;
        case PARAM_FILTER_DECAY:
            mixer_set_track_filter_decay(target->target_track, filter_ui127_to_decay_s(clamped));
            return 1U;
        case PARAM_FILTER_SUSTAIN:
            mixer_set_track_filter_sustain(target->target_track, filter_ui127_to_sustain(clamped));
            return 1U;
        case PARAM_FILTER_RELEASE:
            mixer_set_track_filter_release(target->target_track, filter_ui127_to_release_s(clamped));
            return 1U;
        case PARAM_FILTER_KEYTRK:
            mixer_set_track_filter_keytrack(target->target_track, filter_ui127_to_keytrack(clamped));
            return 1U;
        case PARAM_FILTER_ENVRST:
        case PARAM_FILTER_ENVDLY:
            return 1U;
        case PARAM_FILTER_EQ_LOW:
            mixer_set_track_filter_eq_low(target->target_track, filter_eq_ui127_to_db(clamped));
            return 1U;
        case PARAM_FILTER_EQ_MID:
            mixer_set_track_filter_eq_mid(target->target_track, filter_eq_ui127_to_db(clamped));
            return 1U;
        case PARAM_FILTER_EQ_HIGH:
            mixer_set_track_filter_eq_high(target->target_track, filter_eq_ui127_to_db(clamped));
            return 1U;
        default:
            return 0U;
    }
}

static void param_filter_update_shadow_state(filter_ui_state_t *state, param_id_t id, float clamped)
{
    if (state == NULL)
    {
        return;
    }

    switch (id)
    {
        case PARAM_FILTER_TYPE: state->type = clamp_value(clamped, 0.0f, 4.0f); break;
        case PARAM_FILTER_CUTOFF: state->cutoff = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_RESONANCE: state->resonance = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_EG_AMT: state->eg_amount = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_ATTACK: state->attack = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_DECAY: state->decay = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_SUSTAIN: state->sustain = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_RELEASE: state->release = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_KEYTRK: state->keytrack = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_ENVRST: state->env_reset = filter_ui127_to_bool(clamped) ? 1.0f : 0.0f; break;
        case PARAM_FILTER_ENVDLY: state->env_delay = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_EQ_LOW: state->eq_low = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_EQ_MID: state->eq_mid = filter_ui127_clamp(clamped); break;
        case PARAM_FILTER_EQ_HIGH: state->eq_high = filter_ui127_clamp(clamped); break;
        default: break;
    }
}

uint8_t param_filter_apply_value(param_id_t id,
                                        uint8_t track,
                                        float clamped,
                                        uint8_t update_shadow_state,
                                        uint8_t resync_lfo_base)
{
    param_filter_apply_target_t target;
    if (param_filter_resolve_target(track, id, update_shadow_state, &target) == 0U)
    {
        return 0U;
    }

    if (param_filter_apply_runtime(id, clamped, &target) == 0U)
    {
        return 0U;
    }

    if (update_shadow_state != 0U)
    {
        param_filter_update_shadow_state(target.state, id, clamped);
    }

    if (resync_lfo_base != 0U)
    {
        mod_lfo_v1_resync_base_on_authoritative_write(track, id, clamped);
    }

    return 1U;
}

void param_filter_sync_ui_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state == NULL)
    {
        return;
    }

    param_store_set_active(PARAM_FILTER_TYPE, state->type);
    param_store_set_active(PARAM_FILTER_CUTOFF, state->cutoff);
    param_store_set_active(PARAM_FILTER_RESONANCE, state->resonance);
    param_store_set_active(PARAM_FILTER_EG_AMT, state->eg_amount);
    param_store_set_active(PARAM_FILTER_ATTACK, state->attack);
    param_store_set_active(PARAM_FILTER_DECAY, state->decay);
    param_store_set_active(PARAM_FILTER_SUSTAIN, state->sustain);
    param_store_set_active(PARAM_FILTER_RELEASE, state->release);
    param_store_set_active(PARAM_FILTER_KEYTRK, state->keytrack);
    param_store_set_active(PARAM_FILTER_ENVRST, state->env_reset);
    param_store_set_active(PARAM_FILTER_ENVDLY, state->env_delay);
    param_store_set_active(PARAM_FILTER_EQ_LOW, state->eq_low);
    param_store_set_active(PARAM_FILTER_EQ_MID, state->eq_mid);
    param_store_set_active(PARAM_FILTER_EQ_HIGH, state->eq_high);

    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_KEYTRK, 0.0f);
        param_store_set_active(PARAM_FILTER_ENVRST, 0.0f);
        param_store_set_active(PARAM_FILTER_ENVDLY, 0.0f);
    }
}

/*
 * Variante FILTER audio:
 * - le runtime audio n'expose plus que Off / EQ3 / SVF Peaks multimode.
 * - le système de paramètres conserve un jeu global `PARAM_FILTER_*`.
 * - la cible DSP est résolue dynamiquement depuis le contexte UI actif.
 */
void apply_filter_type(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->type = clamp_value(v, 0.0f, 4.0f);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }

    mixer_set_track_filter_type(target_track, (mixer_track_filter_type_t)((uint32_t)(clamp_value(v, 0.0f, 4.0f) + 0.5f)));
}

void apply_filter_cutoff(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->cutoff = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_cutoff(target_track, filter_ui127_to_cutoff_hz(v));
}

void apply_filter_resonance(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->resonance = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_resonance(target_track, filter_ui127_to_resonance(v));
}

void apply_filter_eg_amount(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eg_amount = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eg_amount(target_track, filter_ui127_to_eg_amount(v));
}

void apply_filter_attack(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->attack = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_attack(target_track, filter_ui127_to_attack_s(v));
}

void apply_filter_decay(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->decay = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_decay(target_track, filter_ui127_to_decay_s(v));
}

void apply_filter_sustain(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->sustain = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_sustain(target_track, filter_ui127_to_sustain(v));
}

void apply_filter_release(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->release = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_release(target_track, filter_ui127_to_release_s(v));
}

void apply_filter_keytrack(float v)
{
    const uint8_t active_track = ui_get_active_track();
    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    if (filter_mod_locked_for_active_track() != 0U)
    {
        mixer_set_track_filter_keytrack(target_track, 0.0f);
        param_store_set_active(PARAM_FILTER_KEYTRK, 0.0f);
        return;
    }

    mixer_set_track_filter_keytrack(target_track, filter_ui127_to_keytrack(v));
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->keytrack = filter_ui127_clamp(v);
    }
}

void apply_filter_env_reset(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVRST, 0.0f);
        return;
    }

    filter_ui_state_t *state = resolve_filter_ui_state_for_track(ui_get_active_track());
    if (state != NULL)
    {
        state->env_reset = filter_ui127_to_bool(v) ? 1.0f : 0.0f;
    }
}

void apply_filter_env_delay(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVDLY, 0.0f);
        return;
    }

    filter_ui_state_t *state = resolve_filter_ui_state_for_track(ui_get_active_track());
    if (state != NULL)
    {
        state->env_delay = filter_ui127_clamp(v);
    }
}

void apply_filter_eq_low(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_low = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_low(target_track, filter_eq_ui127_to_db(v));
}

void apply_filter_eq_mid(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_mid = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_mid(target_track, filter_eq_ui127_to_db(v));
}

void apply_filter_eq_high(float v)
{
    const uint8_t active_track = ui_get_active_track();
    filter_ui_state_t *state = resolve_filter_ui_state_for_track(active_track);
    if (state != NULL)
    {
        state->eq_high = filter_ui127_clamp(v);
    }

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }
    mixer_set_track_filter_eq_high(target_track, filter_eq_ui127_to_db(v));
}

void apply_filter_drive(float v) { (void)v; }
void apply_filter_decimator_bits(float v) { (void)v; }
void apply_filter_decimator_rate(float v) { (void)v; }
void apply_filter_decimator_rate2(float v) { (void)v; }

uint8_t param_filter_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    if ((out_value == NULL) || (track >= SEQ_TRACK_COUNT) || (param_filter_is_param(id) == 0U))
    {
        return 0U;
    }

    filter_ui_state_t *state = resolve_filter_ui_state_for_track(track);
    if (state == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_FILTER_TYPE: *out_value = state->type; return 1U;
        case PARAM_FILTER_CUTOFF: *out_value = state->cutoff; return 1U;
        case PARAM_FILTER_RESONANCE: *out_value = state->resonance; return 1U;
        case PARAM_FILTER_EG_AMT: *out_value = state->eg_amount; return 1U;
        case PARAM_FILTER_ATTACK: *out_value = state->attack; return 1U;
        case PARAM_FILTER_DECAY: *out_value = state->decay; return 1U;
        case PARAM_FILTER_SUSTAIN: *out_value = state->sustain; return 1U;
        case PARAM_FILTER_RELEASE: *out_value = state->release; return 1U;
        case PARAM_FILTER_KEYTRK: *out_value = state->keytrack; return 1U;
        case PARAM_FILTER_ENVRST: *out_value = state->env_reset; return 1U;
        case PARAM_FILTER_ENVDLY: *out_value = state->env_delay; return 1U;
        case PARAM_FILTER_EQ_LOW: *out_value = state->eq_low; return 1U;
        case PARAM_FILTER_EQ_MID: *out_value = state->eq_mid; return 1U;
        case PARAM_FILTER_EQ_HIGH: *out_value = state->eq_high; return 1U;
        default: return 0U;
    }
}

float param_filter_ui127_to_attack_s(float v) { return filter_ui127_to_attack_s(v); }
float param_filter_ui127_to_decay_s(float v) { return filter_ui127_to_decay_s(v); }
float param_filter_ui127_to_sustain(float v) { return filter_ui127_to_sustain(v); }
float param_filter_ui127_to_release_s(float v) { return filter_ui127_to_release_s(v); }
float param_filter_ui127_to_cutoff_hz(float v) { return filter_ui127_to_cutoff_hz(v); }
float param_filter_ui127_to_resonance(float v) { return filter_ui127_to_resonance(v); }
float param_filter_ui127_to_eg_amount(float v) { return filter_ui127_to_eg_amount(v); }
float param_filter_ui127_to_keytrack(float v) { return filter_ui127_to_keytrack(v); }
float param_filter_eq_ui127_to_db(float v) { return filter_eq_ui127_to_db(v); }



