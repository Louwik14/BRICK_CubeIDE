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
#include "Param/param_filter.h"
#include "Param/param_registry_backends.h"
#include "Param/param_registry_runtime_state.h"
#include "mixer.h"
#include "Seq/seq_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_tone_sound_state.h"
#include "Core/track_sound_state.h"
#include "Core/track_state.h"
#include "Mod/mod_lfo_v1.h"
#include "UI/ui_track_catalog.h"
#include <stddef.h>
#include <string.h>

static void param_registry_neutralize_filter_runtime_if_invalid(uint8_t track);
static void param_registry_neutralize_vca_runtime_if_invalid(uint8_t track);
static uint8_t param_apply_non_filter_track_value_core(param_id_t id,
                                                       uint8_t track,
                                                       float clamped,
                                                       uint8_t rt_fast);
static uint8_t param_registry_get_track_sound_value(param_id_t id, uint8_t track, float *out_value);
static uint8_t param_registry_get_track_tone_value(param_id_t id, uint8_t track, float *out_value);
static uint8_t param_registry_set_track_tone_value(param_id_t id, uint8_t track, float value);

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

/*
 * Legacy MIX params (PARAM_MIX_TRACKx_*) are storage tombstones/load-only compat.
 * Normal MIX runtime writes use PARAM_MIX_* through param_registry_apply_track_value(track,...).
 */
#define PARAM_MIX_LEGACY_TRACK_COUNT MAX_TRACKS
#if PARAM_MIX_LEGACY_TRACK_COUNT != 4U
#error "Legacy PARAM_MIX_TRACKx mapping assumes 4 physical tracks"
#endif

uint8_t param_registry_is_legacy_physical_mix_param(param_id_t id)
{
    return ((id >= PARAM_MIX_TRACK0_GAIN)
            && (id <= PARAM_MIX_TRACK3_SEND1)
            && (id != PARAM_MIX_MUTE)) ? 1U : 0U;
}

extern const param_desc_t param_registry[PARAM_COUNT];
#define FILTER_RUNTIME_REBIND_NONE 0xFFU

static uint8_t g_param_registry_batch_depth = 0U;
static volatile uint8_t g_param_registry_track_structure_transition_depth = 0U;

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

static void param_registry_track_structure_transition_begin(void)
{
    if (g_param_registry_track_structure_transition_depth < 255U)
    {
        g_param_registry_track_structure_transition_depth++;
    }
}

static void param_registry_track_structure_transition_end(void)
{
    if (g_param_registry_track_structure_transition_depth > 0U)
    {
        g_param_registry_track_structure_transition_depth--;
    }
}

uint8_t param_registry_track_structure_transition_is_active(void)
{
    return (g_param_registry_track_structure_transition_depth != 0U) ? 1U : 0U;
}

static uint8_t param_lfo_map(param_id_t id, uint8_t *out_lfo_index, mod_lfo_param_t *out_lfo_param)
{
    if ((out_lfo_index == NULL) || (out_lfo_param == NULL))
    {
        return 0U;
    }

    switch (id)
    {
        case PARAM_LFO1_DEST:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_DEST;
            return 1U;
        case PARAM_LFO1_RATE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO1_DEPTH:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_DEPTH;
            return 1U;
        case PARAM_LFO1_SHAPE:
            *out_lfo_index = 0U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        case PARAM_LFO2_DEST:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_DEST;
            return 1U;
        case PARAM_LFO2_RATE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_RATE;
            return 1U;
        case PARAM_LFO2_DEPTH:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_DEPTH;
            return 1U;
        case PARAM_LFO2_SHAPE:
            *out_lfo_index = 1U;
            *out_lfo_param = MOD_LFO_PARAM_SHAPE;
            return 1U;
        default:
            return 0U;
    }
}

