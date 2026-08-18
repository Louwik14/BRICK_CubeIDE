#include "Core/track_snapshot.h"

#include <string.h>

#include "Core/track_runtime.h"
#include "Core/live_clock.h"
#include "Core/live_parameter_audio_queue.h"
#include "Core/live_parameter_migration.h"
#include "Core/track_input_ownership.h"
#include "Core/track_state.h"
#include "Keyboard/keyboard_engine.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "Param/param_registry.h"
#include "Seq/seq_edit.h"
#include "Core/entity_topology.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_step_snapshot.h"
#include "Seq/seq_runtime_control.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "UI/ui_active_track_sync.h"
#include "UI/ui_param.h"
#include "UI/ui_track_catalog.h"

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
    return (track < BRICK_ENTITY_CAPACITY) ? 1U : 0U;
}

static void track_snapshot_add_restore_track(uint8_t track,
                                             seq_track_id_t *tracks,
                                             uint8_t *track_count)
{
    if ((track >= SEQ_LANE_CAPACITY) || (tracks == 0) || (track_count == 0))
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
    if (*track_count < SEQ_LANE_CAPACITY)
    {
        tracks[*track_count] = (seq_track_id_t)track;
        (*track_count)++;
    }
}

static void track_snapshot_collect_restore_tracks(uint8_t track,
                                                  seq_track_id_t *tracks,
                                                  uint8_t *track_count)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    track_snapshot_add_restore_track(track, tracks, track_count);
}

static void track_snapshot_runtime_quiesce_engine(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    note_fx_pipeline_reset_runtime_overrides(track);
    keyboard_engine_all_notes_off_for_track(track);
    mod_lfo_v1_all_notes_off(track);
    param_registry_clear_track_runtime_state(track);
}

static void track_snapshot_runtime_neutralize_note_state(uint8_t track)
{
    if (track >= SEQ_LANE_CAPACITY)
    {
        return;
    }

    const seq_track_id_t transition_track = track;
    if (seq_play_scheduler_transition_tracks(
            &transition_track, 1U,
            SEQ_PLAY_TRANSITION_MODEL_RECONFIGURE) == 0U)
        return;
    keyboard_engine_all_notes_off_for_track(track);
    mod_lfo_v1_all_notes_off(track);
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

    if (ui_apply_entity_config_bulk_mutation_with_inputs(ctx->family,
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
    if ((track >= SEQ_LANE_CAPACITY) || (out_snapshot == 0))
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
        for (uint8_t voice = 0U; voice < SEQ_PLAY_MAX_CAPACITY; ++voice)
        {
            for (uint8_t field = 0U; field < SEQ_STEP_PLAY_FIELD_COUNT; ++field)
            {
                int16_t value = 0;
                if (seq_model_play_get((seq_track_id_t)track,
                                            step,
                                            voice,
                                            (seq_step_play_field_t)field,
                                            &value) != 0U)
                {
                    if (seq_play_snapshot_set(&saved->play[step],
                                          voice,
                                          (seq_step_play_field_t)field,
                                          value) == 0U) return 0U;
                }
            }
        }

        const uint8_t lock_count = seq_model_step_param_plock_count((seq_track_id_t)track, step);
        if (lock_count > SEQ_STEP_MAX_LOCKS) return 0U;
        saved->step_locks[step].count = lock_count;
        for (uint8_t lock = 0U; lock < lock_count; ++lock)
        {
            if (seq_model_step_param_plock_get_at((seq_track_id_t)track,
                                                  step,
                                                  lock,
                                                  &saved->step_locks[step].locks[lock]) == 0U) return 0U;
        }
    }

    (void)seq_runtime_get_track_div((seq_track_id_t)track, &out_snapshot->seq_div);
    (void)seq_runtime_get_track_quant((seq_track_id_t)track, &out_snapshot->seq_quant);
    (void)seq_runtime_get_track_swing((seq_track_id_t)track, &out_snapshot->seq_swing);
    return 1U;
}

static uint8_t track_snapshot_build_step_snapshot(const track_snapshot_sequence_t *saved,
                                                  seq_step_id_t step,
                                                  seq_step_snapshot_t *out_step)
{
    if ((saved == 0) || (out_step == 0) || (step >= (seq_step_id_t)SEQ_MAX_STEPS))
    {
        return 0U;
    }

    const seq_step_t *const source = &saved->track.steps[step];
    const track_snapshot_step_locks_t *const source_locks = &saved->step_locks[step];
    if (source_locks->count > SEQ_STEP_SNAPSHOT_MAX_LOCKS)
    {
        return 0U;
    }

    memset(out_step, 0, sizeof(*out_step));
    out_step->valid = 1U;
    out_step->trig = source->trig;
    out_step->roll = source->roll;
    out_step->lock_count = source_locks->count;
    out_step->play = saved->play[step];
    for (uint8_t i = 0U; i < source_locks->count; ++i)
    {
        const seq_plock_entry_t *const lock = &source_locks->locks[i];
        out_step->locks[i] = (seq_step_snapshot_plock_t){
            .set_id = lock->set_id,
            .param_slot = lock->param_slot,
            .value16 = lock->value16,
            .flags = lock->flags
        };
    }
    return 1U;
}

static uint8_t track_snapshot_validate_sequence(uint8_t track,
                                                const track_snapshot_t *snapshot,
                                                uint8_t can_store_play,
                                                uint8_t can_store_params,
                                                uint8_t runtime_type)
{
    if ((track >= SEQ_LANE_CAPACITY) || (snapshot == 0))
    {
        return 0U;
    }
    if (seq_edit_track_sequence_is_locked((seq_track_id_t)track) != 0U)
    {
        return 0U;
    }

    const track_snapshot_sequence_t *const saved = &snapshot->sequence;
    uint16_t incoming_lock_count = 0U;
    seq_step_snapshot_t step_snapshot;
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        if ((track_snapshot_build_step_snapshot(saved, step, &step_snapshot) == 0U)
                || (seq_step_snapshot_validate_for_target(can_store_play,
                                                          can_store_params,
                                                          runtime_type,
                                                          &step_snapshot) == 0U))
        {
            return 0U;
        }
        incoming_lock_count = (uint16_t)(incoming_lock_count + step_snapshot.lock_count);
    }
    if (incoming_lock_count > seq_model_get_track_plock_capacity((seq_track_id_t)track))
    {
        return 0U;
    }

    return 1U;
}

