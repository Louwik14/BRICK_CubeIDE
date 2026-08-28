/**
 * @file param_registry.c
 * @brief Module applicatif param_registry.
 *
 * Rôle du module:
 * - Implémenter les traitements liés à param_registry.
 * - Fournir les services internes utilisés par le firmware utilisateur.
 *
 * Architecture:
 * - Appelé par: modules applicatifs selon l'orchestration du firmware.
 * - Appelle: dépendances matérielles et/ou modules utilisateur associés.
 *
 * Contraintes temps réel:
 * - IRQ: selon les API appelées.
 * - Hard realtime: selon le chemin d'exécution.
 * - malloc: éviter en chemin critique.
 *
 * Notes:
 * - Documentation ajoutée sans modification de la logique d'exécution.
 */

#include "param_registry.h"
#include "param_store.h"
#include "Audio/fx_modfx_global.h"
#include "Core/audio_wave_table_projection.h"
#include "Core/control_music_output.h"
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_pipeline.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Param/param_macro.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_param_iface.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/entity_topology.h"
#include "Core/track_mute.h"
#include "Core/track_sound_state.h"
#include "Audio/md_model.h"
#include "Core/track_state.h"
#include "Core/live_parameter_migration.h"
#include "Core/live_parameter_audio_publication.h"
#include "Core/live_clock.h"
#include "Audio/audio_fx_runtime.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix.h"
#include "Mod/mod_destination_catalog.h"
#include "UI/ui_core.h"
#include "UI/ui_track_catalog.h"
#include "Keyboard/keyboard_engine.h"
#include <stddef.h>
#include <string.h>
#include <math.h>

static float clamp_value(float v, float lo, float hi);

static uint8_t param_registry_audio_fx_pre_filter_supported_control(uint8_t track)
{
    track_runtime_descriptor_t descriptor;
    if (track_runtime_get_descriptor(track, &descriptor) == 0U
            || descriptor.active == 0U)
        return 0U;
    entity_topology_descriptor_t topology;
    if ((entity_topology_get((brick_entity_id_t)track, &topology) != 0U)
            && (topology.role == ENTITY_ROLE_GROUP_MASTER))
        return 1U;
    if ((descriptor.family == TRACK_RUNTIME_FAMILY_SAMPLER)
            && (descriptor.type == TRACK_RUNTIME_TYPE_MULTI))
        return 0U;
    if (descriptor.family == TRACK_RUNTIME_FAMILY_SYNTH)
    {
        float voices = 1.0f;
        (void)param_registry_control_value_get(
            track, PARAM_CFG_POLY_VOICES, &voices);
        if (voices > 1.5f)
            return 0U;
    }
    return 1U;
}
static float param_registry_audio_fx_clamp_p3(float value)
{
    return clamp_value(value, 0.0f, 127.0f);
}
static void param_registry_audio_fx_set_control(uint8_t track,
                                                param_id_t id,
                                                float value);

static float param_registry_global_delay_time_seconds(float value)
{
    static const float beats[] = {
        0.125f, 0.1666667f, 0.25f, 0.3333333f, 0.5f, 0.6666667f,
        0.75f, 1.0f, 1.3333334f, 1.5f, 2.0f, 3.0f, 4.0f
    };
    uint32_t bpm_milli = 120000U;
    const uint8_t index = (uint8_t)(clamp_value(value, 0.0f, 12.0f) + 0.5f);

    if ((seq_runtime_get_clock_source() != SEQ_CLOCK_SRC_INTERNAL)
            && (seq_runtime_is_external_tempo_valid() != 0U))
    {
        bpm_milli = seq_runtime_get_external_tempo_bpm_milli();
    }
    else
    {
        bpm_milli = seq_runtime_get_tempo_bpm_milli();
    }
    if (bpm_milli < 40000U)
        bpm_milli = 40000U;
    if (bpm_milli > 300000U)
        bpm_milli = 300000U;
    return beats[index] * 60000.0f / (float)bpm_milli;
}

static float param_registry_global_modfx_unit(float value)
{
    return clamp_value(value, 0.0f, 127.0f) * (1.0f / 127.0f);
}

static float param_registry_global_modfx_command(param_id_t id,
                                                  float value,
                                                  uint8_t model)
{
    const float unit = param_registry_global_modfx_unit(value);
    if (model == FX_MODFX_DAISY_STEREO)
    {
        switch (id)
        {
            case PARAM_MODFX_RATE:
            case PARAM_MODFX_RATE_B:
                return (value <= 61.0f)
                    ? (0.01f * powf(30.0f, value / 61.0f))
                    : (0.3f * powf(40.0f, (value - 61.0f) / 66.0f));
            case PARAM_MODFX_DEPTH:
            case PARAM_MODFX_DEPTH_B:
                return (value <= 123.0f)
                    ? (0.9f * value / 123.0f)
                    : (0.9f + 0.03f * (value - 123.0f) / 4.0f);
            case PARAM_MODFX_OFFSET:
            case PARAM_MODFX_DELAY_B:
                return (value <= 95.0f)
                    ? (0.75f * value / 95.0f)
                    : (0.75f + 0.25f * (value - 95.0f) / 32.0f);
            case PARAM_MODFX_FEEDBACK:
                return (value <= 64.0f)
                    ? ((value - 64.0f) * (1.0f / 64.0f))
                    : ((value - 64.0f) * (1.0f / 63.0f));
            case PARAM_MODFX_WIDTH:
                return (value <= 64.0f)
                    ? (value * (1.0f / 128.0f))
                    : (0.5f + (value - 64.0f) * (0.5f / 63.0f));
            default:
                break;
        }
    }
    switch (id)
    {
        case PARAM_MODFX_RATE: return 0.01f + 11.99f * unit;
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET: return unit;
        default: return value;
    }
}