static uint8_t param_runtime_apply_track(uint8_t track,
                                         param_id_t id,
                                         float value,
                                         uint8_t update_base_state)
{
    const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
    if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
    {
        return 0U;
    }

    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx == NULL) || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND))
    {
        return 0U;
    }

    if ((rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_TONE)
            && (param_backend_track_supports_midi_tone_ctx(ctx) != 0U))
    {
        if (param_backend_is_midi_cc_id(id) != 0U)
        {
            if (param_backend_send_midi_cc(track, id, value) == 0U)
            {
                return 0U;
            }
            param_registry_runtime_cache_set(track, id, value);
            return 1U;
        }

        if (id == PARAM_MIDI_PROGRAM)
        {
            return 0U;
        }
    }

    uint8_t applied = 0U;
    if (rule.domain == TRACK_RUNTIME_PARAM_DOMAIN_MIX)
    {
        applied = param_backend_apply_mix_track(ctx, track, id, value);
    }
    else if (rule.resource == TRACK_RUNTIME_RESOURCE_BUFFER)
    {
        applied = param_backend_apply_buffer_track(ctx, track, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_SAMPLER)
    {
        applied = param_backend_apply_tone_sampler(track, id, value);
    }
    else if (ctx->engine == (uint8_t)TRACK_RUNTIME_ENGINE_DRUM)
    {
        applied = param_backend_apply_tone_drum(track, ctx, id, value, update_base_state);
    }

    if (applied != 0U)
    {
        param_registry_runtime_cache_set(track, id, value);
    }

    return applied;
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
            *out_value = state->hybrid_gate;
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
        case PARAM_SAMPLER_FADE_IN:
            *out_value = state->fade_in;
            return 1U;
        case PARAM_SAMPLER_FADE_OUT:
            *out_value = state->fade_out;
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
            *out_value = state->slice_count;
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
        case PARAM_DRUM_TRX_CLAVES_PITCH:
            *out_value = state->trx_claves.pitch;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_INTERVAL:
            *out_value = state->trx_claves.interval;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_DECAY:
            *out_value = state->trx_claves.decay;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_BALANCE:
            *out_value = state->trx_claves.balance;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_DRIVE:
            *out_value = state->trx_claves.drive;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_DECAY:
            *out_value = state->trx_hihat.decay;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_METAL:
            *out_value = state->trx_hihat.metal;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_HP_TONE:
            *out_value = state->trx_hihat.hp_tone;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_LP_TONE:
            *out_value = state->trx_hihat.lp_tone;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_GAP:
            *out_value = state->trx_hihat.gap;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_PEAK:
            *out_value = state->trx_hihat.peak;
            return 1U;
        case PARAM_DRUM_FM_KICK_PITCH:
            *out_value = state->fm_kick.pitch;
            return 1U;
        case PARAM_DRUM_FM_KICK_DECAY:
            *out_value = state->fm_kick.decay;
            return 1U;
        case PARAM_DRUM_FM_KICK_FM_AMOUNT:
            *out_value = state->fm_kick.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_KICK_PITCH_SWEEP:
            *out_value = state->fm_kick.pitch_sweep;
            return 1U;
        case PARAM_DRUM_FM_KICK_FEEDBACK:
            *out_value = state->fm_kick.feedback;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_FREQ:
            *out_value = state->fm_kick.mod_freq;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_DECAY:
            *out_value = state->fm_kick.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_KICK_SWEEP_DECAY:
            *out_value = state->fm_kick.sweep_decay;
            return 1U;
        case PARAM_DRUM_FM_KICK_RATIO_MODE:
            *out_value = state->fm_kick.ratio_mode;
            return 1U;
        case PARAM_DRUM_FM_KICK_RATIO_INDEX:
            *out_value = state->fm_kick.ratio_index;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_ENV_SYNC:
            *out_value = state->fm_kick.mod_env_sync;
            return 1U;
        case PARAM_DRUM_FM_SNARE_PITCH:
            *out_value = state->fm_snare.pitch;
            return 1U;
        case PARAM_DRUM_FM_SNARE_DECAY:
            *out_value = state->fm_snare.decay;
            return 1U;
        case PARAM_DRUM_FM_SNARE_FM_AMOUNT:
            *out_value = state->fm_snare.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_SNARE_NOISE:
            *out_value = state->fm_snare.noise;
            return 1U;
        case PARAM_DRUM_FM_SNARE_HP_TONE:
            *out_value = state->fm_snare.hp_tone;
            return 1U;
        case PARAM_DRUM_FM_SNARE_MOD_FREQ:
            *out_value = state->fm_snare.mod_freq;
            return 1U;
        case PARAM_DRUM_FM_SNARE_MOD_DECAY:
            *out_value = state->fm_snare.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_SNARE_NOISE_DECAY:
            *out_value = state->fm_snare.noise_decay;
            return 1U;
        case PARAM_DRUM_FM_TOM_PITCH:
            *out_value = state->fm_tom.pitch;
            return 1U;
        case PARAM_DRUM_FM_TOM_DECAY:
            *out_value = state->fm_tom.decay;
            return 1U;
        case PARAM_DRUM_FM_TOM_PITCH_SWEEP:
            *out_value = state->fm_tom.pitch_sweep;
            return 1U;
        case PARAM_DRUM_FM_TOM_FM_AMOUNT:
            *out_value = state->fm_tom.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_TOM_MOD_FREQ:
            *out_value = state->fm_tom.mod_freq;
            return 1U;
        case PARAM_DRUM_FM_TOM_MOD_DECAY:
            *out_value = state->fm_tom.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_TOM_SWEEP_DECAY:
            *out_value = state->fm_tom.sweep_decay;
            return 1U;
        case PARAM_DRUM_FM_TOM_START_PHASE:
            *out_value = state->fm_tom.start_phase;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_PITCH:
            *out_value = state->fm_rimshot.rim_pitch;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_DECAY:
            *out_value = state->fm_rimshot.rim_decay;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_MIX:
            *out_value = state->fm_rimshot.body_mix;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_HP_TONE:
            *out_value = state->fm_rimshot.hp_tone;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT:
            *out_value = state->fm_rimshot.rim_fm_amount;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_PITCH:
            *out_value = state->fm_rimshot.body_pitch;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_DECAY:
            *out_value = state->fm_rimshot.body_decay;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT:
            *out_value = state->fm_rimshot.body_fm_amount;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_MOD_DECAY:
            *out_value = state->fm_rimshot.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_COUNT:
            *out_value = state->fm_clap.clap_count;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_SPACING:
            *out_value = state->fm_clap.clap_spacing;
            return 1U;
        case PARAM_DRUM_FM_CLAP_TAIL_DECAY:
            *out_value = state->fm_clap.tail_decay;
            return 1U;
        case PARAM_DRUM_FM_CLAP_HP_TONE:
            *out_value = state->fm_clap.hp_tone;
            return 1U;
        case PARAM_DRUM_FM_CLAP_FEEDBACK:
            *out_value = state->fm_clap.feedback;
            return 1U;
        case PARAM_DRUM_FM_CLAP_FM_AMOUNT:
            *out_value = state->fm_clap.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_CLAP_BASE_FREQ:
            *out_value = state->fm_clap.base_freq;
            return 1U;
        case PARAM_DRUM_FM_CLAP_MOD_FREQ:
            *out_value = state->fm_clap.mod_freq;
            return 1U;
        case PARAM_DRUM_FM_CLAP_MOD_DECAY:
            *out_value = state->fm_clap.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_DECAY:
            *out_value = state->fm_clap.clap_decay;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_PITCH:
            *out_value = state->fm_cowbell.pitch;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_DECAY_SHORT:
            *out_value = state->fm_cowbell.decay_short;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_DECAY_LONG:
            *out_value = state->fm_cowbell.decay_long;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_FM_AMOUNT:
            *out_value = state->fm_cowbell.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_FEEDBACK:
            *out_value = state->fm_cowbell.feedback;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_ENV_MIX:
            *out_value = state->fm_cowbell.env_mix;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_MOD_DECAY:
            *out_value = state->fm_cowbell.mod_decay;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_MOD_FREQ:
            *out_value = state->fm_cowbell.mod_freq;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_DECAY:
            *out_value = state->fm_cymbal.decay;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_SUSTAIN:
            *out_value = state->fm_cymbal.sustain;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_FM_AMOUNT:
            *out_value = state->fm_cymbal.fm_amount;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_HP_TONE:
            *out_value = state->fm_cymbal.hp_tone;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_FEEDBACK:
            *out_value = state->fm_cymbal.feedback;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_BASE_CARRIER:
            *out_value = state->fm_cymbal.base_carrier;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_BASE_MOD:
            *out_value = state->fm_cymbal.base_mod;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_MOD_DECAY:
            *out_value = state->fm_cymbal.mod_decay;
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
            state->mode = value;
            return 1U;
        case PARAM_SAMPLER_TUNE:
            state->tune = value;
            return 1U;
        case PARAM_SAMPLER_FADE_IN:
            state->fade_in = value;
            return 1U;
        case PARAM_SAMPLER_FADE_OUT:
            state->fade_out = value;
            return 1U;
        case PARAM_SAMPLER_SLICE_COUNT:
            state->slice_count = value;
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
        case PARAM_DRUM_TRX_CLAVES_PITCH:
            state->trx_claves.pitch = value;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_INTERVAL:
            state->trx_claves.interval = value;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_DECAY:
            state->trx_claves.decay = value;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_BALANCE:
            state->trx_claves.balance = value;
            return 1U;
        case PARAM_DRUM_TRX_CLAVES_DRIVE:
            state->trx_claves.drive = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_DECAY:
            state->trx_hihat.decay = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_METAL:
            state->trx_hihat.metal = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_HP_TONE:
            state->trx_hihat.hp_tone = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_LP_TONE:
            state->trx_hihat.lp_tone = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_GAP:
            state->trx_hihat.gap = value;
            return 1U;
        case PARAM_DRUM_TRX_HIHAT_PEAK:
            state->trx_hihat.peak = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_PITCH:
            state->fm_kick.pitch = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_DECAY:
            state->fm_kick.decay = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_FM_AMOUNT:
            state->fm_kick.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_PITCH_SWEEP:
            state->fm_kick.pitch_sweep = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_FEEDBACK:
            state->fm_kick.feedback = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_FREQ:
            state->fm_kick.mod_freq = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_DECAY:
            state->fm_kick.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_SWEEP_DECAY:
            state->fm_kick.sweep_decay = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_RATIO_MODE:
            state->fm_kick.ratio_mode = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_RATIO_INDEX:
            state->fm_kick.ratio_index = value;
            return 1U;
        case PARAM_DRUM_FM_KICK_MOD_ENV_SYNC:
            state->fm_kick.mod_env_sync = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_PITCH:
            state->fm_snare.pitch = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_DECAY:
            state->fm_snare.decay = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_FM_AMOUNT:
            state->fm_snare.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_NOISE:
            state->fm_snare.noise = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_HP_TONE:
            state->fm_snare.hp_tone = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_MOD_FREQ:
            state->fm_snare.mod_freq = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_MOD_DECAY:
            state->fm_snare.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_SNARE_NOISE_DECAY:
            state->fm_snare.noise_decay = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_PITCH:
            state->fm_tom.pitch = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_DECAY:
            state->fm_tom.decay = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_PITCH_SWEEP:
            state->fm_tom.pitch_sweep = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_FM_AMOUNT:
            state->fm_tom.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_MOD_FREQ:
            state->fm_tom.mod_freq = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_MOD_DECAY:
            state->fm_tom.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_SWEEP_DECAY:
            state->fm_tom.sweep_decay = value;
            return 1U;
        case PARAM_DRUM_FM_TOM_START_PHASE:
            state->fm_tom.start_phase = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_PITCH:
            state->fm_rimshot.rim_pitch = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_DECAY:
            state->fm_rimshot.rim_decay = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_MIX:
            state->fm_rimshot.body_mix = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_HP_TONE:
            state->fm_rimshot.hp_tone = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_RIM_FM_AMOUNT:
            state->fm_rimshot.rim_fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_PITCH:
            state->fm_rimshot.body_pitch = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_DECAY:
            state->fm_rimshot.body_decay = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_BODY_FM_AMOUNT:
            state->fm_rimshot.body_fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_RIMSHOT_MOD_DECAY:
            state->fm_rimshot.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_COUNT:
            state->fm_clap.clap_count = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_SPACING:
            state->fm_clap.clap_spacing = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_TAIL_DECAY:
            state->fm_clap.tail_decay = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_HP_TONE:
            state->fm_clap.hp_tone = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_FEEDBACK:
            state->fm_clap.feedback = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_FM_AMOUNT:
            state->fm_clap.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_BASE_FREQ:
            state->fm_clap.base_freq = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_MOD_FREQ:
            state->fm_clap.mod_freq = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_MOD_DECAY:
            state->fm_clap.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_CLAP_CLAP_DECAY:
            state->fm_clap.clap_decay = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_PITCH:
            state->fm_cowbell.pitch = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_DECAY_SHORT:
            state->fm_cowbell.decay_short = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_DECAY_LONG:
            state->fm_cowbell.decay_long = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_FM_AMOUNT:
            state->fm_cowbell.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_FEEDBACK:
            state->fm_cowbell.feedback = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_ENV_MIX:
            state->fm_cowbell.env_mix = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_MOD_DECAY:
            state->fm_cowbell.mod_decay = value;
            return 1U;
        case PARAM_DRUM_FM_COWBELL_MOD_FREQ:
            state->fm_cowbell.mod_freq = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_DECAY:
            state->fm_cymbal.decay = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_SUSTAIN:
            state->fm_cymbal.sustain = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_FM_AMOUNT:
            state->fm_cymbal.fm_amount = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_HP_TONE:
            state->fm_cymbal.hp_tone = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_FEEDBACK:
            state->fm_cymbal.feedback = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_BASE_CARRIER:
            state->fm_cymbal.base_carrier = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_BASE_MOD:
            state->fm_cymbal.base_mod = value;
            return 1U;
        case PARAM_DRUM_FM_CYMBAL_MOD_DECAY:
            state->fm_cymbal.mod_decay = value;
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
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_DEST;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_get_track_param(track, lfo_index, lfo_param, out_value);
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
uint8_t param_registry_apply_track_value_rt_fast(param_id_t id, uint8_t track, float value)
{
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

static void param_registry_capture_runtime_mix_targets(uint8_t *out_mix_tracks)
{
    if (out_mix_tracks == NULL)
    {
        return;
    }

    track_runtime_refresh_all();
    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        out_mix_tracks[track] = FILTER_RUNTIME_REBIND_NONE;

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx == NULL)
                || (ctx->bind_state != TRACK_RUNTIME_BIND_BOUND)
                || (ctx->mix_track_id >= MIXER_MAX_TRACKS))
        {
            continue;
        }

        out_mix_tracks[track] = ctx->mix_track_id;
    }
}

