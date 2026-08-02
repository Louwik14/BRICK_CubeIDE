#include "Core/track_snapshot.h"

#include <string.h>

#include "Audio/drum_synth.h"
#include "Audio/mixer.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_input_ownership.h"
#include "Core/track_state.h"
#include "Core/synth_polyphony.h"
#include "Keyboard/keyboard_engine.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "Param/param_registry.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime_control.h"
#include "UI/ui_active_track_sync.h"

#define TRACK_SNAPSHOT_LOCK_NONE 0xFFFFU

static uint8_t g_track_snapshot_voice_limited;
static uint8_t g_track_snapshot_voice_max;

uint8_t track_snapshot_last_voice_limited(void) { return g_track_snapshot_voice_limited; }
uint8_t track_snapshot_last_voice_max(void) { return g_track_snapshot_voice_max; }

typedef struct
{
    const uint8_t *family;
    const uint8_t *type;
    const uint8_t *external_input;
    const uint8_t *midi_channel;
    const uint8_t *midi_source;
} track_snapshot_structure_apply_ctx_t;

static uint8_t track_snapshot_track_is_valid(uint8_t track)
{
    return (track < UI_TRACK_COUNT) ? 1U : 0U;
}

static void track_snapshot_add_restore_track(uint8_t track,
                                             seq_track_id_t *tracks,
                                             uint8_t *track_count)
{
    if ((track >= SEQ_TRACK_COUNT) || (tracks == 0) || (track_count == 0))
    {
        return;
    }

    for (uint8_t i = 0U; i < *track_count; ++i)
    {
        if (tracks[i] == (seq_track_id_t)track)
        {
            return;
        }
    }
    if (*track_count < SEQ_TRACK_COUNT)
    {
        tracks[*track_count] = (seq_track_id_t)track;
        (*track_count)++;
    }
}

static void track_snapshot_collect_restore_tracks(uint8_t track,
                                                  uint8_t include_preceding_group,
                                                  seq_track_id_t *tracks,
                                                  uint8_t *track_count)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    track_snapshot_add_restore_track(track, tracks, track_count);
    (void)include_preceding_group;
}

static void track_snapshot_runtime_quiesce_engine(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    note_fx_pipeline_reset_runtime_overrides(track);
    keyboard_engine_all_notes_off_for_track(track);
    mod_lfo_v1_all_notes_off(track);
    brick6_sampler_runtime_reset_track(track);
    brick6_looper_runtime_stop_playback(track);
    brick6_looper_runtime_prepare_replace(track);
    synth_polyphony_reset_track(track);
    drum_synth_all_notes_off_for_instance(track);
    param_registry_clear_track_runtime_state(track);
}

static void track_snapshot_runtime_neutralize_note_state(uint8_t track)
{
    uint8_t filter_track = 0U;
    uint8_t mix_track = 0U;

    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    (void)note_fx_pipeline_transition_track(
        track, NOTE_FX_TRANSITION_MODEL_RECONFIGURE);
    keyboard_engine_all_notes_off_for_track(track);
    mod_lfo_v1_all_notes_off(track);
    brick6_sampler_runtime_reset_track(track);
    brick6_looper_runtime_stop_playback(track);
    synth_polyphony_reset_track(track);
    drum_synth_all_notes_off_for_instance(track);

    track_runtime_refresh_track(track);
    if (track_runtime_resolve_filter_target_track(track, &filter_track) != 0U)
    {
        mixer_track_filter_all_notes_off(filter_track);
    }
    if (track_runtime_get_mix_target_track(track, &mix_track) != 0U)
    {
        mixer_track_vca_all_notes_off(mix_track);
    }
    param_registry_clear_track_runtime_state(track);
}

static uint8_t track_snapshot_apply_structure_mutation(void *ctx_ptr)
{
    const track_snapshot_structure_apply_ctx_t *const ctx =
        (const track_snapshot_structure_apply_ctx_t *)ctx_ptr;

    if (ctx == 0)
    {
        return 0U;
    }

    if (ui_apply_track_config_bulk_mutation_with_inputs(ctx->family,
                                                        ctx->type,
                                                        ctx->midi_channel,
                                                        ctx->midi_source,
                                                        ctx->external_input) == false)
    {
        return 0U;
    }

    track_runtime_invalidate_all();
    return 1U;
}