uint8_t param_registry_prepare_global_audio_command(param_id_t id,
                                                    float canonical_value,
                                                    float *out_command_value)
{
    if ((out_command_value == NULL) || (id >= PARAM_COUNT))
        return 0U;

    *out_command_value = canonical_value;
    switch (id)
    {
        case PARAM_MIX_DELAY_TIME:
        case PARAM_MIX_DELAY_TIME_R:
            *out_command_value = param_registry_global_delay_time_seconds(canonical_value);
            break;
        case PARAM_MODFX_RATE:
        case PARAM_MODFX_DEPTH:
        case PARAM_MODFX_FEEDBACK:
        case PARAM_MODFX_OFFSET:
        case PARAM_MODFX_RATE_B:
        case PARAM_MODFX_DELAY_B:
        case PARAM_MODFX_DEPTH_B:
        case PARAM_MODFX_WIDTH:
            *out_command_value = param_registry_global_modfx_command(
                id, canonical_value, (uint8_t)(param_get(PARAM_MODFX_MODEL) + 0.5f));
            break;
        default:
            break;
    }
    return 1U;
}

static const param_id_t g_audio_fx_param_order[] = {
    PARAM_AUDIO_FX_MODEL,
    PARAM_AUDIO_FX_P1,
    PARAM_AUDIO_FX_P2,
    PARAM_AUDIO_FX_P3,
    PARAM_AUDIO_FX_B_MODEL,
    PARAM_AUDIO_FX_B_P1,
    PARAM_AUDIO_FX_B_P2,
    PARAM_AUDIO_FX_B_P3,
    PARAM_AUDIO_FX_FILTER_POS,
    PARAM_AUDIO_FX_ORDER,
    PARAM_AUDIO_FX_MODE_A,
    PARAM_AUDIO_FX_MODE_B,
    PARAM_GROUP_FX_A_LEVEL,
    PARAM_GROUP_FX_B_LEVEL
};

uint8_t param_registry_is_audio_fx_param(param_id_t id)
{
    return audio_fx_runtime_is_param(id);
}

param_id_t param_registry_get_audio_fx_param(uint8_t order)
{
    return (order < (uint8_t)(sizeof(g_audio_fx_param_order)
                              / sizeof(g_audio_fx_param_order[0])))
        ? g_audio_fx_param_order[order] : PARAM_COUNT;
}









static float clamp_value(float v, float lo, float hi);



static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast);

static uint8_t param_registry_submit_audio_value(param_id_t id,
                                                 uint8_t track,
                                                 float value,
                                                 uint8_t scope)
{
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 1U,
        .item = {{
            .parameter_id = (uint16_t)id,
            .scope = scope,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),
            .value = live_parameter_event_encode_float(value)
        }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

static uint8_t param_registry_apply_audio_fx_control(param_id_t id,
                                                      uint8_t track,
                                                      float value)
{
    if ((param_registry_is_audio_fx_param(id) == 0U)
            || (track >= BRICK_ENTITY_CAPACITY)
            || (track_runtime_get_effective_param_status(track, id)
                != TRACK_RUNTIME_PARAM_ALLOWED))
    {
        return 0U;
    }

    if ((id == PARAM_AUDIO_FX_FILTER_POS)
            && (param_registry_audio_fx_pre_filter_supported_control(track) == 0U))
        value = (float)AUDIO_FX_FILTER_POS_PRE;
    if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
        value = param_registry_audio_fx_clamp_p3(value);
    param_registry_audio_fx_set_control(track, id, value);
    (void)param_registry_control_value_get(track, id, &value);

    if (param_registry_submit_audio_value(
            id, track, value, LIVE_PARAMETER_EVENT_SCOPE_TRACK) == 0U)
        return 0U;
    if ((id == PARAM_AUDIO_FX_MODEL) || (id == PARAM_AUDIO_FX_B_MODEL))
        mod_destination_catalog_invalidate_track(track);
    return 1U;
}

uint8_t param_registry_project_track_mute(uint8_t track, uint8_t effective_muted)
{
    const float value = (effective_muted != 0U) ? 1.0f : 0.0f;
    param_registry_control_value_set(track, PARAM_MIX_MUTE, value);
    return param_registry_submit_audio_value(
        PARAM_MIX_MUTE, track, value, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
}
static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped);
static float clamp_value(float v, float lo, float hi);


static void param_registry_sync_active_cfg_mirror_for_track(uint8_t track)
{
    if ((track >= SEQ_LANE_CAPACITY) || (track != ui_get_active_lane()))
    {
        return;
    }

    const ui_track_family_t family = track_state_get_family(track);
    param_store_set_active(PARAM_CFG_TRACK, (float)family);
    param_store_set_active(PARAM_CFG_TRACK_TYPE,
                           (float)ui_track_catalog_type_index_for_family(family,
                                                                         track_state_get_type(track),
                                                                         track,
                                                                         track_state_get_configs()));
}

static uint8_t param_apply_cfg_track_value(param_id_t id, uint8_t track, float clamped)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return 0U;
    }

    if (id == PARAM_CFG_TRACK)
    {
        const ui_track_family_t requested_family =
            (ui_track_family_t)((uint8_t)(clamp_value(clamped,
                                                      0.0f,
                                                      (float)((uint8_t)UI_TRACK_FAMILY_COUNT - 1U)) + 0.5f));
        if (ui_set_track_family(track, requested_family) == false)
        {
            param_registry_sync_active_cfg_mirror_for_track(track);
            return 0U;
        }

        param_registry_sync_active_cfg_mirror_for_track(track);
        return 1U;
    }

    if (id == PARAM_CFG_TRACK_TYPE)
    {
        const ui_track_family_t family = track_state_get_family(track);
        const uint8_t requested_index =
            (uint8_t)(clamp_value(clamped, 0.0f, (float)((uint8_t)UI_TRACK_TYPE_COUNT - 1U)) + 0.5f);
        const ui_track_type_t requested_type =
            ui_track_catalog_type_from_family_index(family,
                                                    requested_index,
                                                    track,
                                                    track_state_get_configs());
        if (ui_set_track_type(track, requested_type) == false)
        {
            param_registry_sync_active_cfg_mirror_for_track(track);
            return 0U;
        }

        param_registry_sync_active_cfg_mirror_for_track(track);
        return 1U;
    }

    return 0U;
}