static void param_registry_mark_runtime_global_dirty(void)
{
    track_runtime_invalidate_all();
}

static uint8_t param_registry_get_reapply_lane_bound_track_value(param_id_t id,
                                                                 uint8_t track,
                                                                 float *out_value)
{
    if ((id >= PARAM_COUNT) || (track >= SEQ_TRACK_COUNT) || (out_value == NULL))
    {
        return 0U;
    }

    if (param_filter_is_param(id) != 0U)
    {
        /* FILTER, MIX and VCA authority is shadow-state per logical track, not runtime cache. */
        return param_registry_get_track_value(id, track, out_value);
    }

    if (param_registry_get_track_sound_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    if (param_registry_get_track_tone_value(id, track, out_value) != 0U)
    {
        return 1U;
    }

    /*
     * Lane-bound reapply runs after mixer lane rebind already restored runtime states.
     * On non-FILTER/VCA params, a cache miss is not authoritative and must not promote
     * descriptor defaults that would overwrite the freshly rebound runtime values.
     */
    return param_registry_runtime_cache_get(track, id, out_value);
}

static void param_registry_reapply_lane_bound_runtime_for_changed_tracks(const uint8_t *previous_mix_tracks)
{
    static const param_id_t k_lane_bound_params[] = {
        PARAM_FILTER_TYPE,
        PARAM_FILTER_CUTOFF,
        PARAM_FILTER_RESONANCE,
        PARAM_FILTER_EG_AMT,
        PARAM_FILTER_ATTACK,
        PARAM_FILTER_DECAY,
        PARAM_FILTER_SUSTAIN,
        PARAM_FILTER_RELEASE,
        PARAM_FILTER_KEYTRK,
        PARAM_FILTER_ENVRST,
        PARAM_FILTER_ENVDLY,
        PARAM_FILTER_EQ_LOW,
        PARAM_FILTER_EQ_MID,
        PARAM_FILTER_EQ_HIGH,
        PARAM_FILTER_DRIVE,
        PARAM_FILTER_DECIMATOR_BITS,
        PARAM_FILTER_DECIMATOR_RATE,
        PARAM_FILTER_DECIMATOR_RATE2,
        PARAM_MIX_LEVEL,
        PARAM_MIX_PAN,
        PARAM_MIX_SEND1,
        PARAM_MIX_SEND2,
        PARAM_MIX_MUTE,
        PARAM_HYBRID_GATE,
        PARAM_VCA_ATTACK,
        PARAM_VCA_DECAY,
        PARAM_VCA_SUSTAIN,
        PARAM_VCA_RELEASE
    };

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        uint8_t previous_mix_track = FILTER_RUNTIME_REBIND_NONE;
        uint8_t current_mix_track = FILTER_RUNTIME_REBIND_NONE;
        uint8_t migrated_existing_lane = 0U;

        const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
        if ((ctx != NULL)
                && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
                && (ctx->mix_track_id < MIXER_MAX_TRACKS))
        {
            current_mix_track = ctx->mix_track_id;
        }

        if (previous_mix_tracks != NULL)
        {
            previous_mix_track = previous_mix_tracks[track];

            if (previous_mix_track == current_mix_track)
            {
                continue;
            }

            migrated_existing_lane = ((previous_mix_track < MIXER_MAX_TRACKS)
                    && (current_mix_track < MIXER_MAX_TRACKS)) ? 1U : 0U;
        }

        for (uint8_t i = 0U; i < (uint8_t)(sizeof(k_lane_bound_params) / sizeof(k_lane_bound_params[0])); ++i)
        {
            if ((migrated_existing_lane != 0U)
                    && (param_filter_is_param(k_lane_bound_params[i]) != 0U))
            {
                /*
                 * Runtime FILTER state for lane migrations is already moved by
                 * mixer_rebind_track_states(previous_mix_tracks, next_mix_tracks,...).
                 * Reapplying from FILTER shadow here can overwrite that good runtime state.
                 */
                continue;
            }

            float value = 0.0f;
            if (param_registry_get_reapply_lane_bound_track_value(k_lane_bound_params[i], track, &value) == 0U)
            {
                continue;
            }

            (void)param_registry_apply_track_value(k_lane_bound_params[i], track, value);
        }
    }
}