static uint8_t track_snapshot_capture_sequence(uint8_t track, track_snapshot_t *out_snapshot)
{
    if ((track >= SEQ_TRACK_COUNT) || (out_snapshot == 0))
    {
        return 0U;
    }

    memset(&out_snapshot->sequence, 0, sizeof(out_snapshot->sequence));
    track_snapshot_sequence_t *const saved = &out_snapshot->sequence;
    saved->track.length_steps = seq_model_get_track_length((seq_track_id_t)track);
    saved->track.ui_page = seq_model_get_track_page((seq_track_id_t)track);
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_step_t *const dst_step = &saved->track.steps[step];
        dst_step->lock_head = TRACK_SNAPSHOT_LOCK_NONE;
        dst_step->trig = seq_model_get_trig((seq_track_id_t)track, step);
        dst_step->roll = seq_model_get_step_roll((seq_track_id_t)track, step);
        if (seq_model_step_plock_collect((seq_track_id_t)track, step,
                                         saved->step_locks[step].locks,
                                         SEQ_STEP_MAX_LOCKS,
                                         &saved->step_locks[step].count) == 0U) return 0U;
    }

    (void)seq_runtime_get_track_div((seq_track_id_t)track, &out_snapshot->seq_div);
    (void)seq_runtime_get_track_quant((seq_track_id_t)track, &out_snapshot->seq_quant);
    (void)seq_runtime_get_track_swing((seq_track_id_t)track, &out_snapshot->seq_swing);
    return 1U;
}

static void track_snapshot_apply_sequence(uint8_t track, const track_snapshot_t *snapshot)
{
    if ((track >= SEQ_TRACK_COUNT) || (snapshot == 0))
    {
        return;
    }
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) != 0U)
    {
        return;
    }

    const track_snapshot_sequence_t *const saved = &snapshot->sequence;
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig((seq_track_id_t)track, step, saved->track.steps[step].trig);
        seq_model_set_step_roll((seq_track_id_t)track, step, saved->track.steps[step].roll);
        seq_model_step_plock_clear((seq_track_id_t)track, step);
        const uint8_t count = saved->step_locks[step].count;
        for (uint8_t lock = 0U; lock < count; ++lock)
        {
            const seq_plock_entry_t *const entry = &saved->step_locks[step].locks[lock];
            (void)seq_model_step_plock_upsert((seq_track_id_t)track, step, entry->set_id,
                                              entry->param_slot, entry->value16, entry->flags);
        }
    }
    seq_model_set_track_length((seq_track_id_t)track, saved->track.length_steps);
    seq_model_set_track_page((seq_track_id_t)track, saved->track.ui_page);
    seq_runtime_restore_track_div((seq_track_id_t)track, snapshot->seq_div);
    seq_runtime_set_track_quant((seq_track_id_t)track, snapshot->seq_quant);
    seq_runtime_set_track_swing((seq_track_id_t)track, snapshot->seq_swing);
}

static void track_snapshot_reapply_track_params(uint8_t track)
{
    track_runtime_refresh_track(track);
    mod_matrix_rebuild_route_cache_track(track);

    param_registry_batch_begin();
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        const track_runtime_param_rule_t rule = track_runtime_get_param_rule(id);
        if ((rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_ENV)
                && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_TONE)
                && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MOD)
                && (rule.domain != TRACK_RUNTIME_PARAM_DOMAIN_MIX))
        {
            continue;
        }
        if (id == PARAM_LOOPER_PLAY)
        {
            continue;
        }
        if (track_runtime_get_effective_param_status(track, id) != TRACK_RUNTIME_PARAM_ALLOWED)
        {
            continue;
        }

        float value = 0.0f;
        if (param_registry_get_track_value(id, track, &value) != 0U)
        {
            (void)param_registry_apply_track_value(id, track, value);
        }
    }
    param_registry_batch_end();
}