/**
 * @brief Point d'entrée clamp_value.
 *
 * Rôle:
 * - Exécuter le traitement associé à clamp_value.
 *
 * @param v Paramètre d'entrée de l'API.
 * @param lo Paramètre d'entrée de l'API.
 * @param hi Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static float clamp_value(float v, float lo, float hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

extern const param_desc_t param_registry[PARAM_COUNT];

static uint8_t g_param_registry_batch_depth = 0U;

static const param_id_t g_param_registry_lfo_params[MOD_LFO_COUNT_PER_TRACK][MOD_LFO_PARAM_COUNT] = {
    { PARAM_LFO1_RATE, PARAM_LFO1_SHAPE, PARAM_LFO1_TRIG, PARAM_LFO1_PHASE },
    { PARAM_LFO2_RATE, PARAM_LFO2_SHAPE, PARAM_LFO2_TRIG, PARAM_LFO2_PHASE },
    { PARAM_LFO3_RATE, PARAM_LFO3_SHAPE, PARAM_LFO3_TRIG, PARAM_LFO3_PHASE },
};

void param_registry_batch_begin(void)
{
    if (g_param_registry_batch_depth < 255U)
    {
        g_param_registry_batch_depth++;
    }
}

void param_registry_batch_end(void)
{
    if (g_param_registry_batch_depth > 0U)
    {
        g_param_registry_batch_depth--;
    }
}

static uint8_t param_lfo_map(param_id_t id, uint8_t *out_lfo_index, mod_lfo_param_t *out_lfo_param)
{
    if ((out_lfo_index == NULL) || (out_lfo_param == NULL))
    {
        return 0U;
    }

    for (uint8_t lfo = 0U; lfo < MOD_LFO_COUNT_PER_TRACK; ++lfo)
    {
        for (uint8_t param = 0U; param < (uint8_t)MOD_LFO_PARAM_COUNT; ++param)
        {
            if (g_param_registry_lfo_params[lfo][param] == id)
            {
                *out_lfo_index = lfo;
                *out_lfo_param = (mod_lfo_param_t)param;
                return 1U;
            }
        }
    }

    return 0U;
}

static uint8_t param_env3_map(param_id_t id, mod_env3_param_t *out_env_param)
{
    if (out_env_param == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_ENV3_ATTACK: *out_env_param = MOD_ENV3_PARAM_ATTACK; return 1U;
        case PARAM_ENV3_DECAY: *out_env_param = MOD_ENV3_PARAM_DECAY; return 1U;
        case PARAM_ENV3_SUSTAIN: *out_env_param = MOD_ENV3_PARAM_SUSTAIN; return 1U;
        case PARAM_ENV3_RELEASE: *out_env_param = MOD_ENV3_PARAM_RELEASE; return 1U;
        default: return 0U;
    }
}

static uint8_t param_matrix_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    switch (id)
    {
        case PARAM_MOD_MATRIX_SLOT:
            return mod_matrix_get_selected_slot(track, out_value);
        case PARAM_MOD_MATRIX_SOURCE:
            return mod_matrix_get_selected_slot_source(track, out_value);
        case PARAM_MOD_MATRIX_DEST:
            return mod_matrix_get_selected_slot_destination_index(track, out_value);
        case PARAM_MOD_MATRIX_DEPTH:
            return mod_matrix_get_selected_slot_depth(track, out_value);
        default:
            return 0U;
    }
}

static uint8_t param_matrix_set_track_value(param_id_t id, uint8_t track, float value)
{
    switch (id)
    {
        case PARAM_MOD_MATRIX_SLOT:
            return mod_matrix_set_selected_slot(track, value);
        case PARAM_MOD_MATRIX_SOURCE:
            return mod_matrix_set_selected_slot_source(track, value);
        case PARAM_MOD_MATRIX_DEST:
            return mod_matrix_set_selected_slot_destination_index(track, value);
        case PARAM_MOD_MATRIX_DEPTH:
            return mod_matrix_set_selected_slot_depth(track, value);
        default:
            return 0U;
    }
}


static uint8_t param_mod_operator_set_track_value(param_id_t id, uint8_t track, float value)
{
    switch (id)
    {
        case PARAM_MOD_MULTI_1_A:
            return mod_matrix_set_multi_source(track, 0U, 0U, value);
        case PARAM_MOD_MULTI_1_B:
            return mod_matrix_set_multi_source(track, 0U, 1U, value);
        case PARAM_MOD_MULTI_2_A:
            return mod_matrix_set_multi_source(track, 1U, 0U, value);
        case PARAM_MOD_MULTI_2_B:
            return mod_matrix_set_multi_source(track, 1U, 1U, value);
        case PARAM_MOD_SLEW_1_SOURCE:
            return mod_matrix_set_slew_source(track, 0U, value);
        case PARAM_MOD_SLEW_1_AMOUNT:
            return mod_matrix_set_slew_amount(track, 0U, value);
        case PARAM_MOD_SLEW_2_SOURCE:
            return mod_matrix_set_slew_source(track, 1U, value);
        case PARAM_MOD_SLEW_2_AMOUNT:
            return mod_matrix_set_slew_amount(track, 1U, value);
        default:
            return 0U;
    }
}

static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped)
{
    if (id == PARAM_MIDI_PROGRAM)
    {
        if (seq_param_iface_set_play_base_param(track,
                                                id,
                                                seq_param_iface_encode_param_value(id, clamped)) == 0U)
        {
            return 0U;
        }

        param_registry_control_value_set(track, id, clamped);
        /* Post-commit notification: MIDI program changes are forwarded after the authoritative write. */
        seq_runtime_on_midi_program_live_change(track, clamped);
        return 1U;
    }

    if (param_backend_is_midi_cc_id(id) != 0U)
    {
        if (param_backend_send_midi_cc(track, id, clamped) == 0U)
        {
            return 0U;
        }
    }

    if (seq_param_iface_set_play_base_param(track,
                                            id,
                                            seq_param_iface_encode_param_value(id, clamped)) == 0U)
    {
        return 0U;
    }

    param_registry_control_value_set(track, id, clamped);
    return 1U;
}