static void param_registry_rebind_lane_runtime(const uint8_t *previous_mix_tracks)
{
    uint8_t next_mix_tracks[SEQ_TRACK_COUNT];

    if (previous_mix_tracks == NULL)
    {
        return;
    }

    param_registry_capture_runtime_mix_targets(next_mix_tracks);
    mixer_rebind_track_states(previous_mix_tracks, next_mix_tracks, SEQ_TRACK_COUNT);
    param_registry_mark_runtime_global_dirty();
}

static void param_registry_neutralize_filter_runtime_if_invalid(uint8_t track)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_set_track_filter_type((uint32_t)mix_track, MIXER_TRACK_FILTER_OFF);
    mixer_track_filter_all_notes_off((uint32_t)mix_track);
}

static void param_registry_neutralize_vca_runtime_if_invalid(uint8_t track)
{
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_runtime_refresh_track(track);
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(track);
    if ((ctx != NULL)
            && (ctx->bind_state == TRACK_RUNTIME_BIND_BOUND)
            && ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                || (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_DRUM)
                || ((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_INPUT)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_HYBRID))))
    {
        return;
    }

    if (track_runtime_is_audio_routable(track) == 0U)
    {
        return;
    }

    if (track_runtime_get_mix_target_track(track, &mix_track) == 0U)
    {
        return;
    }

    mixer_track_vca_all_notes_off((uint32_t)mix_track);
    mixer_set_track_vca_enabled((uint32_t)mix_track, 0U);
}
static void param_registry_finalize_track_structure_change(const uint8_t *previous_mix_tracks)
{
    if (previous_mix_tracks == NULL)
    {
        return;
    }

    param_registry_rebind_lane_runtime(previous_mix_tracks);

    for (uint8_t track = 0U; track < SEQ_TRACK_COUNT; ++track)
    {
        param_registry_neutralize_filter_runtime_if_invalid(track);
        param_registry_neutralize_vca_runtime_if_invalid(track);
    }

    param_registry_reapply_lane_bound_runtime_for_changed_tracks(previous_mix_tracks);
    param_registry_mark_runtime_global_dirty();
}

