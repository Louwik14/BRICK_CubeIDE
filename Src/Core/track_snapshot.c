#include "Core/track_snapshot.h"

#include <string.h>

#include "Audio/drum_synth.h"
#include "Core/brick6_braids_runtime.h"
#include "Core/brick6_deluge_runtime.h"
#include "Core/brick6_looper_runtime.h"
#include "Core/brick6_sampler_runtime.h"
#include "Core/brick6_stack_runtime.h"
#include "Core/brick6_wave_runtime.h"
#include "Core/track_runtime.h"
#include "Core/track_state.h"
#include "Keyboard/keyboard_engine.h"
#include "Mod/mod_matrix.h"
#include "Param/param_registry.h"
#include "Seq/seq_edit.h"
#include "Seq/seq_runtime_control.h"
#include "UI/ui_active_track_sync.h"

#define TRACK_SNAPSHOT_LOCK_NONE 0xFFFFU

typedef struct
{
    const uint8_t *family;
    const uint8_t *type;
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

    uint8_t master = track;
    if (track_runtime_get_voice_group_effective_master(track, &master) != 0U)
    {
        uint8_t members[SEQ_TRACK_COUNT];
        uint8_t member_count = 0U;
        if (track_runtime_collect_voice_group_members(master,
                                                      members,
                                                      SEQ_TRACK_COUNT,
                                                      &member_count) != 0U)
        {
            for (uint8_t i = 0U; i < member_count; ++i)
            {
                track_snapshot_add_restore_track(members[i], tracks, track_count);
            }
        }
    }

    for (uint8_t member = (uint8_t)(track + 1U); member < SEQ_TRACK_COUNT; ++member)
    {
        if (track_state_get_voice_group_role(member) != TRACK_VOICE_GROUP_ROLE_SLAVE)
        {
            break;
        }
        track_snapshot_add_restore_track(member, tracks, track_count);
    }

    if ((include_preceding_group != 0U) && (track > 0U))
    {
        const uint8_t previous = (uint8_t)(track - 1U);
        if (track_runtime_get_voice_group_effective_master(previous, &master) != 0U)
        {
            uint8_t members[SEQ_TRACK_COUNT];
            uint8_t member_count = 0U;
            if (track_runtime_collect_voice_group_members(master,
                                                          members,
                                                          SEQ_TRACK_COUNT,
                                                          &member_count) != 0U)
            {
                for (uint8_t i = 0U; i < member_count; ++i)
                {
                    track_snapshot_add_restore_track(members[i], tracks, track_count);
                }
            }
        }
    }
}

static void track_snapshot_runtime_quiesce_engine(uint8_t track)
{
    if (track >= SEQ_TRACK_COUNT)
    {
        return;
    }

    keyboard_engine_all_notes_off_for_track(track);
    brick6_sampler_runtime_reset_track(track);
    brick6_looper_runtime_stop_playback(track);
    brick6_looper_runtime_prepare_replace(track);
    brick6_braids_runtime_all_notes_off(track);
    brick6_braids_runtime_reset_instance(track);
    (void)brick6_stack_runtime_submit_all_notes_off(track);
    (void)brick6_stack_runtime_submit_reset_instance(track);
    brick6_wave_runtime_all_notes_off(track);
    brick6_wave_runtime_reset_instance(track);
    brick6_deluge_runtime_all_notes_off(track);
    brick6_deluge_runtime_reset_instance(track);
    drum_synth_all_notes_off_for_instance(track);
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

    if (ui_apply_track_config_bulk_mutation(ctx->family,
                                            ctx->type,
                                            ctx->midi_channel,
                                            ctx->midi_source) == false)
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

    memset(&out_snapshot->seq_track, 0, sizeof(out_snapshot->seq_track));
    out_snapshot->seq_track.length_steps = seq_model_get_track_length((seq_track_id_t)track);
    out_snapshot->seq_track.ui_page = seq_model_get_track_page((seq_track_id_t)track);

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_step_t *const dst_step = &out_snapshot->seq_track.steps[step];
        dst_step->lock_head = TRACK_SNAPSHOT_LOCK_NONE;
        dst_step->trig = seq_model_get_trig((seq_track_id_t)track, step);
        dst_step->roll = seq_model_get_step_roll((seq_track_id_t)track, step);

        uint8_t lock_count = 0U;
        if (seq_model_step_plock_collect((seq_track_id_t)track,
                                         step,
                                         out_snapshot->step_locks[step].locks,
                                         SEQ_STEP_MAX_LOCKS,
                                         &lock_count) == 0U)
        {
            return 0U;
        }
        out_snapshot->step_locks[step].count = lock_count;
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

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig((seq_track_id_t)track, step, snapshot->seq_track.steps[step].trig);
        seq_model_set_step_roll((seq_track_id_t)track, step, snapshot->seq_track.steps[step].roll);
        seq_model_step_plock_clear((seq_track_id_t)track, step);

        const uint8_t lock_count = snapshot->step_locks[step].count;
        for (uint8_t lock = 0U; lock < lock_count; ++lock)
        {
            const seq_plock_entry_t *const entry = &snapshot->step_locks[step].locks[lock];
            (void)seq_model_step_plock_upsert((seq_track_id_t)track,
                                              step,
                                              entry->set_id,
                                              entry->param_slot,
                                              entry->value16,
                                              entry->flags);
        }
    }

    seq_model_set_track_length((seq_track_id_t)track, snapshot->seq_track.length_steps);
    seq_model_set_track_page((seq_track_id_t)track, snapshot->seq_track.ui_page);
    seq_runtime_set_track_div((seq_track_id_t)track, snapshot->seq_div);
    seq_runtime_set_track_quant((seq_track_id_t)track, snapshot->seq_quant);
    seq_runtime_set_track_swing((seq_track_id_t)track, snapshot->seq_swing);
    (void)seq_runtime_set_playhead_step((seq_track_id_t)track, 0U);
}