static uint8_t param_registry_set_track_tone_value(param_id_t id,
                                                    uint8_t track,
                                                    float value)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id >= PARAM_COUNT))
        return 0U;
    if (id == PARAM_DRUM_MD_MODEL)
    {
        const uint8_t model = md_model_validate(value);
        value = (float)model;
        mod_destination_catalog_invalidate_track(track);
    }
    param_registry_control_value_set(track, id, value);
    return 1U;
}

static uint8_t param_apply_non_filter_track_value_audio(param_id_t id,
                                                        uint8_t track,
                                                        float clamped)
{
    if ((track >= SEQ_LANE_CAPACITY) || (id == PARAM_EXTERNAL_INPUT)
            || (id == PARAM_MIDI_PROGRAM))
    {
        return 0U;
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
            return mod_lfo_v1_set_track_param_audio(
                track, lfo_index, lfo_param, clamped);
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD))
    {
        return 0U;
    }

    track_audio_runtime_ctx_t audio_ctx_value;
    const track_audio_runtime_ctx_t *const audio_ctx =
        (audio_note_engine_adapter_current_ctx(track, &audio_ctx_value) != 0U)
            ? &audio_ctx_value : NULL;
    if (audio_note_engine_adapter_ctx_is_audio_routable(audio_ctx) == 0U)
    {
        return 0U;
    }

    return param_backend_apply_prepared_track_value_audio(
        track, id, clamped, 0U);
}

typedef struct param_track_exec_ctx_t
{
    uint8_t track;
    param_id_t id;
    float clamped;
    uint8_t rt_fast;
    track_runtime_param_rule_t rule;
    track_runtime_resolved_track_t resolved;
} param_track_exec_ctx_t;