typedef struct
{
    uint8_t track;
    param_id_t id;
    float clamped;
    uint8_t rt_fast;
    track_runtime_param_rule_t rule;
    track_runtime_resolved_track_t resolved;
} param_track_exec_ctx_t;
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

    if ((rt_fast == 0U) && (g_param_registry_batch_depth == 0U))
    {
        track_runtime_refresh_track(track);
    }

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

    switch (ctx->rule.domain)
    {
        case TRACK_RUNTIME_PARAM_DOMAIN_TONE:
            if ((ctx->rt_fast == 0U) && (ctx->id == PARAM_MIDI_PROGRAM))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
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
                param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
                return 1U;
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_TRX_BD)
                    && (ctx->id >= PARAM_DRUM_TRX_BD_PITCH)
                    && (ctx->id <= PARAM_DRUM_TRX_BD_DRIVE))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_TRX_CLAVES)
                    && (ctx->id >= PARAM_DRUM_TRX_CLAVES_PITCH)
                    && (ctx->id <= PARAM_DRUM_TRX_CLAVES_DRIVE))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_TRX_HIHAT)
                    && (ctx->id >= PARAM_DRUM_TRX_HIHAT_DECAY)
                    && (ctx->id <= PARAM_DRUM_TRX_HIHAT_PEAK))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_KICK)
                    && (ctx->id >= PARAM_DRUM_FM_KICK_PITCH)
                    && (ctx->id <= PARAM_DRUM_FM_KICK_MOD_ENV_SYNC))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_SNARE)
                    && (ctx->id >= PARAM_DRUM_FM_SNARE_PITCH)
                    && (ctx->id <= PARAM_DRUM_FM_SNARE_NOISE_DECAY))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_TOM)
                    && (ctx->id >= PARAM_DRUM_FM_TOM_PITCH)
                    && (ctx->id <= PARAM_DRUM_FM_TOM_START_PHASE))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_RIMSHOT)
                    && (ctx->id >= PARAM_DRUM_FM_RIMSHOT_RIM_PITCH)
                    && (ctx->id <= PARAM_DRUM_FM_RIMSHOT_MOD_DECAY))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_CLAP)
                    && (ctx->id >= PARAM_DRUM_FM_CLAP_CLAP_COUNT)
                    && (ctx->id <= PARAM_DRUM_FM_CLAP_CLAP_DECAY))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_COWBELL)
                    && (ctx->id >= PARAM_DRUM_FM_COWBELL_PITCH)
                    && (ctx->id <= PARAM_DRUM_FM_COWBELL_MOD_FREQ))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            if ((ctx->rt_fast == 0U)
                    && (ctx->resolved.descriptor.type == TRACK_RUNTIME_TYPE_DRUM_FM_CYMBAL)
                    && (ctx->id >= PARAM_DRUM_FM_CYMBAL_DECAY)
                    && (ctx->id <= PARAM_DRUM_FM_CYMBAL_MOD_DECAY))
            {
                if (param_registry_set_track_tone_value(ctx->id, ctx->track, ctx->clamped) == 0U)
                {
                    return 0U;
                }
                {
                    const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
                    return param_backend_apply_tone_drum(ctx->track, runtime_ctx, ctx->id, ctx->clamped, 0U);
                }
            }

            return param_runtime_apply_track(ctx->track,
                                             ctx->id,
                                             ctx->clamped,
                                             (ctx->rt_fast == 0U) ? 1U : 0U);

        case TRACK_RUNTIME_PARAM_DOMAIN_MIX:
            if (param_runtime_apply_track(ctx->track, ctx->id, ctx->clamped, 1U) == 0U)
            {
                return 0U;
            }
            if (ctx->rt_fast == 0U)
            {
                param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
            }
            return 1U;

        case TRACK_RUNTIME_PARAM_DOMAIN_COLORS:
        {
            const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
            const uint8_t applied = param_backend_apply_colors_track(runtime_ctx, ctx->id, ctx->clamped);
            if (ctx->rt_fast != 0U)
            {
                if (applied != 0U)
                {
                    param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
                }
                return applied;
            }
            param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
            return 1U;
        }

        case TRACK_RUNTIME_PARAM_DOMAIN_BUFFER:
            {
            const track_runtime_ctx_t *const runtime_ctx = track_runtime_get_ctx(ctx->track);
            const uint8_t applied = param_backend_apply_buffer_track(runtime_ctx, ctx->track, ctx->id, ctx->clamped);
            if (applied != 0U)
            {
                param_registry_runtime_cache_set(ctx->track, ctx->id, ctx->clamped);
            }
            return applied;
        }

        case TRACK_RUNTIME_PARAM_DOMAIN_PLAY:
        case TRACK_RUNTIME_PARAM_DOMAIN_MOD:
        case TRACK_RUNTIME_PARAM_DOMAIN_NONE:
        default:
            return 0U;
    }
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

