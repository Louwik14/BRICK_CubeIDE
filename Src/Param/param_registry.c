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
#include "Param/param_macro.h"
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_param_iface.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_sound_state.h"
#include "Core/track_state.h"
#include "Storage/kit_v1.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_matrix.h"
#include "Sampler/multi_sample_pool.h"
#include "UI/ui_core.h"
#include "UI/ui_track_catalog.h"
#include <stddef.h>
#include <string.h>

static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast);
static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped);
static float clamp_value(float v, float lo, float hi);

static void param_registry_sync_active_cfg_mirror_for_track(uint8_t track)
{
    if ((track >= SEQ_TRACK_COUNT) || (track != ui_get_active_track()))
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
    if (track >= SEQ_TRACK_COUNT)
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

static uint8_t param_registry_track_is_sampler_multi(uint8_t track)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    return (uint8_t)((ctx != NULL)
                     && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                     && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI));
}

static float param_registry_multi_instrument_selector_value(uint8_t track)
{
    uint16_t instrument_id = MULTI_SAMPLE_POOL_INVALID_ID;
    if (brick6_sampler_runtime_get_multi_instrument(track, &instrument_id) == 0U)
    {
        return 0.0f;
    }
    if (instrument_id == MULTI_SAMPLE_POOL_INVALID_ID)
    {
        return 0.0f;
    }

    uint8_t selector = 1U;
    for (uint16_t id = 0U; id < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++id)
    {
        if (multi_sample_pool_get_instrument(id) == NULL)
        {
            continue;
        }
        if (id == instrument_id)
        {
            return (float)selector;
        }
        selector++;
    }

    return 0.0f;
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

    switch (id)
    {
        case PARAM_LFO1_RATE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO1_SHAPE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        case PARAM_LFO1_DELAY:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_DELAY;
            return 1U;
        case PARAM_LFO1_TRIG:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_TRIG;
            return 1U;
        case PARAM_LFO1_FADE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_FADE;
            return 1U;
        case PARAM_LFO1_PHASE_SLEW:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_PHASE_SLEW;
            return 1U;
        case PARAM_LFO2_RATE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO2_SHAPE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        case PARAM_LFO2_DELAY:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_DELAY;
            return 1U;
        case PARAM_LFO2_TRIG:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_TRIG;
            return 1U;
        case PARAM_LFO2_FADE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_FADE;
            return 1U;
        case PARAM_LFO2_PHASE_SLEW:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_PHASE_SLEW;
            return 1U;
        default:
            return 0U;
    }
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

static uint8_t param_apply_play_track_value(param_id_t id, uint8_t track, float clamped)
{
    seq_param_slot_t param_slot = 0U;
    if (seq_param_iface_param_to_slot(track, (uint8_t)SEQ_PLOCK_SET_PLAY, id, &param_slot) == 0U)
    {
        return 0U;
    }

    if (id == PARAM_MIDI_PROGRAM)
    {
        if (seq_param_iface_set_play_base_value(track,
                                                param_slot,
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

    if (seq_param_iface_set_play_base_value(track,
                                            param_slot,
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
        case PARAM_HYBRID_GATE:
            *out_value = state->input.hybrid_gate;
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

    switch (id)
    {
        case PARAM_SAMPLER_SAMPLE:
            if (param_registry_track_is_sampler_multi(track) != 0U)
            {
                *out_value = param_registry_multi_instrument_selector_value(track);
                return 1U;
            }
            *out_value = state->sample;
            return 1U;
        case PARAM_SAMPLER_GAIN:
        {
            const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
            if ((ctx != NULL) && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))
            {
                *out_value = brick6_sampler_runtime_get_multi_gain(track);
                return 1U;
            }
            *out_value = state->gain;
            return 1U;
        }
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
        case PARAM_MASTER_FX1_TYPE:
        case PARAM_MASTER_FX2_TYPE:
        case PARAM_MASTER_FX3_TYPE:
        case PARAM_MASTER_FX4_TYPE:
            *out_value = state->master_fx.type[(uint8_t)((id - PARAM_MASTER_FX1_TYPE) / 4U)];
            return 1U;
        case PARAM_MASTER_FX1_LEVEL:
        case PARAM_MASTER_FX2_LEVEL:
        case PARAM_MASTER_FX3_LEVEL:
        case PARAM_MASTER_FX4_LEVEL:
            *out_value = state->master_fx.level[(uint8_t)((id - PARAM_MASTER_FX1_LEVEL) / 4U)];
            return 1U;
        case PARAM_MASTER_FX1_A:
        case PARAM_MASTER_FX2_A:
        case PARAM_MASTER_FX3_A:
        case PARAM_MASTER_FX4_A:
            *out_value = state->master_fx.macro_a[(uint8_t)((id - PARAM_MASTER_FX1_A) / 4U)];
            return 1U;
        case PARAM_MASTER_FX1_B:
        case PARAM_MASTER_FX2_B:
        case PARAM_MASTER_FX3_B:
        case PARAM_MASTER_FX4_B:
            *out_value = state->master_fx.macro_b[(uint8_t)((id - PARAM_MASTER_FX1_B) / 4U)];
            return 1U;
        case PARAM_WAVE_EDIT:
            *out_value = state->wave.edit;
            return 1U;
        case PARAM_WAVE_FINE:
            *out_value = state->wave.fine;
            return 1U;
        case PARAM_WAVE_COARSE:
            *out_value = state->wave.coarse;
            return 1U;
        case PARAM_WAVE_FM:
            *out_value = state->wave.fm;
            return 1U;
        case PARAM_WAVE_TIMBRE:
            *out_value = state->wave.timbre;
            return 1U;
        case PARAM_WAVE_MODULATION:
            *out_value = state->wave.modulation;
            return 1U;
        case PARAM_WAVE_COLOR:
            *out_value = state->wave.color;
            return 1U;
        case PARAM_WAVE_PHASE_RESET:
            *out_value = state->wave.phase_reset;
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
        case PARAM_MASTER_FX1_TYPE:
        case PARAM_MASTER_FX2_TYPE:
        case PARAM_MASTER_FX3_TYPE:
        case PARAM_MASTER_FX4_TYPE:
            state->master_fx.type[(uint8_t)((id - PARAM_MASTER_FX1_TYPE) / 4U)] = value;
            return 1U;
        case PARAM_MASTER_FX1_LEVEL:
        case PARAM_MASTER_FX2_LEVEL:
        case PARAM_MASTER_FX3_LEVEL:
        case PARAM_MASTER_FX4_LEVEL:
            state->master_fx.level[(uint8_t)((id - PARAM_MASTER_FX1_LEVEL) / 4U)] = value;
            return 1U;
        case PARAM_MASTER_FX1_A:
        case PARAM_MASTER_FX2_A:
        case PARAM_MASTER_FX3_A:
        case PARAM_MASTER_FX4_A:
            state->master_fx.macro_a[(uint8_t)((id - PARAM_MASTER_FX1_A) / 4U)] = value;
            return 1U;
        case PARAM_MASTER_FX1_B:
        case PARAM_MASTER_FX2_B:
        case PARAM_MASTER_FX3_B:
        case PARAM_MASTER_FX4_B:
            state->master_fx.macro_b[(uint8_t)((id - PARAM_MASTER_FX1_B) / 4U)] = value;
            return 1U;
        case PARAM_WAVE_EDIT:
            state->wave.edit = value;
            return 1U;
        case PARAM_WAVE_FINE:
            state->wave.fine = value;
            return 1U;
        case PARAM_WAVE_COARSE:
            state->wave.coarse = value;
            return 1U;
        case PARAM_WAVE_FM:
            state->wave.fm = value;
            return 1U;
        case PARAM_WAVE_TIMBRE:
            state->wave.timbre = value;
            return 1U;
        case PARAM_WAVE_MODULATION:
            state->wave.modulation = value;
            return 1U;
        case PARAM_WAVE_COLOR:
            state->wave.color = value;
            return 1U;
        case PARAM_WAVE_PHASE_RESET:
            state->wave.phase_reset = clamp_value(value, 0.0f, 1.0f);
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

    return param_backend_apply_tone_drum(ctx->track, track_runtime_get_ctx(ctx->track), ctx->id, ctx->clamped, 0U);
}

static uint8_t param_track_exec_ctx_build(param_track_exec_ctx_t *ctx,
                                          uint8_t track,
                                          param_id_t id,
                                          float clamped,
                                          track_runtime_param_rule_t rule,
                                          uint8_t rt_fast)
{
    if ((ctx == NULL) || (track >= SEQ_TRACK_COUNT))
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

static uint8_t param_track_exec_apply_backend(const param_track_exec_ctx_t *ctx)
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
                                                                        TRACK_RUNTIME_TYPE_DRUM_TRX_BD,
                                                                        PARAM_DRUM_TRX_BD_PITCH,
                                                                        PARAM_DRUM_TRX_BD_DRIVE);
        if (applied != 2U)
        {
            return applied;
        }
    }

    return param_backend_apply_track_value(ctx->track,
                                           ctx->id,
                                           ctx->clamped,
                                           (ctx->rt_fast == 0U) ? 1U : 0U);
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

    param_track_exec_ctx_t ctx;
    if (param_track_exec_ctx_build(&ctx, track, id, clamped, rule, rt_fast) == 0U)
    {
        return 0U;
    }

    if (param_track_exec_authorize(&ctx) == 0U)
    {
        return 0U;
    }

    return param_track_exec_sync_after_apply(&ctx, param_track_exec_apply_backend(&ctx));
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

    if (track < SEQ_TRACK_COUNT)
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

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            return mod_env3_get_track_param(track, env_param, out_value);
        }
    }

    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_PLAY)
        {
            seq_param_slot_t param_slot = 0U;
            seq_value16_t encoded = 0U;
            if (seq_param_iface_param_to_slot(track, (uint8_t)SEQ_PLOCK_SET_PLAY, id, &param_slot) == 0U)
            {
                return 0U;
            }

            if (seq_param_iface_get_play_base_value(track, param_slot, &encoded) == 0U)
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
            if (track >= SEQ_TRACK_COUNT)
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
    if (id >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    if (param_filter_is_param(id) != 0U)
    {
        return param_filter_apply_value(id, track, clamped, 0U, 0U);
    }

    return param_apply_non_filter_track_value_rt_fast(id, track, clamped);
}

uint8_t param_registry_apply_track_value_runtime_temp(param_id_t id, uint8_t track, float value)
{
    if (id >= PARAM_COUNT)
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

/* Command surface: track-aware apply and post-commit routing. */
uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    if (id >= PARAM_COUNT)
    {
        return 0U;
    }

    if (param_registry_batch_is_active() == 0U)
    {
        track_runtime_refresh_track(track);
    }
    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    if ((id == PARAM_CFG_TRACK) || (id == PARAM_CFG_TRACK_TYPE))
    {
        const uint8_t ok = param_apply_cfg_track_value(id, track, clamped);
        if (ok != 0U)
        {
            kit_v1_mark_dirty();
        }
        return ok;
    }

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_RATE;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            const uint8_t ok = mod_lfo_v1_set_track_param(track, lfo_index, lfo_param, clamped);
            if (ok != 0U)
            {
                kit_v1_mark_dirty();
            }
            return ok;
        }
    }

    if ((id == PARAM_MOD_MATRIX_SLOT)
            || (id == PARAM_MOD_MATRIX_SOURCE)
            || (id == PARAM_MOD_MATRIX_DEST)
            || (id == PARAM_MOD_MATRIX_DEPTH))
    {
        const uint8_t ok = param_matrix_set_track_value(id, track, clamped);
        if (ok != 0U)
        {
            kit_v1_mark_dirty();
        }
        return ok;
    }

    {
        mod_env3_param_t env_param = MOD_ENV3_PARAM_ATTACK;
        if (param_env3_map(id, &env_param) != 0U)
        {
            const uint8_t ok = mod_env3_set_track_param(track, env_param, clamped);
            if (ok != 0U)
            {
                kit_v1_mark_dirty();
            }
            return ok;
        }
    }

    if (param_filter_is_param(id) != 0U)
    {
        const uint8_t ok = param_apply_filter_track_value(id, track, clamped);
        if (ok != 0U)
        {
            kit_v1_mark_dirty();
        }
        return ok;
    }

    const uint8_t ok = param_apply_non_filter_track_value(id, track, clamped);
    if (ok != 0U)
    {
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
                || (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_COLORS))
        {
            kit_v1_mark_dirty();
        }
    }
    return ok;
}

/* Command surface: UI edit command forwarded to the track-aware apply seam. */
uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd)
{
    if ((cmd == NULL) || (cmd->id >= PARAM_COUNT) || (cmd->track >= SEQ_TRACK_COUNT))
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
    if (id >= PARAM_COUNT)
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
    if (id >= PARAM_COUNT)
        return;

    param_set(id, param_registry[id].default_value);
}