static uint8_t param_track_exec_apply_tone_drum_range(const param_track_exec_ctx_t *ctx,
                                                      track_runtime_type_t type,
                                                      param_id_t first_id,
                                                      param_id_t last_id)
{
    if ((ctx == NULL)
        || (ctx->rt_fast != 0U)
        || (ctx->rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        || (ctx->resolved.descriptor.type != type)
        || (ctx->id < first_id)
        || (ctx->id > last_id))
    {
        return 2U;
    }

    if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
    {
        return 0U;
    }

    return param_backend_apply_track_value_control(
        ctx->track, ctx->id, ctx->clamped);
}

static uint8_t param_track_exec_ctx_build(param_track_exec_ctx_t *ctx,
                                          uint8_t track,
                                          param_id_t id,
                                          float clamped,
                                          track_runtime_param_rule_t rule,
                                          uint8_t rt_fast)
{
    if ((ctx == NULL) || (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->track = track;
    ctx->id = id;
    ctx->clamped = clamped;
    ctx->rt_fast = rt_fast;
    ctx->rule = rule;

    if (track_runtime_resolve_track(track, &ctx->resolved) == 0U)
    {
        return 0U;
    }

    if (ctx->resolved.descriptor.active == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t param_track_exec_authorize(const param_track_exec_ctx_t *ctx)
{
    if (ctx == NULL)
    {
        return 0U;
    }

    if (ctx->rt_fast != 0U)
    {
        if ((ctx->rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
                || (ctx->rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                || (ctx->id == PARAM_MIDI_PROGRAM))
        {
            return 0U;
        }

        return 1U;
    }

    if ((ctx->rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            || ((ctx->id != PARAM_MIDI_PROGRAM) && (param_backend_is_midi_cc_id(ctx->id) == 0U)))
    {
        return 1U;
    }

    return param_backend_track_supports_midi_tone_descriptor(&ctx->resolved.descriptor);
}

static uint8_t param_track_exec_apply_backend(const param_track_exec_ctx_t *ctx,
                                              uint8_t update_base_state)
{
    if (ctx == NULL)
    {
        return 0U;
    }
    if ((ctx->rt_fast == 0U) && (ctx->id == PARAM_MIDI_PROGRAM))
    {
        if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
        {
            return 0U;
        }
        param_registry_control_value_set(ctx->track, ctx->id, ctx->clamped);
        seq_runtime_on_midi_program_live_change(ctx->track, ctx->clamped);
        return 1U;
    }

    if ((ctx->rt_fast == 0U) && (param_backend_is_midi_cc_id(ctx->id) != 0U))
    {
        if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
        {
            return 0U;
        }
        if (param_backend_send_midi_cc(ctx->track, ctx->id, ctx->clamped) == 0U)
        {
            return 0U;
        }
        param_registry_control_value_set(ctx->track, ctx->id, ctx->clamped);
        return 1U;
    }

    {
        const uint8_t applied = param_track_exec_apply_tone_drum_range(ctx,
                                                                        TRACK_RUNTIME_TYPE_DRUM_MD,
                                                                        PARAM_DRUM_MD_MODEL,
                                                                        PARAM_DRUM_MD_P8);
        if (applied != 2U)
        {
            return applied;
        }
    }

    const uint8_t applied = (ctx->rt_fast != 0U)
        ? param_backend_apply_prepared_track_value_audio(ctx->track,
                                                        ctx->id,
                                                        ctx->clamped,
                                                        update_base_state)
        : param_backend_apply_track_value_control(ctx->track,
                                                  ctx->id,
                                                  ctx->clamped);
    if ((applied != 0U) && (update_base_state != 0U))
        param_registry_control_value_set(ctx->track, ctx->id, ctx->clamped);
    return applied;
}

static uint8_t param_track_exec_sync_after_apply(const param_track_exec_ctx_t *ctx, uint8_t applied)
{
    if ((ctx == NULL) || (applied == 0U))
    {
        return 0U;
    }

    return applied;
}

static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast)
{
    if (id == PARAM_EXTERNAL_INPUT)
    {
        if (rt_fast != 0U)
        {
            return 0U;
        }
        return ui_set_track_external_input(
            track, (uint8_t)(clamp_value(clamped, 0.0f,
                (float)(ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT - 1U)) + 0.5f)) ? 1U : 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
    {
        if (rt_fast != 0U)
        {
            return 0U;
        }

        param_set(id, clamped);
        return 1U;
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
    {
        if (rt_fast != 0U)
        {
            return 0U;
        }

        return param_apply_play_track_value(id, track, clamped);
    }

    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG)
    {
        return 0U;
    }

    param_track_exec_ctx_t ctx;
    if (param_track_exec_ctx_build(&ctx, track, id, clamped, rule, rt_fast) == 0U)
    {
        return 0U;
    }

    if (param_track_exec_authorize(&ctx) == 0U)
    {
        return 0U;
    }

    return param_track_exec_sync_after_apply(&ctx,
                                             param_track_exec_apply_backend(
                                                 &ctx,
                                                 (ctx.rt_fast == 0U) ? 1U : 0U));
}

static uint8_t param_apply_non_filter_track_value(param_id_t id, uint8_t track, float clamped)
{
    return param_apply_non_filter_track_value_core(id, track, clamped, 0U);
}

static uint8_t param_apply_filter_track_value(param_id_t id, uint8_t track, float clamped)
{
    return param_filter_apply_value(id, track, clamped, 1U, 1U);
}

/* Query surface: pure value read, no mutation, no resync, no transition. */
uint8_t param_registry_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    if ((id >= PARAM_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    if (note_fx_state_param_map(id, &note_fx_slot, &note_fx_param) != 0U)
    {
        return note_fx_state_get_param(track, id, out_value);
    }

    if (track < SEQ_LANE_CAPACITY)
    {
        switch (id)
        {
            case PARAM_CFG_TRACK:
                *out_value = (float)track_state_get_family(track);
                return 1U;

            case PARAM_CFG_TRACK_TYPE:
                *out_value = (float)ui_track_catalog_type_index_for_family(track_state_get_family(track),
                                                                           track_state_get_type(track),
                                                                           track,
                                                                           track_state_get_configs());
                return 1U;

            case PARAM_CFG_MIDI_CH:
                *out_value = (float)track_state_get_midi_channel(track);
                return 1U;

            case PARAM_CFG_MIDI_SRC:
                *out_value = (float)track_state_get_midi_source(track);
                return 1U;

            case PARAM_EXTERNAL_INPUT:
                *out_value = (float)track_state_get_external_input(track);
                return 1U;

            default:
                break;
        }
    }

    if (param_matrix_get_track_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            if (track >= SEQ_LANE_CAPACITY)
            {
                return 0U;
            }

            return param_registry_control_value_get(track, id, out_value);
        }
    }

    *out_value = param_get(id);
    return 1U;
}
/* Command surface: RT fast-path apply reserved for modulation callers. */
uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value)
{
    /* RT fast path: same value semantics as apply_track_value, but restricted to modulation callers. */
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = param_value_policy_canonicalize(prepared.id, track,
                                                          prepared.value);

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
    {
        return 0U;
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value_audio(id, track, clamped);
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

uint8_t param_registry_apply_track_value_audio(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }
    return param_registry_apply_prepared_track_value_audio(&prepared, track);
}

uint8_t param_registry_apply_prepared_track_value_audio(
    const param_registry_prepared_value_t *prepared,
    uint8_t track)
{
    if ((prepared == NULL)
            || (prepared->id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(prepared->id) != 0U))
    {
        return 0U;
    }

    const param_id_t id = prepared->id;
    const float clamped = param_value_policy_canonicalize(id, track,
                                                           prepared->value);

    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        const float audio_value = ((id == PARAM_AUDIO_FX_P3)
                || (id == PARAM_AUDIO_FX_B_P3))
            ? param_registry_audio_fx_clamp_p3(clamped) : clamped;
        track_audio_runtime_ctx_t audio_ctx;
        if ((audio_note_engine_adapter_current_ctx(track, &audio_ctx) == 0U)
                || (audio_note_engine_adapter_ctx_is_audio_routable(&audio_ctx) == 0U))
        {
            return 0U;
        }
        if (audio_fx_runtime_apply_param((brick_entity_id_t)track,
                                         id,
                                         audio_value) == 0U)
        {
            return 0U;
        }

        return 1U;
    }

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
        return 0U;

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t applied = param_filter_apply_value_audio(
            id, track, clamped);
        return applied;
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            const uint8_t applied = mod_env3_audio_apply_track_param(track, env_param, clamped);
            return applied;
        }
    }

    if (id == PARAM_ENV_RETRIG_MOD)
    {
        mod_env3_audio_apply_retrigger(track, clamped);
        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG))
    {
        return 0U;
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

static uint8_t param_registry_publish_track_value_runtime_temp(
    param_id_t id, uint8_t track, float value)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(id) != 0U))
        return 0U;
    const float clamped = clamp_value(
        value, param_registry[id].min, param_registry[id].max);
    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 1U,
        .item = {{
            .parameter_id = (uint16_t)id,
            .scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK,
            .track = track,
            .slot = LIVE_PARAMETER_EVENT_INVALID_INDEX,
            .flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS
                                | LIVE_PARAMETER_EVENT_FLAG_RUNTIME_TEMP),
            .value = live_parameter_event_encode_float(clamped)
        }}
    };
    return live_parameter_audio_publication_submit_bulk(&bulk) ? 1U : 0U;
}

uint8_t param_registry_project_track_base_audio(param_id_t id,
                                                uint8_t track,
                                                float value)
{
    return param_registry_publish_track_value_runtime_temp(id, track, value);
}


static uint8_t param_registry_audio_fx_valid_model(uint8_t model)
{
    return (uint8_t)((model == AUDIO_FX_MODEL_LOFI)
        || (model == AUDIO_FX_MODEL_FOLD) || (model == AUDIO_FX_MODEL_DRIVE)
        || (model == AUDIO_FX_MODEL_POINT) || (model == AUDIO_FX_MODEL_SUB)
        || (model == AUDIO_FX_MODEL_SUB_LIGHT) || (model == AUDIO_FX_MODEL_RING)
        || (model == AUDIO_FX_MODEL_VIBE) || (model == AUDIO_FX_MODEL_DRIFT));
}

static void param_registry_audio_fx_set_control(uint8_t track,
                                                param_id_t id,
                                                float value)
{
    if ((id != PARAM_AUDIO_FX_MODEL) && (id != PARAM_AUDIO_FX_B_MODEL))
    {
        if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
            value = param_registry_audio_fx_clamp_p3(value);
        else if ((id == PARAM_GROUP_FX_A_LEVEL) || (id == PARAM_GROUP_FX_B_LEVEL))
            value = clamp_value(value, 0.0f, 1.0f);
        param_registry_control_value_set(track, id, value);
        return;
    }

    const uint8_t bank_b = (uint8_t)(id == PARAM_AUDIO_FX_B_MODEL);
    const param_id_t other_id = bank_b ? PARAM_AUDIO_FX_MODEL : PARAM_AUDIO_FX_B_MODEL;
    float other = 0.0f;
    (void)param_registry_control_value_get(track, other_id, &other);
    uint8_t model = (uint8_t)(value + 0.5f);
    if (!param_registry_audio_fx_valid_model(model)) model = AUDIO_FX_MODEL_OFF;
    if ((model != AUDIO_FX_MODEL_OFF) && (model == (uint8_t)other))
        model = AUDIO_FX_MODEL_OFF;
    param_registry_control_value_set(track, id, (float)model);
}

uint8_t param_registry_apply_track_value_runtime_temp_audio(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = prepared.value;

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_apply_track_param_temp(track, lfo_index, lfo_param, clamped);
        }
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            return mod_env3_apply_track_param_temp(track, env_param, clamped);
        }
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value_audio(id, track, clamped);
    }

    return param_apply_non_filter_track_value_audio(id, track, clamped);
}