static uint8_t track_snapshot_apply_sequence(uint8_t track, const track_snapshot_t *snapshot)
{
    if ((track >= SEQ_LANE_CAPACITY) || (snapshot == 0))
    {
        return 0U;
    }

    const track_snapshot_sequence_t *const saved = &snapshot->sequence;
    seq_step_snapshot_t step_snapshot;
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        seq_model_set_trig((seq_track_id_t)track, step, 0U);
        seq_model_step_plock_clear((seq_track_id_t)track, step);
        seq_model_play_clear_step((seq_track_id_t)track, step);
    }
    for (seq_step_id_t step = 0U; step < (seq_step_id_t)SEQ_MAX_STEPS; ++step)
    {
        if ((track_snapshot_build_step_snapshot(saved, step, &step_snapshot) == 0U)
                || (seq_step_snapshot_apply((seq_track_id_t)track,
                                            step,
                                            &step_snapshot) == 0U))
        {
            return 0U;
        }
    }
    seq_model_set_track_length((seq_track_id_t)track, saved->track.length_steps);
    seq_model_set_track_page((seq_track_id_t)track, saved->track.ui_page);
    seq_runtime_restore_track_div((seq_track_id_t)track, snapshot->seq_div);
    seq_runtime_set_track_quant((seq_track_id_t)track, snapshot->seq_quant);
    seq_runtime_set_track_swing((seq_track_id_t)track, snapshot->seq_swing);
    return 1U;
}

static uint8_t track_snapshot_reapply_track_params(uint8_t track,
                                                   const track_snapshot_t *snapshot)
{
    if ((snapshot == 0) || (track >= SEQ_LANE_CAPACITY))
    {
        return 0U;
    }

    track_runtime_refresh_track(track);
    mod_matrix_publish_control_snapshot_track(track);

    live_parameter_audio_bulk_t bulk = {
        .capture_tick = live_clock_capture_tick(),
        .source = LIVE_PARAMETER_EVENT_SOURCE_BULK,
        .count = 0U
    };
    for (uint8_t phase = 0U; phase < 5U; ++phase)
    {
        for (uint8_t i = 0U; i < snapshot->audio_owned_count; ++i)
        {
            const track_snapshot_audio_owned_item_t *const item =
                &snapshot->audio_owned[i];
            if (item->parameter_id >= PARAM_COUNT)
                continue;
            const param_id_t id = (param_id_t)item->parameter_id;
            uint8_t selected = 0U;
            if (phase < 4U)
            {
                selected = (uint8_t)(param_registry_get_audio_fx_param(phase) == id);
            }
            else
            {
                selected = (uint8_t)(param_registry_is_audio_fx_param(id) == 0U);
            }
            if ((selected == 0U)
                    || (live_parameter_is_audio_owned(id) == 0U)
                    || (track_runtime_get_effective_param_status(track, id)
                        != TRACK_RUNTIME_PARAM_ALLOWED))
                continue;
            if (bulk.count >= LIVE_PARAMETER_AUDIO_BULK_MAX_ITEMS)
                return 0U;
            live_parameter_audio_bulk_item_t *const target = &bulk.item[bulk.count++];
            target->parameter_id = item->parameter_id;
            target->scope = LIVE_PARAMETER_EVENT_SCOPE_TRACK;
            target->track = track;
            target->slot = LIVE_PARAMETER_EVENT_INVALID_INDEX;
            target->reserved = 0U;
            target->flags = (uint16_t)(LIVE_PARAMETER_EVENT_FLAG_SET_TARGET
                                       | LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS);
            target->value = live_parameter_event_encode_float(item->value);
        }
    }

    if ((bulk.count != 0U)
            && (live_parameter_audio_queue_submit_bulk(&bulk) == false))
    {
        return 0U;
    }
    for (uint8_t i = 0U; i < bulk.count; ++i)
    {
        const live_parameter_audio_bulk_item_t *const item = &bulk.item[i];
        (void)ui_param_accept_audio_owned_command((param_id_t)item->parameter_id,
                                                  item->scope,
                                                  item->track,
                                                  live_parameter_event_decode_float(item->value));
    }

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
        if ((id == PARAM_LOOPER_PLAY)
                || (live_parameter_is_audio_owned(id) != 0U))
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
    return 1U;
}