uint8_t param_registry_apply_track_value(param_id_t id, uint8_t track, float value)
{
    if (id >= PARAM_COUNT)
    {
        return 0U;
    }

    const param_desc_t *const desc = &param_registry[id];
    const float clamped = clamp_value(value, desc->min, desc->max);

    {
        uint8_t lfo_index = 0U;
        mod_lfo_param_t lfo_param = MOD_LFO_PARAM_DEST;
        if (param_lfo_map(id, &lfo_index, &lfo_param) != 0U)
        {
            return mod_lfo_v1_set_track_param(track, lfo_index, lfo_param, clamped);
        }
    }

    if (param_filter_is_param(id) != 0U)
    {
        return param_apply_filter_track_value(id, track, clamped);
    }

    return param_apply_non_filter_track_value(id, track, clamped);
}

uint8_t param_registry_apply_track_edit(const param_registry_track_edit_cmd_t *cmd)
{
    if ((cmd == NULL) || (cmd->id >= PARAM_COUNT) || (cmd->track >= SEQ_TRACK_COUNT))
    {
        return 0U;
    }

    return param_registry_apply_track_value(cmd->id, cmd->track, cmd->value);
}

uint8_t param_registry_run_track_transition_pipeline(const param_registry_track_transition_pipeline_cmd_t *cmd)
{
    uint8_t previous_mix_tracks[SEQ_TRACK_COUNT];

    if ((cmd == NULL) || (cmd->mutate_fn == NULL))
    {
        return 0U;
    }

    param_registry_capture_runtime_mix_targets(previous_mix_tracks);
    param_registry_track_structure_transition_begin();

    uint8_t ok = 1U;
    if ((cmd->prepare_fn != NULL) && (cmd->prepare_fn(cmd->ctx) == 0U))
    {
        ok = 0U;
    }

    if ((ok != 0U) && (cmd->mutate_fn(cmd->ctx) == 0U))
    {
        ok = 0U;
    }

    if (ok != 0U)
    {
        param_registry_finalize_track_structure_change(previous_mix_tracks);
    }

    param_registry_track_structure_transition_end();

    if (ok == 0U)
    {
        return 0U;
    }

    if ((cmd->reapply_fn != NULL) && (cmd->reapply_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->seq_runtime_sync_fn != NULL) && (cmd->seq_runtime_sync_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->ui_sync_fn != NULL) && (cmd->ui_sync_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    if ((cmd->resume_fn != NULL) && (cmd->resume_fn(cmd->ctx) == 0U))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t param_registry_apply_track_structure_transition_mutate(void *ctx)
{
    const param_registry_track_structure_transition_cmd_t *const cmd =
            (const param_registry_track_structure_transition_cmd_t *)ctx;

    if ((cmd == NULL) || (cmd->mutation_fn == NULL))
    {
        return 0U;
    }

    cmd->mutation_fn(cmd->mutation_ctx);
    return 1U;
}

void param_registry_apply_track_structure_transition(const param_registry_track_structure_transition_cmd_t *cmd)
{
    const param_registry_track_transition_pipeline_cmd_t pipeline_cmd = {
        .prepare_fn = NULL,
        .mutate_fn = param_registry_apply_track_structure_transition_mutate,
        .reapply_fn = NULL,
        .seq_runtime_sync_fn = NULL,
        .ui_sync_fn = NULL,
        .resume_fn = NULL,
        .ctx = (void *)cmd
    };

    (void)param_registry_run_track_transition_pipeline(&pipeline_cmd);
}
void param_registry_sync_filter_ui_for_active_track(void)
{
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
void param_reset(param_id_t id)
{
    if (id >= PARAM_COUNT)
        return;

    param_set(id, param_registry[id].default_value);
}