uint8_t param_registry_clear_track_value_runtime_temp_audio(param_id_t id, uint8_t track)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        return mod_lfo_v1_clear_track_param_temp_audio(track, lfo_index, lfo_param);
    }

    mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
    if (param_env3_map(id, &env_param) != 0U)
    {
        return mod_env3_clear_track_param_temp_audio(track, env_param);
    }
    return 0U;
}

uint8_t param_registry_is_lfo_param(param_id_t id)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    return param_lfo_map(id, &lfo_index, &lfo_param);
}

void param_registry_release_track_value_runtime_temp(param_id_t id, uint8_t track)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        (void)param_registry_publish_track_value_runtime_temp(
            id, track, 0.0f);
        return;
    }

}

void param_registry_clear_track_runtime_state(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    /* Only AUDIO runtime overrides are released; CONTROL values are untouched. */
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        param_registry_release_track_value_runtime_temp((param_id_t)raw_id, track);
    }
}

uint8_t param_registry_track_value_is_audio_command(param_id_t id,
                                                    uint8_t track)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    const uint8_t midi_tone = (uint8_t)(
        (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
        && (param_backend_track_supports_midi_tone_ctx(
                track_runtime_get_ctx(track)) != 0U));
    return (uint8_t)(
        (live_parameter_is_audio_owned(id) != 0U)
        || (id == PARAM_CFG_POLY_VOICES)
        || (id == PARAM_CFG_POLY_SPREAD)
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
        || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (midi_tone == 0U))
        || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
}

uint8_t param_registry_install_prepared_track_control_target(
    const param_registry_prepared_value_t *prepared,
    uint8_t track)
{
    if ((prepared == NULL)
            || (prepared->id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(prepared->id) != 0U)
            || (param_registry_track_value_is_audio_command(
                    prepared->id, track) == 0U))
    {
        return 0U;
    }

    const param_id_t id = prepared->id;
    const float value = param_value_policy_canonicalize(prepared->id, track,
                                                         prepared->value);
    if (id == PARAM_MIX_MUTE)
    {
        const float muted = (value >= 0.5f) ? 1.0f : 0.0f;
        param_registry_control_value_set(track, id, muted);
        return 1U;
    }
    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        if ((track >= BRICK_ENTITY_CAPACITY)
                || (track_runtime_get_effective_param_status(track, id)
                    != TRACK_RUNTIME_PARAM_ALLOWED))
        {
            return 0U;
        }
        float effective = value;
        if ((id == PARAM_AUDIO_FX_FILTER_POS)
                && (param_registry_audio_fx_pre_filter_supported_control(track) == 0U))
            effective = (float)AUDIO_FX_FILTER_POS_PRE;
        if ((id == PARAM_AUDIO_FX_P3) || (id == PARAM_AUDIO_FX_B_P3))
            effective = param_registry_audio_fx_clamp_p3(effective);
        param_registry_audio_fx_set_control(track, id, effective);

        return 1U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (live_parameter_is_audio_owned(id) == 0U)
            && (param_registry_set_track_tone_value(id, track, value) == 0U))
    {
        return 0U;
    }
    param_registry_control_value_set(track, id, value);
    if ((id == PARAM_PRISM_OSC1_MODEL)
            || (id == PARAM_PRISM_OSC2_MODEL)
            || (id == PARAM_STACK_OSC1_MODEL)
            || (id == PARAM_STACK_OSC2_MODEL)
            || (id == PARAM_STACK_OSC3_MODEL))
    {
        mod_destination_catalog_invalidate_track(track);
    }
    return 1U;
}

uint8_t param_registry_install_prepared_global_control_target(
    const param_registry_prepared_value_t *prepared)
{
    if ((prepared == NULL) || (prepared->id >= PARAM_COUNT)
            || (param_id_is_reserved(prepared->id) != 0U)
            || ((live_parameter_is_audio_owned(prepared->id) == 0U)
                && (prepared->id != PARAM_MASTER_GAIN)))
        return 0U;
    param_store_set_active(prepared->id, prepared->value);
    return 1U;
}

