#include "Param/param_filter.h"
#include "param_store.h"
#include "ui_core.h"
#include "Track/track_runtime.h"
#include "Param/param_registry_runtime_state.h"
#include "Audio/audio_note_engine_adapter.h"
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

void param_filter_init(void)
{
    filter_init_exp2_lut();
}

uint8_t param_filter_is_param(param_id_t id)
{
    switch (id)
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
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t filter_mod_locked_for_active_track(void)
{
    return 0U;
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

typedef struct
{
    uint32_t target_track;
    uint8_t control_track;
} param_filter_apply_target_t;

static uint8_t param_filter_resolve_target(uint8_t track,
                                           param_id_t id,
                                           uint8_t require_control_value,
                                           param_filter_apply_target_t *out_target)
{
    if (out_target == NULL)
    {
        return 0U;
    }

    memset(out_target, 0, sizeof(*out_target));
    out_target->control_track = track;
    (void)require_control_value;
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
        case PARAM_FILTER_MORPH:
            mixer_set_track_filter_morph(target->target_track, clamp_value(clamped, 0.0f, 127.0f));
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
        default:
            return 0U;
    }
}

static void param_filter_update_control_value(uint8_t track, param_id_t id, float clamped)
{
    switch (id)
    {
        case PARAM_FILTER_MORPH:
            clamped = clamp_value(clamped, 0.0f, 127.0f); break;
        case PARAM_FILTER_ENVRST:
            clamped = filter_ui127_to_bool(clamped) ? 1.0f : 0.0f; break;
        default:
            clamped = filter_ui127_clamp(clamped); break;
    }
    param_registry_control_value_set(track, id, clamped);
}


uint8_t param_filter_apply_value(param_id_t id,
                                        uint8_t track,
                                        float clamped,
                                        uint8_t update_control_value,
                                        uint8_t resync_lfo_base)
{
    param_filter_apply_target_t target;
    if (param_filter_resolve_target(track, id, update_control_value, &target) == 0U)
    {
        return 0U;
    }

    if (param_filter_apply_runtime(id, clamped, &target) == 0U)
    {
        return 0U;
    }

    if (update_control_value != 0U)
    {
        param_filter_update_control_value(track, id, clamped);
    }

    (void)resync_lfo_base;

    return 1U;
}

uint8_t param_filter_apply_value_audio(param_id_t id,
                                       uint8_t track,
                                       float clamped)
{
    track_audio_runtime_ctx_t ctx_value;
    const track_audio_runtime_ctx_t *const ctx =
        (audio_note_engine_adapter_current_ctx(track, &ctx_value) != 0U)
            ? &ctx_value : NULL;
    uint8_t target_track = 0U;
    if ((audio_note_engine_adapter_ctx_is_audio_routable(ctx) == 0U)
            || (audio_note_engine_adapter_ctx_filter_target(
                    ctx, &target_track) == 0U))
    {
        return 0U;
    }

    param_filter_apply_target_t target = {
        .target_track = target_track,
        .control_track = track
    };
    return param_filter_apply_runtime(id, clamped, &target);
}

void param_filter_sync_ui_for_active_track(void)
{
    const uint8_t active_track = ui_get_active_track();
    const param_id_t ids[] = { PARAM_FILTER_MORPH, PARAM_FILTER_CUTOFF,
        PARAM_FILTER_RESONANCE, PARAM_FILTER_EG_AMT, PARAM_FILTER_ATTACK,
        PARAM_FILTER_DECAY, PARAM_FILTER_SUSTAIN, PARAM_FILTER_RELEASE,
        PARAM_FILTER_KEYTRK, PARAM_FILTER_ENVRST, PARAM_FILTER_ENVDLY };
    for (uint8_t i = 0U; i < (uint8_t)(sizeof(ids) / sizeof(ids[0])); ++i)
    {
        float value = 0.0f;
        if (param_registry_control_value_get(active_track, ids[i], &value))
            param_store_set_active(ids[i], value);
    }

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
 * - le syst�me de param�tres conserve un jeu global `PARAM_FILTER_*`.
 * - la cible DSP est r�solue dynamiquement depuis le contexte UI actif.
 */
void apply_filter_morph(float v)
{
    const uint8_t active_track = ui_get_active_track();
    param_registry_control_value_set(active_track, PARAM_FILTER_MORPH,
                                     clamp_value(v, 0.0f, 127.0f));

    uint32_t target_track = 0U;
    if (!resolve_filter_target_track(&target_track))
    {
        return;
    }

    mixer_set_track_filter_morph(target_track, clamp_value(v, 0.0f, 127.0f));
}

void apply_filter_cutoff(float v)
{
    const uint8_t active_track = ui_get_active_track();
    param_registry_control_value_set(active_track, PARAM_FILTER_CUTOFF,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_RESONANCE,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_EG_AMT,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_ATTACK,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_DECAY,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_SUSTAIN,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_RELEASE,
                                     filter_ui127_clamp(v));

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
    param_registry_control_value_set(active_track, PARAM_FILTER_KEYTRK,
                                     filter_ui127_clamp(v));
}

void apply_filter_env_reset(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVRST, 0.0f);
        return;
    }

    param_registry_control_value_set(ui_get_active_track(), PARAM_FILTER_ENVRST,
        filter_ui127_to_bool(v) ? 1.0f : 0.0f);
}

void apply_filter_env_delay(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_ENVDLY, 0.0f);
        return;
    }

    param_registry_control_value_set(ui_get_active_track(), PARAM_FILTER_ENVDLY,
                                     filter_ui127_clamp(v));
}

void apply_filter_drive(float v) { (void)v; }
void apply_filter_decimator_bits(float v) { (void)v; }
void apply_filter_decimator_rate(float v) { (void)v; }
void apply_filter_decimator_rate2(float v) { (void)v; }


float param_filter_ui127_to_attack_s(float v) { return filter_ui127_to_attack_s(v); }
float param_filter_ui127_to_decay_s(float v) { return filter_ui127_to_decay_s(v); }
float param_filter_ui127_to_sustain(float v) { return filter_ui127_to_sustain(v); }
float param_filter_ui127_to_release_s(float v) { return filter_ui127_to_release_s(v); }
float param_filter_ui127_to_cutoff_hz(float v) { return filter_ui127_to_cutoff_hz(v); }
float param_filter_ui127_to_resonance(float v) { return filter_ui127_to_resonance(v); }
float param_filter_ui127_to_eg_amount(float v) { return filter_ui127_to_eg_amount(v); }
float param_filter_ui127_to_keytrack(float v) { return filter_ui127_to_keytrack(v); }
