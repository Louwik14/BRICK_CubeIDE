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
#include "NoteFx/note_fx_state.h"
#include "NoteFx/note_fx_pipeline.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "Param/param_macro.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_param_iface.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Audio/md_model.h"
#include "Core/track_sound_state.h"
#include "Core/track_state.h"
#include "Core/live_parameter_migration.h"
#include "Core/live_parameter_audio_queue.h"
#include "Core/live_clock.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix.h"
#include "UI/ui_core.h"
#include "UI/ui_track_catalog.h"
#include "Keyboard/keyboard_engine.h"
#include <stddef.h>
#include <string.h>

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
    return live_parameter_audio_queue_submit_bulk(&bulk) ? 1U : 0U;
}
static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped);
static float clamp_value(float v, float lo, float hi);

static uint8_t param_registry_prism_param_slot(param_id_t id, uint8_t *out_osc, uint8_t *out_param)
{
    if ((out_osc == NULL) || (out_param == NULL))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_PRISM_EDIT: *out_osc = 0U; *out_param = 0U; return 1U;
        case PARAM_PRISM_FINE: *out_osc = 0U; *out_param = 1U; return 1U;
        case PARAM_PRISM_COARSE: *out_osc = 0U; *out_param = 2U; return 1U;
        case PARAM_PRISM_FM: *out_osc = 0U; *out_param = 3U; return 1U;
        case PARAM_PRISM_TIMBRE: *out_osc = 0U; *out_param = 4U; return 1U;
        case PARAM_PRISM_MODULATION: *out_osc = 0U; *out_param = 5U; return 1U;
        case PARAM_PRISM_COLOR: *out_osc = 0U; *out_param = 6U; return 1U;
        case PARAM_PRISM_PHASE_RESET: *out_osc = 0U; *out_param = 7U; return 1U;
        case PARAM_PRISM_LEVEL: *out_osc = 0U; *out_param = 8U; return 1U;
        case PARAM_PRISM_OSC2_EDIT: *out_osc = 1U; *out_param = 0U; return 1U;
        case PARAM_PRISM_OSC2_FINE: *out_osc = 1U; *out_param = 1U; return 1U;
        case PARAM_PRISM_OSC2_COARSE: *out_osc = 1U; *out_param = 2U; return 1U;
        case PARAM_PRISM_OSC2_FM: *out_osc = 1U; *out_param = 3U; return 1U;
        case PARAM_PRISM_OSC2_TIMBRE: *out_osc = 1U; *out_param = 4U; return 1U;
        case PARAM_PRISM_OSC2_MODULATION: *out_osc = 1U; *out_param = 5U; return 1U;
        case PARAM_PRISM_OSC2_COLOR: *out_osc = 1U; *out_param = 6U; return 1U;
        case PARAM_PRISM_OSC2_PHASE_RESET: *out_osc = 1U; *out_param = 7U; return 1U;
        case PARAM_PRISM_OSC2_LEVEL: *out_osc = 1U; *out_param = 8U; return 1U;
        default: return 0U;
    }
}

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

static uint8_t param_registry_batch_is_active(void)
{
    return (g_param_registry_batch_depth != 0U) ? 1U : 0U;
}

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