/* Command surface: track-aware apply and post-commit routing. */
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    param_registry_prepared_value_t prepared;
    value = param_value_policy_canonicalize(id, track, value);
    if (param_registry_prepare_value(id, value, &prepared) == 0U)
    {
        return 0U;
    }

    const float clamped = prepared.value;
    if (id == PARAM_MIX_MUTE)
        return track_mute_set(track, (clamped >= 0.5f) ? 1U : 0U);

    if (param_registry_is_audio_fx_param(id) != 0U)
    {
        return param_registry_apply_audio_fx_control(id, track, clamped);
    }

    uint8_t note_fx_slot = 0U;
    uint8_t note_fx_param = 0U;
    if (note_fx_state_param_map(id, &note_fx_slot, &note_fx_param) != 0U)
    {
        note_fx_track_state_t previous_note_fx_state;
        if (track_runtime_is_ui_ensemble_available(track, TRACK_RUNTIME_UI_ENSEMBLE_MIDI_FX) == 0U)
        {
            return 0U;
        }
        if (note_fx_state_capture_track(track, &previous_note_fx_state) == 0U)
        {
            return 0U;
        }
        if (note_fx_param == 3U)
        {
            float previous = 0.0f;
            uint8_t requested_model = NOTE_FX_MODEL_OFF;
            if ((value > 0.0f) && (value < 255.0f))
            {
                const uint8_t rounded = (uint8_t)(value + 0.5f);
                requested_model = (rounded < NOTE_FX_MODEL_COUNT)
                    ? rounded : NOTE_FX_MODEL_OFF;
            }
            if ((note_fx_state_get_param(track, id, &previous) != 0U)
                    && ((uint8_t)(previous + 0.5f) != requested_model))
            {
                const seq_track_id_t transition_track = track;
                if (seq_play_scheduler_transition_tracks(
                        &transition_track, 1U,
                        SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE) == 0U)
                    return 0U;
            }
        }
        /* NoteFx owns the model-aware schema.  The static catalog is only the
         * legacy ARP/p-lock descriptor and cannot clamp EUCLID LENGTH/PULSE. */
        if (note_fx_state_set_param(track, id, value) == 0U)
        {
            if (note_fx_param == 3U)
            {
                const seq_track_id_t transition_track = track;
                (void)seq_play_scheduler_transition_tracks(
                    &transition_track, 1U,
                    SEQ_PLAY_TRANSITION_RESUME_TRIGS);
            }
            return 0U;
        }
        if (note_fx_pipeline_sync_track(track) == 0U)
        {
            (void)note_fx_state_restore_track_exact(track, &previous_note_fx_state);
            if (note_fx_param == 3U)
            {
                const seq_track_id_t transition_track = track;
                (void)seq_play_scheduler_transition_tracks(
                    &transition_track, 1U, SEQ_PLAY_TRANSITION_RESUME_TRIGS);
            }
            return 0U;
        }
        if (note_fx_param == 3U)
        {
            const seq_track_id_t transition_track = track;
            (void)seq_play_scheduler_transition_tracks(
                &transition_track, 1U, SEQ_PLAY_TRANSITION_RESUME_TRIGS);
        }
        return 1U;
    }

    {
        const uint8_t audio_command =
            param_registry_track_value_is_audio_command(id, track);
        if (audio_command != 0U)
        {
            if ((id == PARAM_CFG_POLY_VOICES)
                    && ((uint8_t)clamped
                        < control_music_output_count(track)))
                return 0U;
            if (param_registry_install_prepared_track_control_target(
                    &prepared, track) == 0U)
                return 0U;
            const uint8_t submitted = param_registry_submit_audio_value(
                id, track, clamped, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
            return submitted;
        }
    }

    if ((id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE))
    {
        const seq_track_id_t transition_track = track;
        if (seq_play_scheduler_transition_tracks(
                &transition_track, 1U,
                SEQ_PLAY_TRANSITION_STOP_CLOSE) == 0U)
            return 0U;
        const uint8_t ok = param_apply_cfg_track_value(id, track, clamped);
        return ok;
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            const uint8_t ok = mod_lfo_v1_set_track_param(track, lfo_index, lfo_param, clamped);
            return (uint8_t)((ok != 0U)
                && param_registry_submit_audio_value(
                    id, track, clamped, LIVE_PARAMETER_EVENT_SCOPE_TRACK));
        }
    }

    if ((id == PARAM_MOD_MATRIX_SLOT)
            || (id == PARAM_MOD_MATRIX_SOURCE)
            || (id == PARAM_MOD_MATRIX_DEST)
            || (id == PARAM_MOD_MATRIX_DEPTH))
    {
        const uint8_t ok = param_matrix_set_track_value(id, track, clamped);
        return ok;
    }

    if ((id == PARAM_MOD_MULTI_1_A)
            || (id == PARAM_MOD_MULTI_1_B)
            || (id == PARAM_MOD_MULTI_2_A)
            || (id == PARAM_MOD_MULTI_2_B)
            || (id == PARAM_MOD_SLEW_1_SOURCE)
            || (id == PARAM_MOD_SLEW_1_AMOUNT)
            || (id == PARAM_MOD_SLEW_2_SOURCE)
            || (id == PARAM_MOD_SLEW_2_AMOUNT))
    {
        const uint8_t ok = param_mod_operator_set_track_value(id, track, clamped);
        return ok;
    }

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t ok = param_apply_filter_track_value(id, track, clamped);
        return ok;
    }

    const uint8_t ok = param_apply_non_filter_track_value(id, track, clamped);
    return ok;
}