static void track_snapshot_capture_audio_owned_values(uint8_t track,
                                                       track_snapshot_t *snapshot)
{
    snapshot->audio_owned_count = 0U;
    for (uint16_t raw_id = 0U;
         (raw_id < (uint16_t)PARAM_COUNT)
             && (snapshot->audio_owned_count < TRACK_SNAPSHOT_AUDIO_OWNED_MAX_ITEMS);
         ++raw_id)
    {
        const param_id_t id = (param_id_t)raw_id;
        if (live_parameter_is_audio_owned(id) == 0U)
        {
            continue;
        }
        float value = param_registry[id].default_value;
        (void)param_registry_get_track_value(id, track, &value);
        snapshot->audio_owned[snapshot->audio_owned_count++] =
            (track_snapshot_audio_owned_item_t){
                .parameter_id = raw_id,
                .reserved = 0U,
                .value = value
            };
    }
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
        float voices = 1.0f;
        (void)param_registry_get_track_value(
            PARAM_CFG_POLY_VOICES, track, &voices);
        out_snapshot->poly_voice_count = (uint8_t)voices;
        (void)param_registry_get_track_value(
            PARAM_CFG_POLY_SPREAD, track, &out_snapshot->poly_spread);
    }
    else if ((out_snapshot->config.family == UI_TRACK_FAMILY_SAMPLER)
            && (out_snapshot->config.type == UI_TRACK_TYPE_MULTI))
    {
        float voices = 1.0f;
        (void)param_registry_get_track_value(
            PARAM_CFG_POLY_VOICES, track, &voices);
        out_snapshot->poly_voice_count = (uint8_t)voices;
        (void)param_registry_get_track_value(
            PARAM_CFG_POLY_SPREAD, track, &out_snapshot->poly_spread);
    }
    memcpy(&out_snapshot->sound, sound, sizeof(out_snapshot->sound));
    memcpy(&out_snapshot->tone, tone, sizeof(out_snapshot->tone));
    track_snapshot_capture_audio_owned_values(track, out_snapshot);
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
    out_snapshot->external_input = (uint8_t)(track % ENTITY_TOPOLOGY_PHYSICAL_INPUT_COUNT);
    out_snapshot->midi_channel = (uint8_t)((track < 16U) ? (track + 1U) : 16U);
    out_snapshot->midi_source = UI_TRACK_MIDI_SRC_ALL;
    out_snapshot->poly_voice_count = 1U;
    out_snapshot->poly_spread = 0.0f;
    track_sound_state_make_default(&out_snapshot->sound);
    track_tone_sound_state_make_default(&out_snapshot->tone);
    track_snapshot_capture_audio_owned_values(track, out_snapshot);
    for (uint8_t i = 0U; i < out_snapshot->audio_owned_count; ++i)
    {
        out_snapshot->audio_owned[i].value =
            param_registry[out_snapshot->audio_owned[i].parameter_id].default_value;
    }
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

    uint8_t family[BRICK_ENTITY_CAPACITY];
    uint8_t type[BRICK_ENTITY_CAPACITY];
    uint8_t external_input[UI_TRACK_COUNT];
    uint8_t midi_channel[BRICK_ENTITY_CAPACITY];
    uint8_t midi_source[BRICK_ENTITY_CAPACITY];

    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        const ui_track_config_t cfg = ui_get_track_config(track);
        family[track] = (uint8_t)cfg.family;
        type[track] = (uint8_t)cfg.type;
        if (track < UI_TRACK_COUNT)
        {
            external_input[track] = ui_get_track_external_input(track);
        }
        midi_channel[track] = ui_get_track_midi_channel(track);
        midi_source[track] = (uint8_t)ui_get_track_midi_source(track);
    }

    if ((options != 0)
            && (options->clear_source_track != 0U)
            && (options->source_track < BRICK_ENTITY_CAPACITY)
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
        const uint8_t maximum = (uint8_t)param_registry[PARAM_CFG_POLY_VOICES].max;
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
        const uint8_t maximum = (uint8_t)param_registry[PARAM_CFG_POLY_VOICES].max;
        if (applied_voice_count < 1U) applied_voice_count = 1U;
        if (applied_voice_count > maximum)
        {
            applied_voice_count = maximum;
            g_track_snapshot_voice_limited = 1U;
            g_track_snapshot_voice_max = maximum;
        }
    }

    family[target_track] = (uint8_t)target_family;
    type[target_track] = (uint8_t)snapshot->config.type;
    if (target_track < UI_TRACK_COUNT)
    {
        external_input[target_track] = snapshot->external_input;
    }
    midi_channel[target_track] = snapshot->midi_channel;
    midi_source[target_track] = (uint8_t)snapshot->midi_source;

    ui_track_config_t ownership_configs[BRICK_ENTITY_CAPACITY];
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        ownership_configs[track] = track_state_get_config(track);
    }
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        if ((family[track] >= (uint8_t)UI_TRACK_FAMILY_COUNT)
                || (type[track] >= (uint8_t)UI_TRACK_TYPE_COUNT)
                || ((family[track] == (uint8_t)UI_TRACK_FAMILY_OFF)
                    && (type[track] != (uint8_t)UI_TRACK_TYPE_NONE)))
        {
            return 0U;
        }
        ownership_configs[track].family = (ui_track_family_t)family[track];
        ownership_configs[track].type = (ui_track_type_t)type[track];
    }
    const uint8_t prospective_group_active = (uint8_t)(
        ownership_configs[BRICK_ENTITY_GROUP_MASTER_ID].type == UI_TRACK_TYPE_GROUP);
    for (uint8_t track = 0U; track < BRICK_ENTITY_CAPACITY; ++track)
    {
        entity_topology_descriptor_t entity;
        if ((entity_topology_resolve(prospective_group_active, track, &entity) == 0U)
                || (entity.active == 0U))
        {
            continue;
        }
        if ((ownership_configs[track].family != UI_TRACK_FAMILY_OFF)
                && (ui_track_catalog_type_is_available(
                        track,
                        ownership_configs[track].family,
                        ownership_configs[track].type,
                        ownership_configs) == false))
        {
            return 0U;
        }
    }
    if (track_input_ownership_validate_bulk(ownership_configs, external_input) == 0U)
    {
        return 0U;
    }

    const uint8_t group_active = (uint8_t)(type[BRICK_ENTITY_GROUP_MASTER_ID]
        == (uint8_t)UI_TRACK_TYPE_GROUP);
    entity_topology_descriptor_t target_entity;
    if ((entity_topology_resolve(group_active,
                                 (brick_entity_id_t)target_track,
                                 &target_entity) == 0U)
            || (target_entity.active == 0U)
            || (snapshot->audio_owned_count > TRACK_SNAPSHOT_AUDIO_OWNED_MAX_ITEMS)
            || (track_snapshot_validate_sequence(target_track,
                                                 snapshot,
                                                 seq_model_track_can_store_play((seq_track_id_t)target_track),
                                                 target_entity.active,
                                                 type[target_track]) == 0U))
    {
        return 0U;
    }

    seq_track_id_t restore_tracks[SEQ_LANE_CAPACITY];
    uint8_t restore_track_count = 0U;
    track_snapshot_collect_restore_tracks(
        target_track,
        restore_tracks,
        &restore_track_count);
    if ((options != 0)
            && (options->clear_source_track != 0U)
            && (options->source_track < BRICK_ENTITY_CAPACITY)
            && (options->source_track != target_track))
    {
        track_snapshot_collect_restore_tracks(options->source_track,
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
    if ((target_track < NOTE_FX_TRACK_COUNT)
            && (note_fx_pipeline_sync_track(target_track) == 0U))
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

    if (track_snapshot_apply_sequence(target_track, snapshot) == 0U)
    {
        goto restore_done;
    }
    if (track_snapshot_reapply_track_params(target_track, snapshot) == 0U)
    {
        goto restore_done;
    }
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