static uint8_t param_mod_operator_get_track_value(param_id_t id, uint8_t track, float *out_value)
{
    switch (id)
    {
        case PARAM_MOD_MULTI_1_A:
            return mod_matrix_get_multi_source(track, 0U, 0U, out_value);
        case PARAM_MOD_MULTI_1_B:
            return mod_matrix_get_multi_source(track, 0U, 1U, out_value);
        case PARAM_MOD_MULTI_2_A:
            return mod_matrix_get_multi_source(track, 1U, 0U, out_value);
        case PARAM_MOD_MULTI_2_B:
            return mod_matrix_get_multi_source(track, 1U, 1U, out_value);
        case PARAM_MOD_SLEW_1_SOURCE:
            return mod_matrix_get_slew_source(track, 0U, out_value);
        case PARAM_MOD_SLEW_1_AMOUNT:
            return mod_matrix_get_slew_amount(track, 0U, out_value);
        case PARAM_MOD_SLEW_2_SOURCE:
            return mod_matrix_get_slew_source(track, 1U, out_value);
        case PARAM_MOD_SLEW_2_AMOUNT:
            return mod_matrix_get_slew_amount(track, 1U, out_value);
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

        param_registry_runtime_commit_authoritative_write(track, id, clamped, 1U);
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

    param_registry_runtime_commit_authoritative_write(track, id, clamped, 1U);
    return 1U;
}

static uint8_t param_registry_get_track_sound_value(param_id_t id, uint8_t track, float *out_value)
{
    const track_sound_state_t *const state = track_sound_state_get_const(track);

    if ((state == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_MIX_LEVEL:
            *out_value = state->mix_level;
            return 1U;
        case PARAM_MIX_PAN:
            *out_value = state->mix_pan;
            return 1U;
        case PARAM_MIX_SEND1:
            *out_value = state->mix_send1;
            return 1U;
        case PARAM_MIX_SEND2:
            *out_value = state->mix_send2;
            return 1U;
        case PARAM_MIX_MUTE:
            *out_value = state->mix_mute;
            return 1U;
        case PARAM_VCA_ATTACK:
            *out_value = state->vca_attack;
            return 1U;
        case PARAM_VCA_DECAY:
            *out_value = state->vca_decay;
            return 1U;
        case PARAM_VCA_SUSTAIN:
            *out_value = state->vca_sustain;
            return 1U;
        case PARAM_VCA_RELEASE:
            *out_value = state->vca_release;
            return 1U;
        case PARAM_VCA_ENV_TYPE:
            *out_value = state->vca_env_type;
            return 1U;
        case PARAM_ENV_RETRIG_FILTER:
            *out_value = state->env_retrig_filter;
            return 1U;
        case PARAM_ENV_RETRIG_VCA:
            *out_value = state->env_retrig_vca;
            return 1U;
        case PARAM_ENV_RETRIG_MOD:
            *out_value = state->env_retrig_mod;
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_registry_get_track_tone_value(param_id_t id, uint8_t track, float *out_value)
{
    const track_tone_sound_state_t *const state = track_tone_sound_state_get_const(track);

    if ((state == NULL) || (out_value == NULL))
    {
        return 0U;
    }

    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        *out_value = state->fm.operator_params[offset / PARAM_FM_OPERATOR_PARAM_COUNT]
                                            [offset % PARAM_FM_OPERATOR_PARAM_COUNT];
        return 1U;
    }
    if ((id >= PARAM_FM_PLAY_VEL) && (id <= PARAM_FM_OPERATOR_SELECT))
    {
        switch (id)
        {
            case PARAM_FM_PLAY_VEL: *out_value = state->fm.play_vel; return 1U;
            case PARAM_FM_PLAY_KEY: *out_value = state->fm.play_key; return 1U;
            case PARAM_FM_PLAY_PITCH_ENV: *out_value = state->fm.pitch_env; return 1U;
            case PARAM_FM_PLAY_PITCH_TIME: *out_value = state->fm.pitch_time; return 1U;
            case PARAM_FM_OPERATOR_SELECT: *out_value = state->fm.operator_select; return 1U;
            default: break;
        }
    }

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            *out_value = state->sample;
            return 1U;
        case PARAM_SAMPLER_GAIN:
            *out_value = state->gain;
            return 1U;
        case PARAM_SAMPLER_START:
            *out_value = state->start;
            return 1U;
        case PARAM_SAMPLER_END:
            *out_value = state->end;
            return 1U;
        case PARAM_SAMPLER_MODE:
            *out_value = state->mode;
            return 1U;
        case PARAM_SAMPLER_TUNE:
            *out_value = state->tune;
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
            *out_value = state->slice_count;
            return 1U;
        case PARAM_SAMPLER_LOOP_START:
            *out_value = state->loop_start;
            return 1U;
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            *out_value = state->clip.source_bpm;
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            *out_value = state->clip.sync_length;
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            *out_value = state->clip.pitch;
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            *out_value = state->clip.play_mode;
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            *out_value = state->clip.loop;
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
            *out_value = clamp_value(state->clip.stretch_mode, 0.0f, 2.0f);
            return 1U;
        case PARAM_SAMPLER_CLIP_GRAIN:
            *out_value = state->clip.grain_size;
            return 1U;
        case PARAM_SAMPLER_CLIP_HOP:
            *out_value = state->clip.hop_size;
            return 1U;
        case PARAM_SAMPLER_CLIP_SEARCH:
            *out_value = state->clip.search_size;
            return 1U;
        case PARAM_SAMPLER_MULTI_LOOP:
            *out_value = state->multi.loop;
            return 1U;
        case PARAM_LOOPER_ARM:
            *out_value = state->looper.arm;
            return 1U;
        case PARAM_LOOPER_LEN:
            *out_value = state->looper.len;
            return 1U;
        case PARAM_LOOPER_PLAY:
            *out_value = state->looper.play;
            return 1U;
        case PARAM_LOOPER_XFADE:
            *out_value = state->looper.xfade;
            return 1U;
        case PARAM_LOOPER_STRETCH:
            *out_value = clamp_value(state->looper.stretch, 0.0f, 2.0f);
            return 1U;
        case PARAM_LOOPER_PITCH:
            *out_value = state->looper.pitch;
            return 1U;
        case PARAM_LOOPER_GRAIN:
            *out_value = state->looper.grain;
            return 1U;
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
        {
            uint8_t osc = 0U;
            uint8_t param = 0U;
            if (param_registry_prism_param_slot(id, &osc, &param) == 0U)
            {
                return 0U;
            }
            switch (param)
            {
                case 0U: *out_value = state->prism.edit[osc]; return 1U;
                case 1U: *out_value = state->prism.fine[osc]; return 1U;
                case 2U: *out_value = state->prism.coarse[osc]; return 1U;
                case 3U: *out_value = state->prism.fm[osc]; return 1U;
                case 4U: *out_value = state->prism.timbre[osc]; return 1U;
                case 5U: *out_value = state->prism.modulation[osc]; return 1U;
                case 6U: *out_value = state->prism.color[osc]; return 1U;
                case 7U: *out_value = state->prism.phase_reset[osc]; return 1U;
                case 8U: *out_value = state->prism.level[osc]; return 1U;
                default: return 0U;
            }
        }
        case PARAM_FM_RATIO: *out_value = state->fm.ratio; return 1U;
        case PARAM_FM_ALGORITHM: *out_value = state->fm.algorithm; return 1U;
        case PARAM_FM_FEEDBACK: *out_value = state->fm.feedback; return 1U;
        case PARAM_FM_SYNC: *out_value = state->fm.sync; return 1U;
        case PARAM_FM_BRIGHT: *out_value = state->fm.bright; return 1U;
        case PARAM_FM_BODY: *out_value = state->fm.body; return 1U;
        case PARAM_FM_DETAIL: *out_value = state->fm.detail; return 1U;
        case PARAM_FM_METAL: *out_value = state->fm.metal; return 1U;
        case PARAM_FM_ENV_ATTACK: *out_value = state->fm.env_attack; return 1U;
        case PARAM_FM_ENV_DECAY: *out_value = state->fm.env_decay; return 1U;
        case PARAM_FM_ENV_SUSTAIN: *out_value = state->fm.env_sustain; return 1U;
        case PARAM_FM_ENV_RELEASE: *out_value = state->fm.env_release; return 1U;
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
            *out_value = state->stack.level[(uint8_t)(id - PARAM_STACK_OSC1_LEVEL)];
            return 1U;
        case PARAM_STACK_NOISE_LEVEL:
            *out_value = state->stack.noise_level;
            return 1U;
        case PARAM_STACK_OSC1_MODEL:
        case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC3_MODEL:
            *out_value = state->stack.model[(uint8_t)((id - PARAM_STACK_OSC1_MODEL) / 4U)];
            return 1U;
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_TUNE:
            *out_value = state->stack.tune[(uint8_t)((id - PARAM_STACK_OSC1_TUNE) / 4U)];
            return 1U;
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC3_TIMBRE:
            *out_value = state->stack.timbre[(uint8_t)((id - PARAM_STACK_OSC1_TIMBRE) / 4U)];
            return 1U;
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_COLOR:
            *out_value = state->stack.color[(uint8_t)((id - PARAM_STACK_OSC1_COLOR) / 4U)];
            return 1U;
        case PARAM_STACK_OSC_DETUNE:
            *out_value = state->stack.osc_detune;
            return 1U;
        case PARAM_STACK_PHASE_RESET:
            *out_value = state->stack.phase_reset;
            return 1U;
        case PARAM_WAVE_OSC1_TABLE:
        case PARAM_WAVE_OSC2_TABLE:
            *out_value = state->wave.table[(uint8_t)((id - PARAM_WAVE_OSC1_TABLE) / 6U)];
            return 1U;
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC2_POS:
            *out_value = state->wave.pos[(uint8_t)((id - PARAM_WAVE_OSC1_POS) / 6U)];
            return 1U;
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC2_START:
            *out_value = state->wave.start[(uint8_t)((id - PARAM_WAVE_OSC1_START) / 6U)];
            return 1U;
        case PARAM_WAVE_OSC1_END:
        case PARAM_WAVE_OSC2_END:
            *out_value = state->wave.end[(uint8_t)((id - PARAM_WAVE_OSC1_END) / 6U)];
            return 1U;
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC2_LEVEL:
            *out_value = state->wave.level[(uint8_t)((id - PARAM_WAVE_OSC1_LEVEL) / 6U)];
            return 1U;
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_TUNE:
            *out_value = state->wave.tune[(uint8_t)((id - PARAM_WAVE_OSC1_TUNE) / 6U)];
            return 1U;
        case PARAM_WAVE_FRAME_INTERP:
            *out_value = state->wave.frame_interp;
            return 1U;
        case PARAM_WAVE_SAMPLE_INTERP:
            *out_value = state->wave.sample_interp;
            return 1U;
        case PARAM_WAVE_POS_UPDATE:
            *out_value = state->wave.pos_update;
            return 1U;
        case PARAM_WAVE_POS_SMOOTH:
            *out_value = state->wave.pos_smooth;
            return 1U;
        case PARAM_MIDI_PROGRAM:
            *out_value = state->midi_program;
            return 1U;
        case PARAM_MIDI_CC1_1:
            *out_value = state->midi_cc[0];
            return 1U;
        case PARAM_MIDI_CC1_2:
            *out_value = state->midi_cc[1];
            return 1U;
        case PARAM_MIDI_CC1_3:
            *out_value = state->midi_cc[2];
            return 1U;
        case PARAM_MIDI_CC1_4:
            *out_value = state->midi_cc[3];
            return 1U;
        case PARAM_MIDI_CC2_1:
            *out_value = state->midi_cc[4];
            return 1U;
        case PARAM_MIDI_CC2_2:
            *out_value = state->midi_cc[5];
            return 1U;
        case PARAM_MIDI_CC2_3:
            *out_value = state->midi_cc[6];
            return 1U;
        case PARAM_MIDI_CC2_4:
            *out_value = state->midi_cc[7];
            return 1U;
        case PARAM_MIDI_CC3_1:
            *out_value = state->midi_cc[8];
            return 1U;
        case PARAM_MIDI_CC3_2:
            *out_value = state->midi_cc[9];
            return 1U;
        case PARAM_MIDI_CC3_3:
            *out_value = state->midi_cc[10];
            return 1U;
        case PARAM_MIDI_CC3_4:
            *out_value = state->midi_cc[11];
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH:
            *out_value = state->trx_bd.pitch;
            return 1U;
        case PARAM_DRUM_TRX_BD_DECAY:
            *out_value = state->trx_bd.decay;
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            *out_value = state->trx_bd.pitch_sweep;
            return 1U;
        case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
            *out_value = state->trx_bd.sweep_decay;
            return 1U;
        case PARAM_DRUM_TRX_BD_ATTACK:
            *out_value = state->trx_bd.attack;
            return 1U;
        case PARAM_DRUM_TRX_BD_NOISE:
            *out_value = state->trx_bd.noise;
            return 1U;
        case PARAM_DRUM_TRX_BD_HARMONICS:
            *out_value = state->trx_bd.harmonics;
            return 1U;
        case PARAM_DRUM_TRX_BD_DRIVE:
            *out_value = state->trx_bd.drive;
            return 1U;
        case PARAM_DRUM_MD_MODEL:
            *out_value = state->md.model;
            return 1U;
        case PARAM_DRUM_MD_P1:
        case PARAM_DRUM_MD_P2:
        case PARAM_DRUM_MD_P3:
        case PARAM_DRUM_MD_P4:
        case PARAM_DRUM_MD_P5:
        case PARAM_DRUM_MD_P6:
        case PARAM_DRUM_MD_P7:
        case PARAM_DRUM_MD_P8:
            *out_value = state->md.slot[(uint8_t)(id - PARAM_DRUM_MD_P1)];
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_registry_set_track_tone_value(param_id_t id, uint8_t track, float value)
{
    track_tone_sound_state_t *const state = track_tone_sound_state_get(track);

    if (state == NULL)
    {
        return 0U;
    }

    if ((id >= PARAM_FM_OPERATOR_FIRST) && (id <= PARAM_FM_OPERATOR_LAST))
    {
        const uint16_t offset = (uint16_t)(id - PARAM_FM_OPERATOR_FIRST);
        state->fm.operator_params[offset / PARAM_FM_OPERATOR_PARAM_COUNT]
                                  [offset % PARAM_FM_OPERATOR_PARAM_COUNT] = value;
        return 1U;
    }
    if ((id >= PARAM_FM_PLAY_VEL) && (id <= PARAM_FM_OPERATOR_SELECT))
    {
        switch (id)
        {
            case PARAM_FM_PLAY_VEL: state->fm.play_vel = clamp_value(value, 0.0f, 1.0f); return 1U;
            case PARAM_FM_PLAY_KEY: state->fm.play_key = clamp_value(value, 0.0f, 1.0f); return 1U;
            case PARAM_FM_PLAY_PITCH_ENV: state->fm.pitch_env = clamp_value(value, -1.0f, 1.0f); return 1U;
            case PARAM_FM_PLAY_PITCH_TIME: state->fm.pitch_time = clamp_value(value, 0.0f, 1.0f); return 1U;
            case PARAM_FM_OPERATOR_SELECT: state->fm.operator_select = clamp_value(value, 0.0f, 5.0f); return 1U;
            default: break;
        }
    }

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            state->sample = value;
            return 1U;
        case PARAM_SAMPLER_GAIN:
            state->gain = value;
            return 1U;
        case PARAM_SAMPLER_START:
            state->start = value;
            return 1U;
        case PARAM_SAMPLER_END:
            state->end = value;
            return 1U;
        case PARAM_SAMPLER_MODE:
            state->mode = (value >= 4.0f) ? 0.0f : value;
            return 1U;
        case PARAM_SAMPLER_TUNE:
            state->tune = value;
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
            state->slice_count = value;
            return 1U;
        case PARAM_SAMPLER_LOOP_START:
            state->loop_start = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_SAMPLER_CLIP_SOURCE_BPM:
            state->clip.source_bpm = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_SYNC_LENGTH:
            state->clip.sync_length = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_PITCH:
            state->clip.pitch = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_PLAY_MODE:
            state->clip.play_mode = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_LOOP:
            state->clip.loop = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_STRETCH_MODE:
            state->clip.stretch_mode = clamp_value(value, 0.0f, 2.0f);
            return 1U;
        case PARAM_SAMPLER_CLIP_GRAIN:
            state->clip.grain_size = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_HOP:
            state->clip.hop_size = value;
            return 1U;
        case PARAM_SAMPLER_CLIP_SEARCH:
            state->clip.search_size = value;
            return 1U;
        case PARAM_SAMPLER_MULTI_LOOP:
            state->multi.loop = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_LOOPER_ARM:
            state->looper.arm = clamp_value(value, 0.0f, 2.0f);
            return 1U;
        case PARAM_LOOPER_LEN:
            state->looper.len = clamp_value(value, 0.0f, 5.0f);
            return 1U;
        case PARAM_LOOPER_PLAY:
            state->looper.play = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_LOOPER_XFADE:
            state->looper.xfade = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_LOOPER_STRETCH:
            state->looper.stretch = clamp_value(value, 0.0f, 2.0f);
            return 1U;
        case PARAM_LOOPER_PITCH:
            state->looper.pitch = clamp_value(value, -12.0f, 12.0f);
            return 1U;
        case PARAM_LOOPER_GRAIN:
            state->looper.grain = clamp_value(value, 0.0f, 5.0f);
            return 1U;
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
        {
            uint8_t osc = 0U;
            uint8_t param = 0U;
            if (param_registry_prism_param_slot(id, &osc, &param) == 0U)
            {
                return 0U;
            }
            switch (param)
            {
                case 0U: state->prism.edit[osc] = value; return 1U;
                case 1U: state->prism.fine[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 2U: state->prism.coarse[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 3U: state->prism.fm[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 4U: state->prism.timbre[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 5U: state->prism.modulation[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 6U: state->prism.color[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 7U: state->prism.phase_reset[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                case 8U: state->prism.level[osc] = clamp_value(value, 0.0f, 1.0f); return 1U;
                default: return 0U;
            }
        }
        case PARAM_FM_RATIO:
            state->fm.ratio = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_ALGORITHM:
            state->fm.algorithm = clamp_value(value, 0.0f, 31.0f); return 1U;
        case PARAM_FM_FEEDBACK:
            state->fm.feedback = clamp_value(value, 0.0f, 7.0f); return 1U;
        case PARAM_FM_SYNC:
            state->fm.sync = (value >= 0.5f) ? 1.0f : 0.0f; return 1U;
        case PARAM_FM_BRIGHT:
            state->fm.bright = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_BODY:
            state->fm.body = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_DETAIL:
            state->fm.detail = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_METAL:
            state->fm.metal = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_ENV_ATTACK:
            state->fm.env_attack = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_ENV_DECAY:
            state->fm.env_decay = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_ENV_SUSTAIN:
            state->fm.env_sustain = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_FM_ENV_RELEASE:
            state->fm.env_release = clamp_value(value, -1.0f, 1.0f); return 1U;
        case PARAM_STACK_OSC1_LEVEL:
        case PARAM_STACK_OSC2_LEVEL:
        case PARAM_STACK_OSC3_LEVEL:
            state->stack.level[(uint8_t)(id - PARAM_STACK_OSC1_LEVEL)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_STACK_NOISE_LEVEL:
            state->stack.noise_level = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_STACK_OSC1_MODEL:
        case PARAM_STACK_OSC2_MODEL:
        case PARAM_STACK_OSC3_MODEL:
            state->stack.model[(uint8_t)((id - PARAM_STACK_OSC1_MODEL) / 4U)] =
                clamp_value(value, 0.0f, (float)(BRICK6_STACK_MODEL_COUNT - 1U));
            return 1U;
        case PARAM_STACK_OSC1_TUNE:
        case PARAM_STACK_OSC2_TUNE:
        case PARAM_STACK_OSC3_TUNE:
            state->stack.tune[(uint8_t)((id - PARAM_STACK_OSC1_TUNE) / 4U)] = clamp_value(value, -24.0f, 24.0f);
            return 1U;
        case PARAM_STACK_OSC1_TIMBRE:
        case PARAM_STACK_OSC2_TIMBRE:
        case PARAM_STACK_OSC3_TIMBRE:
            state->stack.timbre[(uint8_t)((id - PARAM_STACK_OSC1_TIMBRE) / 4U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_STACK_OSC1_COLOR:
        case PARAM_STACK_OSC2_COLOR:
        case PARAM_STACK_OSC3_COLOR:
            state->stack.color[(uint8_t)((id - PARAM_STACK_OSC1_COLOR) / 4U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_STACK_OSC_DETUNE:
            state->stack.osc_detune = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_STACK_PHASE_RESET:
            state->stack.phase_reset = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_OSC1_TABLE:
        case PARAM_WAVE_OSC2_TABLE:
            state->wave.table[(uint8_t)((id - PARAM_WAVE_OSC1_TABLE) / 6U)] = clamp_value(value, 0.0f, param_registry[id].max);
            return 1U;
        case PARAM_WAVE_OSC1_POS:
        case PARAM_WAVE_OSC2_POS:
            state->wave.pos[(uint8_t)((id - PARAM_WAVE_OSC1_POS) / 6U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_OSC1_START:
        case PARAM_WAVE_OSC2_START:
            state->wave.start[(uint8_t)((id - PARAM_WAVE_OSC1_START) / 6U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_OSC1_END:
        case PARAM_WAVE_OSC2_END:
            state->wave.end[(uint8_t)((id - PARAM_WAVE_OSC1_END) / 6U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_OSC1_LEVEL:
        case PARAM_WAVE_OSC2_LEVEL:
            state->wave.level[(uint8_t)((id - PARAM_WAVE_OSC1_LEVEL) / 6U)] = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_OSC1_TUNE:
        case PARAM_WAVE_OSC2_TUNE:
            state->wave.tune[(uint8_t)((id - PARAM_WAVE_OSC1_TUNE) / 6U)] = clamp_value(value, -60.0f, 60.0f);
            return 1U;
        case PARAM_WAVE_FRAME_INTERP:
            state->wave.frame_interp = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_SAMPLE_INTERP:
            state->wave.sample_interp = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_WAVE_POS_UPDATE:
            state->wave.pos_update = clamp_value(value, 0.0f, 3.0f);
            return 1U;
        case PARAM_WAVE_POS_SMOOTH:
            state->wave.pos_smooth = clamp_value(value, 0.0f, 1.0f);
            return 1U;
        case PARAM_MIDI_PROGRAM:
            state->midi_program = value;
            return 1U;
        case PARAM_MIDI_CC1_1:
            state->midi_cc[0] = value;
            return 1U;
        case PARAM_MIDI_CC1_2:
            state->midi_cc[1] = value;
            return 1U;
        case PARAM_MIDI_CC1_3:
            state->midi_cc[2] = value;
            return 1U;
        case PARAM_MIDI_CC1_4:
            state->midi_cc[3] = value;
            return 1U;
        case PARAM_MIDI_CC2_1:
            state->midi_cc[4] = value;
            return 1U;
        case PARAM_MIDI_CC2_2:
            state->midi_cc[5] = value;
            return 1U;
        case PARAM_MIDI_CC2_3:
            state->midi_cc[6] = value;
            return 1U;
        case PARAM_MIDI_CC2_4:
            state->midi_cc[7] = value;
            return 1U;
        case PARAM_MIDI_CC3_1:
            state->midi_cc[8] = value;
            return 1U;
        case PARAM_MIDI_CC3_2:
            state->midi_cc[9] = value;
            return 1U;
        case PARAM_MIDI_CC3_3:
            state->midi_cc[10] = value;
            return 1U;
        case PARAM_MIDI_CC3_4:
            state->midi_cc[11] = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH:
            state->trx_bd.pitch = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_DECAY:
            state->trx_bd.decay = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_PITCH_SWEEP:
            state->trx_bd.pitch_sweep = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_SWEEP_DECAY:
            state->trx_bd.sweep_decay = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_ATTACK:
            state->trx_bd.attack = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_NOISE:
            state->trx_bd.noise = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_HARMONICS:
            state->trx_bd.harmonics = value;
            return 1U;
        case PARAM_DRUM_TRX_BD_DRIVE:
            state->trx_bd.drive = value;
            return 1U;
        case PARAM_DRUM_MD_MODEL:
        {
            const uint8_t model = md_model_validate(value);
            if ((uint8_t)state->md.model != model)
            {
                const md_model_profile_t *const profile = md_model_profile_get(model);
                state->md.model = model;
                for (uint8_t slot = 0U; slot < 8U; ++slot)
                {
                    state->md.slot[slot] = profile->defaults[slot];
                    param_registry_runtime_commit_authoritative_write(
                        track, (param_id_t)(PARAM_DRUM_MD_P1 + slot), state->md.slot[slot], 1U);
                }
            }
            return 1U;
        }
        case PARAM_DRUM_MD_P1:
        case PARAM_DRUM_MD_P2:
        case PARAM_DRUM_MD_P3:
        case PARAM_DRUM_MD_P4:
        case PARAM_DRUM_MD_P5:
        case PARAM_DRUM_MD_P6:
        case PARAM_DRUM_MD_P7:
        case PARAM_DRUM_MD_P8:
            state->md.slot[(uint8_t)(id - PARAM_DRUM_MD_P1)] =
                (uint8_t)(clamp_value(value, 0.0f, 127.0f) + 0.5f);
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_apply_non_filter_track_value_rt_fast(param_id_t id,
                                                           uint8_t track,
                                                           float clamped)
{
    return param_apply_non_filter_track_value_core(id, track, clamped, 1U);
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

    return param_backend_apply_track_value(
        ctx->track, ctx->id, ctx->clamped, 0U);
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

    if (ctx->resolved.descriptor.bind_state != TRACK_RUNTIME_BIND_BOUND)
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
        param_registry_runtime_commit_authoritative_write(ctx->track, ctx->id, ctx->clamped, 1U);
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
        param_registry_runtime_commit_authoritative_write(ctx->track, ctx->id, ctx->clamped, 1U);
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

    return param_backend_apply_track_value(ctx->track,
                                           ctx->id,
                                           ctx->clamped,
                                           update_base_state);
}

static uint8_t param_track_exec_sync_after_apply(const param_track_exec_ctx_t *ctx, uint8_t applied)
{
    if ((ctx == NULL) || (applied == 0U))
    {
        return 0U;
    }

    if (ctx->rt_fast == 0U)
    {
        param_registry_runtime_resync_lfo(ctx->track, ctx->id, ctx->clamped);
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

    /* Audio-owned parameters expose only their authoritative target cache.
     * This keeps UI and control queries away from private voice/backend state. */
    if (live_parameter_is_audio_owned(id) != 0U)
    {
        if ((track >= SEQ_LANE_CAPACITY)
                || (param_registry_runtime_cache_get(track, id, out_value) == 0U))
        {
            *out_value = param_registry[id].default_value;
        }
        return 1U;
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

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_get_track_param(track, lfo_index, lfo_param, out_value);
        }
    }

    if (param_matrix_get_track_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    if (param_mod_operator_get_track_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            return mod_env3_get_track_param(track, env_param, out_value);
        }
    }

    if (id == PARAM_ENV_RETRIG_MOD)
    {
        return mod_env3_get_track_retrigger_hard(track, out_value);
    }

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
        {
            seq_value16_t encoded = 0U;
            if (seq_param_iface_get_play_base_param(track, id, &encoded) == 0U)
            {
                return 0U;
            }

            *out_value = seq_param_iface_decode_param_value(id, encoded);
            return 1U;
        }
    }

    if (param_filter_get_track_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    if (param_registry_get_track_sound_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    if (param_registry_get_track_tone_value(id, track, out_value) != 0U)
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

            if (param_registry_runtime_cache_get(track, id, out_value) != 0U)
            {
                return 1U;
            }

            *out_value = param_registry[id].default_value;
            return 1U;
        }
    }

    *out_value = param_get(id);
    return 1U;
}
/* Command surface: RT fast-path apply reserved for modulation callers. */
uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value)
{
    /* RT fast path: same value semantics as apply_track_value, but restricted to modulation callers. */
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
    {
        return 0U;
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value(id, track, clamped, 0U, 0U);
    }

    return param_apply_non_filter_track_value_rt_fast(id, track, clamped);
}

uint8_t param_registry_apply_track_value_audio(param_id_t id, uint8_t track, float value)
{
    if ((id >= PARAM_COUNT)
            || (track >= SEQ_LANE_CAPACITY)
            || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    if ((id == PARAM_CFG_POLY_VOICES) || (id == PARAM_CFG_POLY_SPREAD))
        return 0U;

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t applied = param_filter_apply_value(id,
                                                          track,
                                                          clamped,
                                                          1U,
                                                          0U);
        if (applied != 0U)
        {
            param_registry_runtime_commit_authoritative_write(track, id, clamped, 1U);
        }
        return applied;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            || (rule.status == TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_CFG))
    {
        return 0U;
    }

    param_track_exec_ctx_t ctx;
    if (param_track_exec_ctx_build(&ctx, track, id, clamped, rule, 1U) == 0U)
    {
        return 0U;
    }
    if (param_track_exec_authorize(&ctx) == 0U)
    {
        return 0U;
    }

    const uint8_t applied = param_track_exec_apply_backend(&ctx, 1U);
    if (applied != 0U)
    {
        param_registry_runtime_commit_authoritative_write(track, id, clamped, 1U);
    }
    return applied;
}

uint8_t param_registry_apply_global_value_rt_fast(param_id_t id, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_NONE)
            && (rule.status != TRACK_RUNTIME_PARAM_GLOBAL_ALLOWED))
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);
    if (id == PARAM_MASTER_GAIN)
        return 0U;
    if (desc->apply == NULL)
    {
        return 0U;
    }

    desc->apply(clamped);
    return 1U;
}

uint8_t param_registry_apply_track_value_runtime_temp(param_id_t id, uint8_t track, float value)
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
    return live_parameter_audio_queue_submit_bulk(&bulk) ? 1U : 0U;
}

uint8_t param_registry_apply_track_value_runtime_temp_audio(param_id_t id, uint8_t track, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

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
        return param_filter_apply_value(id, track, clamped, 0U, 0U);
    }

    return param_apply_non_filter_track_value_rt_fast(id, track, clamped);
}

void param_registry_release_track_value_runtime_temp(param_id_t id, uint8_t track)
{
    uint8_t lfo_index = 0U;
    mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
    if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
    {
        mod_lfo_v1_clear_track_param_temp(track, lfo_index, lfo_param);
        return;
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            mod_env3_clear_track_param_temp(track, env_param);
        }
    }
}

void param_registry_clear_track_runtime_state(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        param_registry_release_track_value_runtime_temp((param_id_t)raw_id, track);
    }
    param_registry_runtime_cache_clear_track(track);
}

/* Command surface: track-aware apply and post-commit routing. */
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    if ((id >= PARAM_COUNT) || (param_id_is_reserved(id) != 0U))
    {
        return 0U;
    }

    if (param_registry_batch_is_active() == 0U)
    {
        track_runtime_refresh_track(track);
    }
    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

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
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        const uint8_t midi_tone = (uint8_t)(
            (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_backend_track_supports_midi_tone_ctx(
                    track_runtime_get_ctx(track)) != 0U));
        const uint8_t audio_command = (uint8_t)(
            (live_parameter_is_audio_owned(id) != 0U)
            || (id == PARAM_CFG_POLY_VOICES)
            || (id == PARAM_CFG_POLY_SPREAD)
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_ENV)
            || ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                && (midi_tone == 0U))
            || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX));
        if (audio_command != 0U)
        {
            param_registry_runtime_cache_set(track, id, clamped);
            return param_registry_submit_audio_value(
                id, track, clamped, LIVE_PARAMETER_EVENT_SCOPE_TRACK);
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
            return ok;
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

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            const uint8_t ok = mod_env3_set_track_param(track, env_param, clamped);
            return ok;
        }
    }

    if (id == PARAM_ENV_RETRIG_MOD)
    {
        const uint8_t ok = mod_env3_set_track_retrigger_hard(track, clamped);
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
    param_macro_init();
    param_filter_init();
    mod_lfo_v1_init();
    note_fx_state_init();
    param_registry_runtime_state_init();
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

    /* Global audio-owned values also have a control-side target shadow.  Keep
     * authoritative writes such as defaults/project restore coherent with the
     * encoder command path, which uses cache slot zero for global scope. */
    if ((live_parameter_is_audio_owned(id) != 0U)
            || (id == PARAM_MASTER_GAIN))
    {
        param_registry_runtime_cache_set(0U, id, clamped);
        (void)param_registry_submit_audio_value(
            id, 0U, clamped, LIVE_PARAMETER_EVENT_SCOPE_GLOBAL);
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
