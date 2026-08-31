#include "Param/param_filter.h"
#include "param_store.h"
#include "ui_core.h"
#include "Track/track_runtime.h"
#include "Param/param_registry_runtime_state.h"
#include "Mod/mod_lfo_v1.h"
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

static uint8_t filter_ui127_to_bool(float v)
{
    return (filter_ui127_clamp(v) >= 63.5f) ? 1U : 0U;
}

void param_filter_init(void) {}

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

    if (update_control_value != 0U)
    {
        param_filter_update_control_value(track, id, clamped);
    }

    (void)resync_lfo_base;
    return param_registry_publish_track_base_audio(id, track, clamped);
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
    (void)param_registry_apply_track_value(
        PARAM_FILTER_MORPH, ui_get_active_track(), v);
}

void apply_filter_cutoff(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_CUTOFF, ui_get_active_track(), v);
}

void apply_filter_resonance(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_RESONANCE, ui_get_active_track(), v);
}

void apply_filter_eg_amount(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_EG_AMT, ui_get_active_track(), v);
}

void apply_filter_attack(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_ATTACK, ui_get_active_track(), v);
}

void apply_filter_decay(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_DECAY, ui_get_active_track(), v);
}

void apply_filter_sustain(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_SUSTAIN, ui_get_active_track(), v);
}

void apply_filter_release(float v)
{
    (void)param_registry_apply_track_value(
        PARAM_FILTER_RELEASE, ui_get_active_track(), v);
}

void apply_filter_keytrack(float v)
{
    if (filter_mod_locked_for_active_track() != 0U)
    {
        param_store_set_active(PARAM_FILTER_KEYTRK, 0.0f);
        return;
    }
    (void)param_registry_apply_track_value(
        PARAM_FILTER_KEYTRK, ui_get_active_track(), v);
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