static void track_snapshot_reapply_track_params(uint8_t track)
{
    track_runtime_refresh_track(track);
    mod_matrix_rebuild_route_cache_track(track);

    param_registry_batch_begin();
    for (uint16_t raw_id = 0U; raw_id < (uint16_t)PARAM_COUNT; ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
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

static uint8_t track_snapshot_apply_voice_group(uint8_t target_track, const track_snapshot_t *snapshot)
{
    uint8_t roles[UI_TRACK_COUNT];

    if ((target_track >= UI_TRACK_COUNT) || (snapshot == 0))
    {
        return 0U;
    }

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        roles[track] = (uint8_t)track_state_get_voice_group_role(track);
    }

    track_voice_group_role_t target_role = (track_voice_group_role_t)snapshot->voice_group_role;
    if ((uint8_t)target_role >= (uint8_t)TRACK_VOICE_GROUP_ROLE_COUNT)
    {
        target_role = TRACK_VOICE_GROUP_ROLE_SOLO;
    }
    if (target_role == TRACK_VOICE_GROUP_ROLE_SLAVE)
    {
        if ((target_track == 0U)
                || ((roles[(uint8_t)(target_track - 1U)] != (uint8_t)TRACK_VOICE_GROUP_ROLE_MASTER)
                    && (roles[(uint8_t)(target_track - 1U)] != (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)))
        {
            target_role = TRACK_VOICE_GROUP_ROLE_SOLO;
        }
    }

    roles[target_track] = (uint8_t)target_role;
    if (target_role == TRACK_VOICE_GROUP_ROLE_SOLO)
    {
        for (uint8_t track = (uint8_t)(target_track + 1U); track < UI_TRACK_COUNT; ++track)
        {
            if (roles[track] != (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE)
            {
                break;
            }
            roles[track] = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;
        }
    }

    if (track_state_apply_voice_group_roles_bulk(roles) == false)
    {
        return 0U;
    }

    if (track_state_get_voice_group_role(target_track) == TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        (void)track_state_set_voice_group_spread(target_track, snapshot->voice_group_spread);
        (void)track_state_set_voice_group_spread_keytrack(target_track, snapshot->voice_group_spread_keytrack);
        (void)track_state_set_voice_group_link(target_track, snapshot->voice_group_link);
        (void)param_registry_commit_voice_group_seq_link(target_track, snapshot->voice_group_seq_link);
    }
    else
    {
        (void)track_state_set_voice_group_spread(target_track, 0.0f);
        (void)track_state_set_voice_group_spread_keytrack(target_track, 0U);
        (void)track_state_set_voice_group_link(target_track, 0U);
        (void)param_registry_commit_voice_group_seq_link(target_track, 0U);
    }

    return 1U;
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
    out_snapshot->midi_channel = ui_get_track_midi_channel(track);
    out_snapshot->midi_source = ui_get_track_midi_source(track);
    out_snapshot->voice_group_role = (uint8_t)track_state_get_voice_group_role(track);
    out_snapshot->voice_group_spread = track_state_get_voice_group_spread(track);
    out_snapshot->voice_group_spread_keytrack = track_state_get_voice_group_spread_keytrack(track);
    out_snapshot->voice_group_link = track_state_get_voice_group_link(track);
    out_snapshot->voice_group_seq_link = track_state_get_voice_group_seq_link(track);
    memcpy(&out_snapshot->sound, sound, sizeof(out_snapshot->sound));
    memcpy(&out_snapshot->tone, tone, sizeof(out_snapshot->tone));

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
    out_snapshot->config.type = UI_TRACK_TYPE_AUDIO;
    out_snapshot->midi_channel = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
    out_snapshot->midi_source = UI_TRACK_MIDI_SRC_ALL;
    out_snapshot->voice_group_role = (uint8_t)TRACK_VOICE_GROUP_ROLE_SOLO;

    track_sound_state_make_default(&out_snapshot->sound);
    track_tone_sound_state_make_default(&out_snapshot->tone);

    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        out_snapshot->seq_track.steps[step].lock_head = TRACK_SNAPSHOT_LOCK_NONE;
    }
    out_snapshot->seq_track.length_steps = SEQ_DEFAULT_LENGTH_STEPS;
    out_snapshot->seq_track.ui_page = 0U;
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
    if ((track_snapshot_track_is_valid(target_track) == 0U)
            || (snapshot == 0)
            || (snapshot->valid == 0U))
    {
        return 0U;
    }

    uint8_t family[UI_TRACK_COUNT];
    uint8_t type[UI_TRACK_COUNT];
    uint8_t midi_channel[UI_TRACK_COUNT];
    uint8_t midi_source[UI_TRACK_COUNT];

    for (uint8_t track = 0U; track < UI_TRACK_COUNT; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        family[track] = (uint8_t)cfg.family;
        type[track] = (uint8_t)cfg.type;
        midi_channel[track] = ui_get_track_midi_channel(track);
        midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
    }

    if ((options != 0)
            && (options->clear_source_track != 0U)
            && (options->source_track < UI_TRACK_COUNT)
            && (options->source_track != target_track))
    {
        family[options->source_track] = (uint8_t)UI_TRACK_FAMILY_OFF;
        type[options->source_track] = (uint8_t)UI_TRACK_TYPE_AUDIO;
    }

    const ui_track_family_t target_family =
        ((options != 0) && (options->has_family_override != 0U))
            ? options->family_override
            : snapshot->config.family;

    family[target_track] = (uint8_t)target_family;
    type[target_track] = (uint8_t)snapshot->config.type;
    midi_channel[target_track] = snapshot->midi_channel;
    midi_source[target_track] = (uint8_t)snapshot->midi_source;

    seq_track_id_t restore_tracks[SEQ_TRACK_COUNT];
    uint8_t restore_track_count = 0U;
    track_snapshot_collect_restore_tracks(
        target_track,
        (snapshot->voice_group_role == (uint8_t)TRACK_VOICE_GROUP_ROLE_SLAVE) ? 1U : 0U,
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

    if (track_snapshot_apply_voice_group(target_track, snapshot) == 0U)
    {
        goto restore_done;
    }
    track_runtime_invalidate_all();
    track_runtime_refresh_all();

    track_snapshot_apply_sequence(target_track, snapshot);
    track_snapshot_reapply_track_params(target_track);
    if (track_state_get_voice_group_role(target_track) == TRACK_VOICE_GROUP_ROLE_MASTER)
    {
        (void)param_registry_apply_track_value(PARAM_CFG_GROUP_SPREAD,
                                               target_track,
                                               track_state_get_voice_group_spread(target_track));
    }

    ui_active_track_sync_full_after_reconfigure();
    apply_ok = 1U;

restore_done:
    seq_runtime_end_track_restore(restore_tracks, restore_track_count);
    return apply_ok;
}

uint8_t track_snapshot_apply(uint8_t target_track, const track_snapshot_t *snapshot)
{
    return track_snapshot_apply_ex(target_track, snapshot, 0);
}