uint8_t track_snapshot_capture(uint8_t track, track_snapshot_t *out_snapshot)
{
    if ((track_snapshot_track_is_valid(track) == 0U) || (out_snapshot == 0))
    {
        return 0U;
    }

    const track_sound_state_t *const sound = track_sound_state_get_const(track);
    const track_tone_sound_state_t *const tone = track_tone_sound_state_get_const(track);
    if ((sound == 0) || (tone == 0))
    {
        return 0U;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->config = ui_get_track_config(track);
    out_snapshot->external_input = ui_get_track_external_input(track);
    out_snapshot->midi_channel = ui_get_track_midi_channel(track);
    out_snapshot->midi_source = ui_get_track_midi_source(track);
    if ((out_snapshot->config.family == UI_TRACK_FAMILY_SYNTH)
            || (out_snapshot->config.family == UI_TRACK_FAMILY_DRUM))
    {
        out_snapshot->poly_voice_count = synth_polyphony_get_voice_count(track);
        out_snapshot->poly_spread = synth_polyphony_get_spread(track);
    }
    else if ((out_snapshot->config.family == UI_TRACK_FAMILY_SAMPLER)
            && (out_snapshot->config.type == UI_TRACK_TYPE_MULTI))
    {
        out_snapshot->poly_voice_count = brick6_sampler_runtime_get_multi_voice_count(track);
        out_snapshot->poly_spread = brick6_sampler_runtime_get_multi_spread(track);
    }
    memcpy(&out_snapshot->sound, sound, sizeof(out_snapshot->sound));
    memcpy(&out_snapshot->tone, tone, sizeof(out_snapshot->tone));
    if ((track < NOTE_FX_TRACK_COUNT)
            && (note_fx_state_capture_track(track, &out_snapshot->note_fx) == 0U))
    {
        return 0U;
    }

    if (track_snapshot_capture_sequence(track, out_snapshot) == 0U)
    {
        return 0U;
    }

    out_snapshot->valid = 1U;
    return 1U;
}

uint8_t track_snapshot_make_default(uint8_t track, track_snapshot_t *out_snapshot)
{
    if ((track_snapshot_track_is_valid(track) == 0U) || (out_snapshot == 0))
    {
        return 0U;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    out_snapshot->config.family = UI_TRACK_FAMILY_OFF;
    out_snapshot->config.type = UI_TRACK_TYPE_NONE;
    out_snapshot->external_input = (uint8_t)(track % TRACK_TOPOLOGY_PHYSICAL_INPUT_COUNT);
    out_snapshot->midi_channel = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
    out_snapshot->midi_source = UI_TRACK_MIDI_SRC_ALL;
    out_snapshot->poly_voice_count = 1U;
    out_snapshot->poly_spread = 0.0f;
    track_sound_state_make_default(&out_snapshot->sound);
    track_tone_sound_state_make_default(&out_snapshot->tone);
    for (uint8_t slot = 0U; slot < NOTE_FX_SLOT_COUNT; ++slot)
    {
        out_snapshot->note_fx.value[slot][0] = 2U;
        out_snapshot->note_fx.value[slot][1] = 0U;
        out_snapshot->note_fx.value[slot][2] = 1U;
        out_snapshot->note_fx.value[slot][3] = NOTE_FX_MODEL_OFF;
    }

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
        out_snapshot->sequence.track.steps[step].lock_head = TRACK_SNAPSHOT_LOCK_NONE;
    out_snapshot->sequence.track.length_steps = SEQ_DEFAULT_LENGTH_STEPS;
    out_snapshot->sequence.track.ui_page = 0U;
    out_snapshot->seq_div = 1U;
    out_snapshot->seq_quant = 0U;
    out_snapshot->seq_swing = 0U;
    out_snapshot->valid = 1U;
    return 1U;
}

uint8_t track_snapshot_apply_ex(uint8_t target_track,
                                const track_snapshot_t *snapshot,
                                const track_snapshot_apply_options_t *options)
{
    g_track_snapshot_voice_limited = 0U;
    g_track_snapshot_voice_max = 0U;
    if ((track_snapshot_track_is_valid(target_track) == 0U)
            || (snapshot == 0)
            || (snapshot->valid == 0U))
    {
        return 0U;
    }

    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t external_input[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        family[track] = (uint8_t)cfg.family;
        type[track] = (uint8_t)cfg.type;
        external_input[track] = ui_get_track_external_input(track);
        midi_channel[track] = ui_get_track_midi_channel(track);
        midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
    }

    if ((options != 0)
            && (options->clear_source_track != 0U)
            && (options->source_track < UI_TRACK_COUNT)
            && (options->source_track != target_track))
    {
        family[options->source_track] = (uint8_t)UI_TRACK_FAMILY_OFF;
        type[options->source_track] = (uint8_t)UI_TRACK_TYPE_NONE;
    }

    const ui_track_family_t target_family =
        ((options != 0) && (options->has_family_override != 0U))
            ? options->family_override
            : snapshot->config.family;

    uint8_t applied_voice_count = snapshot->poly_voice_count;
    const uint8_t target_is_multi = (uint8_t)((target_family == UI_TRACK_FAMILY_SAMPLER)
        && (snapshot->config.type == UI_TRACK_TYPE_MULTI));
    if ((target_family == UI_TRACK_FAMILY_SYNTH) || (target_family == UI_TRACK_FAMILY_DRUM))
    {
        const uint8_t maximum = synth_polyphony_get_available_for_track(target_track);
        if (maximum == 0U)
        {
            g_track_snapshot_voice_max = 1U;
            return 0U;
        }
        if (applied_voice_count < 1U) applied_voice_count = 1U;
        if (target_family == UI_TRACK_FAMILY_DRUM) applied_voice_count = 1U;
        if (applied_voice_count > maximum)
        {
            applied_voice_count = maximum;
            g_track_snapshot_voice_limited = 1U;
        }
    }
    else if (target_is_multi != 0U)
    {
        if (applied_voice_count < 1U) applied_voice_count = 1U;
        if (applied_voice_count > SAMPLER_MULTI_MAX_VOICES_PER_TRACK)
        {
            applied_voice_count = SAMPLER_MULTI_MAX_VOICES_PER_TRACK;
            g_track_snapshot_voice_limited = 1U;
            g_track_snapshot_voice_max = SAMPLER_MULTI_MAX_VOICES_PER_TRACK;
        }
    }

    family[target_track] = (uint8_t)target_family;
    type[target_track] = (uint8_t)snapshot->config.type;
    external_input[target_track] = snapshot->external_input;
    midi_channel[target_track] = snapshot->midi_channel;
    midi_source[target_track] = (uint8_t)snapshot->midi_source;

    ui_track_config_t ownership_configs[UI_TRACK_COUNT];
    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        ownership_configs[track].family = (ui_track_family_t)family[track];
        ownership_configs[track].type = (ui_track_type_t)type[track];
    }
    if (track_input_ownership_validate_bulk(ownership_configs, external_input) == 0U)
    {
        return 0U;
    }

    seq_track_id_t restore_tracks[SEQ_TRACK_COUNT];
    uint8_t restore_track_count = 0U;
    track_snapshot_collect_restore_tracks(
        target_track,
        0U,
        restore_tracks,
        &restore_track_count);
    if ((options != 0)
            && (options->clear_source_track != 0U)
            && (options->source_track < UI_TRACK_COUNT)
            && (options->source_track != target_track))
    {
        track_snapshot_collect_restore_tracks(options->source_track,
                                              0U,
                                              restore_tracks,
                                              &restore_track_count);
    }
    seq_runtime_begin_track_restore(restore_tracks, restore_track_count);
    for (uint8_t i = 0U; i < restore_track_count; ++i)
    {
        track_snapshot_runtime_quiesce_engine((uint8_t)restore_tracks[i]);
    }

    uint8_t apply_ok = 0U;
    track_snapshot_structure_apply_ctx_t structure_ctx = {
        .family = family,
        .type = type,
        .external_input = external_input,
        .midi_channel = midi_channel,
        .midi_source = midi_source
    };
    const param_registry_track_transition_pipeline_cmd_t pipeline_cmd = {
        .prepare_fn = 0,
        .mutate_fn = track_snapshot_apply_structure_mutation,
        .reapply_fn = 0,
        .seq_runtime_sync_fn = 0,
        .ui_sync_fn = 0,
        .resume_fn = 0,
        .ctx = &structure_ctx
    };
    if (param_registry_run_track_transition_pipeline_for_track(&pipeline_cmd, target_track) == 0U)
    {
        goto restore_done;
    }

    track_sound_state_t *const dst_sound = track_sound_state_get(target_track);
    track_tone_sound_state_t *const dst_tone = track_tone_sound_state_get(target_track);
    if ((dst_sound == 0) || (dst_tone == 0))
    {
        goto restore_done;
    }
    memcpy(dst_sound, &snapshot->sound, sizeof(*dst_sound));
    memcpy(dst_tone, &snapshot->tone, sizeof(*dst_tone));
    if ((target_track < NOTE_FX_TRACK_COUNT)
            && (note_fx_state_restore_track(target_track, &snapshot->note_fx) == 0U))
    {
        goto restore_done;
    }

    track_runtime_invalidate_all();
    track_runtime_refresh_all();

    if ((target_family == UI_TRACK_FAMILY_SYNTH) || (target_family == UI_TRACK_FAMILY_DRUM)
            || (target_is_multi != 0U))
    {
        if (param_registry_apply_track_value(PARAM_CFG_POLY_VOICES,
                                              target_track,
                                              (float)applied_voice_count) == 0U)
        {
            goto restore_done;
        }
        if ((target_family == UI_TRACK_FAMILY_SYNTH) || (target_is_multi != 0U))
        {
            if (param_registry_apply_track_value(PARAM_CFG_POLY_SPREAD,
                                                  target_track,
                                                  snapshot->poly_spread) == 0U)
            {
                goto restore_done;
            }
        }
    }

    track_snapshot_apply_sequence(target_track, snapshot);
    track_snapshot_reapply_track_params(target_track);
    ui_active_track_sync_full_after_reconfigure();
    apply_ok = 1U;

restore_done:
    for (uint8_t i = 0U; i < restore_track_count; ++i)
    {
        track_snapshot_runtime_neutralize_note_state((uint8_t)restore_tracks[i]);
    }
    seq_runtime_end_track_restore(restore_tracks, restore_track_count);
    return apply_ok;
}

uint8_t track_snapshot_apply(uint8_t target_track, const track_snapshot_t *snapshot)
{
    return track_snapshot_apply_ex(target_track, snapshot, 0);
}