/* Command surface: UI edit command forwarded to the track-aware apply seam. */
uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd)
{
    if ((cmd == NULL) || (cmd->id >= PARAM_COUNT) || (cmd->track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    return param_registry_apply_track_value(cmd->id, cmd->track, cmd->value);
}

/* Post-commit notification: refresh the filter UI mirror for the active track. */
void param_registry_sync_filter_ui_for_active_track(void)
{
    /* Post-commit notification: keep UI mirror aligned with the active track filter context. */
    param_filter_sync_ui_for_active_track();
}


/**
 * @brief Point d'entrée param_registry_init.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_registry_init.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void param_registry_init(void)
{
    /* Registry is static metadata; runtime values are in param_store. */
    param_registry_control_values_init();
    track_sound_state_init();
    param_macro_init();
    param_filter_init();
    mod_lfo_v1_init();
    note_fx_state_init();
}

/**
 * @brief Point d'entrée param_get.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_get.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Query surface: global canonical value read. */
float param_get(param_id_t id)
{
    return param_store_get_active(id);
}

/**
 * @brief Point d'entrée param_set.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_set.
 *
 * @param id Paramètre d'entrée de l'API.
 * @param value Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
static uint8_t modfx_bank_ids(uint8_t model,param_id_t packed[4])
{
    switch(model){case FX_MODFX_DAISY_STEREO:packed[0]=PARAM_MODFX_BANK_DAISY_STEREO_AB;packed[1]=PARAM_MODFX_BANK_DAISY_STEREO_CD;packed[2]=PARAM_MODFX_BANK_DAISY_STEREO_EF;packed[3]=PARAM_MODFX_BANK_DAISY_STEREO_G;return 4U;case FX_MODFX_JUNOLOGUE:packed[0]=PARAM_MODFX_BANK_JUNOLOGUE_AB;packed[1]=PARAM_MODFX_BANK_JUNOLOGUE_CD;return 2U;default:return 0U;}
}

static uint8_t modfx_control_ids(uint8_t model,const param_id_t **out)
{
    static const param_id_t standard[4]={PARAM_MODFX_RATE,PARAM_MODFX_DEPTH,PARAM_MODFX_FEEDBACK,PARAM_MODFX_OFFSET};
    static const param_id_t daisy_stereo[8]={PARAM_MODFX_RATE,PARAM_MODFX_RATE_B,PARAM_MODFX_OFFSET,PARAM_MODFX_DELAY_B,PARAM_MODFX_DEPTH,PARAM_MODFX_DEPTH_B,PARAM_MODFX_FEEDBACK,PARAM_MODFX_WIDTH};
    if(model==FX_MODFX_DAISY_STEREO){*out=daisy_stereo;return 8U;}*out=standard;return 4U;
}

uint8_t param_registry_prepare_legacy_modfx_bank_values(
    uint8_t model,const float packed_values[4],
    param_registry_prepared_value_t out_values[8],uint8_t *out_count)
{
    if ((packed_values == NULL) || (out_values == NULL) || (out_count == NULL))
        return 0U;
    param_id_t banks[4];
    if (modfx_bank_ids(model,banks) == 0U) return 0U;
    const param_id_t *ids;
    const uint8_t count=modfx_control_ids(model,&ids);
    uint16_t packed[4];
    for (uint8_t i=0U;i<4U;++i) packed[i]=(uint16_t)(packed_values[i]+0.5f);
    for (uint8_t slot=0U;slot<count;++slot)
    {
        const float value=(float)((packed[slot>>1U]>>((slot&1U)*7U))&127U);
        if (param_registry_prepare_value(ids[slot],value,&out_values[slot]) == 0U)
            return 0U;
    }
    *out_count=count;
    return 1U;
}

uint8_t param_registry_install_legacy_modfx_control_targets(void)
{
    const uint8_t model=(uint8_t)(param_get(PARAM_MODFX_MODEL)+0.5f);
    param_id_t banks[4];
    const uint8_t bank_count=modfx_bank_ids(model,banks);
    if (bank_count == 0U) return 0U;
    float packed[4]={0.0f,0.0f,0.0f,0.0f};
    for (uint8_t i=0U;i<bank_count;++i) packed[i]=param_get(banks[i]);
    param_registry_prepared_value_t values[8];uint8_t count=0U;
    if (param_registry_prepare_legacy_modfx_bank_values(
            model,packed,values,&count) == 0U) return 0U;
    for (uint8_t i=0U;i<count;++i)
        if (param_registry_install_prepared_global_control_target(&values[i]) == 0U)
            return 0U;
    return 1U;
}

static uint8_t modfx_bank_project_model(uint8_t model)
{
    const param_id_t *ids;const uint8_t control_count=modfx_control_ids(model,&ids);
    param_id_t banks[4];uint16_t packed[4]={0U,0U,0U,0U};const uint8_t bank_count=modfx_bank_ids(model,banks);for(uint8_t i=0U;i<bank_count;++i)packed[i]=(uint16_t)(param_get(banks[i])+0.5f);
    live_parameter_audio_bulk_t bulk={.capture_tick=live_clock_capture_tick(),.source=LIVE_PARAMETER_EVENT_SOURCE_BULK,.count=control_count};
    for(uint8_t slot=0U;slot<control_count;++slot)
    {
        const float value=(float)((packed[slot>>1U]>>((slot&1U)*7U))&127U);
        float command_value = value;
        if (param_registry_prepare_global_audio_command(
                ids[slot], value, &command_value) == 0U)
            return 0U;
        bulk.item[slot]=(live_parameter_audio_bulk_item_t){.parameter_id=(uint16_t)ids[slot],.scope=LIVE_PARAMETER_EVENT_SCOPE_GLOBAL,.track=0U,.slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,.flags=(uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET|LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS),.value=live_parameter_event_encode_float(command_value)};
    }
    if (!live_parameter_audio_publication_submit_bulk(&bulk))
        return 0U;
    for(uint8_t slot=0U;slot<control_count;++slot)
    {
        const float value=(float)((packed[slot>>1U]>>((slot&1U)*7U))&127U);
        param_store_set_active(ids[slot],value);
    }
    return 1U;
}

uint8_t param_registry_migrate_legacy_modfx_banks(void)
{
    const uint8_t model=(uint8_t)(param_get(PARAM_MODFX_MODEL)+0.5f);
    param_id_t banks[4];
    if(modfx_bank_ids(model,banks)==0U)return 0U;
    return modfx_bank_project_model(model);
}

/* Command surface: global canonical write. */
void param_set(param_id_t id, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return;

    const param_desc_t *desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    param_store_set_active(id, clamped);

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
                && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
        {
            return;
        }
    }

    if ((live_parameter_is_audio_owned(id) != 0U)
            || (id == PARAM_MASTER_GAIN))
    {
        float command_value = clamped;
        if ((param_registry_prepare_global_audio_command(
                id, clamped, &command_value) == 0U)
                || (param_registry_submit_audio_value(
                    id, 0U, command_value,
                    LIVE_PARAMETER_EVENT_SCOPE_GLOBAL) == 0U))
        {
            return;
        }
        return;
    }

    if (desc->apply != NULL)
    {
        desc->apply(clamped);
    }
}

/**
 * @brief Point d'entrée param_reset.
 *
 * Rôle:
 * - Exécuter le traitement associé à param_reset.
 *
 * @param id Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
/* Command surface: reset global canonical value to default. */
void param_reset(param_id_t id)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
        return;

    param_set(id, param_registry[id].default_value);
}
