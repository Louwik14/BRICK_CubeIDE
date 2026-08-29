#include "Core/control_music_output.h"

#include <stddef.h>
#include <string.h>

#include "Track/track_runtime.h"
#include "Core/brick6_sampler_multi_contract.h"
#include "IPC/control_audio_publication.h"
#include "IPC/control_music_publication.h"
#include "IPC/control_audio_command.h"
#include "Core/live_clock.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Platform/memory_layout.h"
#include "midi.h"

typedef struct
{
    uint32_t output_id;
    uint32_t causal_source_id;
    uint32_t generation;
    uint32_t age;
    uint8_t note;
    uint8_t velocity;
    uint8_t alive : 1;
    uint8_t multi : 1;
    uint8_t midi_channel : 4;
    uint8_t midi_dest_mask : 2;
} control_music_output_t;

_Static_assert(sizeof(control_music_output_t) == 20U,
               "terminal NOTE record must remain compact");

CONTROL_M4_SRAM2 static control_music_output_t
    g_control_music_outputs[BRICK_ENTITY_CAPACITY][CONTROL_MUSIC_OUTPUTS_PER_ENTITY];
static uint32_t g_control_music_output_age;
static control_music_output_death_observer_t
    g_control_music_output_death_observer[
        CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY];
CONTROL_M4_SRAM2 static control_music_output_t
    g_control_music_outputs_staged[BRICK_ENTITY_CAPACITY][CONTROL_MUSIC_OUTPUTS_PER_ENTITY];
static uint32_t g_control_music_output_age_staged;

#define CONTROL_MUSIC_WINDOW_MAX_FRAMES 64U
#define CONTROL_MUSIC_WINDOW_KIND_COUNT 3U
#define CONTROL_MUSIC_WINDOW_BUCKET_COUNT \
    (CONTROL_MUSIC_WINDOW_MAX_FRAMES * CONTROL_MUSIC_WINDOW_KIND_COUNT)
#define CONTROL_MUSIC_WINDOW_NONE UINT16_MAX

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST];
    uint16_t next[CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST];
    uint16_t head[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t tail[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t count;
} control_music_window_internal_t;

typedef struct
{
    control_music_action_t actions[CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY];
    uint16_t next[CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY];
    uint16_t head[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t tail[CONTROL_MUSIC_WINDOW_BUCKET_COUNT];
    uint16_t count;
} control_music_window_external_t;

SEQ_STATE_D2 static control_music_window_internal_t
    g_control_music_window_internal;
SEQ_STATE_D2 static control_music_window_external_t
    g_control_music_window_external;
static uint64_t g_control_music_window_first;
static uint64_t g_control_music_first_unpublished;
static uint16_t g_control_music_window_frames;
static uint16_t g_control_music_window_internal_limit;
static uint16_t g_control_music_window_external_limit;
static uint8_t g_control_music_window_active;
static uint8_t g_control_music_window_prepared;

static control_music_output_t (*control_music_output_ledger(void))[CONTROL_MUSIC_OUTPUTS_PER_ENTITY]
{
    return ((g_control_music_window_active != 0U)
            || (g_control_music_window_prepared != 0U))
        ? g_control_music_outputs_staged : g_control_music_outputs;
}

static void control_music_output_reset_window_buckets(void)
{
    for (uint16_t i = 0U; i < CONTROL_MUSIC_WINDOW_BUCKET_COUNT; ++i)
    {
        g_control_music_window_internal.head[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_internal.tail[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_external.head[i] = CONTROL_MUSIC_WINDOW_NONE;
        g_control_music_window_external.tail[i] = CONTROL_MUSIC_WINDOW_NONE;
    }
    g_control_music_window_internal.count = 0U;
    g_control_music_window_external.count = 0U;
}

uint8_t control_music_output_begin_window(uint64_t first_sample,
                                          uint16_t frames)
{
    if ((g_control_music_window_active != 0U)
            || (g_control_music_window_prepared != 0U) || (frames == 0U)
            || (frames > CONTROL_MUSIC_WINDOW_MAX_FRAMES))
        return 0U;
    if (first_sample < g_control_music_first_unpublished)
        first_sample = g_control_music_first_unpublished;
    g_control_music_window_first = first_sample;
    g_control_music_window_frames = frames;
    g_control_music_window_internal_limit =
        control_music_publication_free();
    if (g_control_music_window_internal_limit
            > CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST)
        g_control_music_window_internal_limit =
            CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST;
    g_control_music_window_external_limit =
        control_music_publication_free();
    if (g_control_music_window_external_limit
            > CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY)
        g_control_music_window_external_limit =
            CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY;
    control_music_output_reset_window_buckets();
    memcpy(g_control_music_outputs_staged, g_control_music_outputs,
           sizeof(g_control_music_outputs));
    g_control_music_output_age_staged = g_control_music_output_age;
    g_control_music_window_active = 1U;
    return 1U;
}

uint64_t control_music_output_first_unpublished_sample(uint64_t audio_sample)
{
    return (g_control_music_first_unpublished > audio_sample)
        ? g_control_music_first_unpublished : audio_sample;
}

void control_music_output_abort_window(void)
{
    if ((g_control_music_window_active == 0U)
            && (g_control_music_window_prepared == 0U))
        return;
    control_music_output_reset_window_buckets();
    g_control_music_window_active = 0U;
    g_control_music_window_prepared = 0U;
}

static uint8_t control_music_output_stage(const control_music_action_t *action)
{
    if ((action == NULL)
            || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (control_music_action_kind(action)
                > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U) || (action->note >= 128U)
            || (action->velocity >= 128U)
            || (action->due_sample < g_control_music_window_first)
            || (action->due_sample
                >= (g_control_music_window_first + g_control_music_window_frames)))
        return 0U;
    const uint16_t offset = (uint16_t)(
        action->due_sample - g_control_music_window_first);
    const uint16_t bucket = (uint16_t)(
        (offset * CONTROL_MUSIC_WINDOW_KIND_COUNT)
        + control_music_action_kind(action));
    const uint8_t external = control_music_action_is_external(action);
    control_music_action_t *actions;
    uint16_t *next;
    uint16_t *head;
    uint16_t *tail;
    uint16_t *count;
    uint16_t capacity;
    if (external != 0U)
    {
        actions = g_control_music_window_external.actions;
        next = g_control_music_window_external.next;
        head = g_control_music_window_external.head;
        tail = g_control_music_window_external.tail;
        count = &g_control_music_window_external.count;
        capacity = g_control_music_window_external_limit;
    }
    else
    {
        actions = g_control_music_window_internal.actions;
        next = g_control_music_window_internal.next;
        head = g_control_music_window_internal.head;
        tail = g_control_music_window_internal.tail;
        count = &g_control_music_window_internal.count;
        capacity = g_control_music_window_internal_limit;
    }
    if (*count >= capacity)
        return 0U;
    const uint16_t index = (*count)++;
    actions[index] = *action;
    next[index] = CONTROL_MUSIC_WINDOW_NONE;
    if (head[bucket] == CONTROL_MUSIC_WINDOW_NONE)
        head[bucket] = index;
    else
        next[tail[bucket]] = index;
    tail[bucket] = index;
    return 1U;
}

static uint8_t control_music_output_publish_batch(
    const control_music_action_t *actions, uint16_t count);

static uint8_t control_music_output_publish(const control_music_action_t *action)
{
    return control_music_output_publish_batch(action, 1U);
}

static uint8_t control_music_output_publish_batch(
    const control_music_action_t *actions, uint16_t count)
{
    if (g_control_music_window_active == 0U)
    {
        if ((actions == NULL) || (count == 0U)
                || (count > (CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U)))
            return 0U;
        uint64_t first_sample = actions[0].due_sample;
        for (uint16_t i = 1U; i < count; ++i)
            if (actions[i].due_sample > first_sample)
                first_sample = actions[i].due_sample;
        first_sample = control_music_output_first_unpublished_sample(first_sample);
        if (control_music_output_begin_window(first_sample, 1U) == 0U)
            return 0U;
        control_music_action_t normalized[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
        for (uint16_t i = 0U; i < count; ++i)
        {
            normalized[i] = actions[i];
            normalized[i].due_sample = first_sample;
        }
        if (control_music_output_publish_batch(normalized, count) == 0U)
        {
            control_music_output_abort_window();
            return 0U;
        }
        if (control_music_output_commit_window() == 0U)
        {
            control_music_output_abort_window();
            return 0U;
        }
        if (control_music_output_finalize_window() == 0U)
            return 0U;
        return 1U;
    }
    if ((actions == NULL) || (count == 0U))
        return 0U;
    const uint8_t external = control_music_action_is_external(&actions[0]);
    const uint16_t staged_count = (external != 0U)
        ? g_control_music_window_external.count
        : g_control_music_window_internal.count;
    const uint16_t capacity = (external != 0U)
        ? g_control_music_window_external_limit
        : g_control_music_window_internal_limit;
    if ((uint32_t)staged_count + count > capacity)
        return 0U;
    for (uint16_t i = 0U; i < count; ++i)
    {
        const uint8_t action_external = control_music_action_is_external(&actions[i]);
        if ((action_external != external)
                || (actions[i].due_sample < g_control_music_window_first)
                || (actions[i].due_sample
                    >= (g_control_music_window_first
                        + g_control_music_window_frames)))
            return 0U;
    }
    for (uint16_t i = 0U; i < count; ++i)
        if (control_music_output_stage(&actions[i]) == 0U)
            return 0U;
    return 1U;
}

uint8_t control_music_output_commit_window(void)
{
    if (g_control_music_window_active == 0U)
        return 0U;
    if (control_music_publication_free()
            < (uint16_t)(g_control_music_window_internal.count
                + g_control_music_window_external.count))
        return 0U;

    uint8_t accepted = 1U;
    if ((g_control_music_window_internal.count != 0U)
            || (g_control_music_window_external.count != 0U))
        accepted = control_music_publication_publish_merged_window(
            g_control_music_window_internal.actions,
            g_control_music_window_internal.next,
            g_control_music_window_internal.head,
            g_control_music_window_internal.count,
            g_control_music_window_external.actions,
            g_control_music_window_external.next,
            g_control_music_window_external.head,
            g_control_music_window_external.count,
            (uint16_t)(g_control_music_window_frames
                * CONTROL_MUSIC_WINDOW_KIND_COUNT));
    if (accepted == 0U)
        return 0U;
    g_control_music_window_active = 0U;
    g_control_music_window_prepared = 1U;
    return accepted;
}

uint8_t control_music_output_finalize_window(void)
{
    if (g_control_music_window_prepared == 0U)
        return 0U;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const old =
                &g_control_music_outputs[entity_id][i];
            uint8_t survives = 0U;
            if (old->alive != 0U)
                for (uint8_t j = 0U; j < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++j)
                    if ((g_control_music_outputs_staged[entity_id][j].alive != 0U)
                            && (g_control_music_outputs_staged[entity_id][j].output_id
                                == old->output_id))
                        survives = 1U;
            if ((old->alive != 0U) && (survives == 0U))
                for (uint8_t observer = 0U;
                     observer < CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY;
                     ++observer)
                    if (g_control_music_output_death_observer[observer] != NULL)
                        g_control_music_output_death_observer[observer](
                            entity_id, old->output_id);
        }
    memcpy(g_control_music_outputs, g_control_music_outputs_staged,
           sizeof(g_control_music_outputs));
    g_control_music_output_age = g_control_music_output_age_staged;
    g_control_music_first_unpublished =
        g_control_music_window_first + g_control_music_window_frames;
    g_control_music_window_prepared = 0U;
    return 1U;
}

static void control_music_output_mark_dead(brick_entity_id_t entity_id,
                                           uint8_t index)
{
    control_music_output_t *const output =
        &control_music_output_ledger()[entity_id][index];
    if (output->alive == 0U)
        return;
    const uint32_t output_id = output->output_id;
    output->alive = 0U;
    if ((g_control_music_window_active != 0U)
            || (g_control_music_window_prepared != 0U))
        return;
    for (uint8_t i = 0U;
         i < CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY; ++i)
        if (g_control_music_output_death_observer[i] != NULL)
            g_control_music_output_death_observer[i](entity_id, output_id);
}

uint8_t control_music_output_register_death_observer(
    control_music_output_death_observer_t observer)
{
    if (observer == NULL)
        return 0U;
    for (uint8_t i = 0U;
         i < CONTROL_MUSIC_OUTPUT_DEATH_OBSERVER_CAPACITY; ++i)
    {
        if (g_control_music_output_death_observer[i] == observer)
            return 1U;
        if (g_control_music_output_death_observer[i] == NULL)
        {
            g_control_music_output_death_observer[i] = observer;
            return 1U;
        }
    }
    return 0U;
}

static uint8_t control_music_output_limit(brick_entity_id_t entity_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(entity_id);
    if ((ctx == NULL)
            || ((ctx->family != (uint8_t)TRACK_RUNTIME_FAMILY_SYNTH)
                && !((ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
                    && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI))))
        return 1U;

    float configured = 1.0f;
    (void)param_registry_control_value_get(entity_id, PARAM_CFG_POLY_VOICES,
                                            &configured);
    uint8_t limit = (configured >= 1.0f) ? (uint8_t)configured : 1U;
    return (limit > CONTROL_MUSIC_OUTPUTS_PER_ENTITY)
        ? CONTROL_MUSIC_OUTPUTS_PER_ENTITY : limit;
}

static uint8_t control_music_output_is_multi(brick_entity_id_t entity_id)
{
    const track_runtime_ctx_t *const ctx = track_runtime_get_ctx(entity_id);
    return ((ctx != NULL)
            && (ctx->family == (uint8_t)TRACK_RUNTIME_FAMILY_SAMPLER)
            && (ctx->type == (uint8_t)TRACK_RUNTIME_TYPE_MULTI)) ? 1U : 0U;
}

static uint8_t control_music_output_multi_live_count(void)
{
    uint8_t count = 0U;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            count += ((control_music_output_ledger()[entity_id][i].alive != 0U)
                && (control_music_output_ledger()[entity_id][i].multi != 0U))
                ? 1U : 0U;
    }
    return count;
}

static uint8_t control_music_output_ref_is_excluded(
    brick_entity_id_t entity_id, uint8_t index,
    const brick_entity_id_t *excluded_entities,
    const uint8_t *excluded_indices, uint8_t excluded_count)
{
    for (uint8_t i = 0U; i < excluded_count; ++i)
        if ((excluded_entities[i] == entity_id)
                && (excluded_indices[i] == index))
            return 1U;
    return 0U;
}

static uint8_t control_music_output_find_oldest_multi(
    const brick_entity_id_t *excluded_entities,
    const uint8_t *excluded_indices, uint8_t excluded_count,
    brick_entity_id_t *out_entity, uint8_t *out_index)
{
    uint8_t found = 0U;
    uint32_t oldest_age = UINT32_MAX;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        for (uint8_t index = 0U;
             index < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++index)
        {
            const control_music_output_t *const output =
                &control_music_output_ledger()[entity_id][index];
            if ((output->alive == 0U) || (output->multi == 0U)
                    || (control_music_output_ref_is_excluded(
                        entity_id, index, excluded_entities, excluded_indices,
                        excluded_count) != 0U))
                continue;
            if ((found == 0U) || (output->age < oldest_age))
            {
                found = 1U;
                oldest_age = output->age;
                *out_entity = entity_id;
                *out_index = index;
            }
        }
    }
    return found;
}

static int8_t control_music_output_find(brick_entity_id_t entity_id,
                                        uint32_t output_id)
{
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        if ((control_music_output_ledger()[entity_id][i].alive != 0U)
                && (control_music_output_ledger()[entity_id][i].output_id == output_id))
            return (int8_t)i;
    return -1;
}

static uint8_t control_music_output_live_count(brick_entity_id_t entity_id)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        count += (control_music_output_ledger()[entity_id][i].alive != 0U) ? 1U : 0U;
    return count;
}

static int8_t control_music_output_find_free(brick_entity_id_t entity_id)
{
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        if (control_music_output_ledger()[entity_id][i].alive == 0U)
            return (int8_t)i;
    return -1;
}

static int8_t control_music_output_find_oldest(brick_entity_id_t entity_id,
                                               uint8_t excluded_mask)
{
    int8_t oldest_index = -1;
    uint32_t oldest_age = UINT32_MAX;
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
    {
        const control_music_output_t *const output =
            &control_music_output_ledger()[entity_id][i];
        if ((output->alive == 0U)
                || ((excluded_mask & (uint8_t)(1U << i)) != 0U))
            continue;
        if (output->age < oldest_age)
        {
            oldest_age = output->age;
            oldest_index = (int8_t)i;
        }
    }
    return oldest_index;
}

static uint8_t control_music_output_cause_is_external(uint32_t causal_source_id)
{
    const uint32_t name_space = causal_source_id & UINT32_C(0xC0000000);
    return (uint8_t)((name_space == UINT32_C(0x40000000))
        || (name_space == UINT32_C(0x80000000)));
}

static void control_music_output_send_midi_off(
    const control_music_output_t *output)
{
    if ((output->midi_dest_mask & MIDI_ADMISSION_UART) != 0U)
        (void)midi_note_off_admit(MIDI_DEST_UART, output->midi_channel,
                                  output->note, 0U);
    if ((output->midi_dest_mask & MIDI_ADMISSION_USB) != 0U)
        (void)midi_note_off_admit(MIDI_DEST_USB, output->midi_channel,
                                  output->note, 0U);
}

uint8_t control_music_output_submit(const control_music_action_t *action,
                                    uint32_t causal_source_id,
                                    uint32_t generation)
{
    if ((action == NULL) || (action->entity_id >= BRICK_ENTITY_CAPACITY)
            || (control_music_action_kind(action)
                > (uint8_t)CONTROL_MUSIC_ACTION_RETRIGGER)
            || (action->output_id == 0U))
        return 0U;

    const brick_entity_id_t entity_id = action->entity_id;
    const int8_t existing = control_music_output_find(entity_id,
                                                       action->output_id);
    if (control_music_action_kind(action)
            == (uint8_t)CONTROL_MUSIC_ACTION_STOP)
    {
        if (existing < 0)
        {
            return 1U;
        }
        if (control_music_output_ledger()[entity_id][(uint8_t)existing]
                .generation != generation)
            return 1U;
        if (control_music_output_publish(action) == 0U)
        {
            return 0U;
        }
        control_music_output_send_midi_off(
            &control_music_output_ledger()[entity_id][(uint8_t)existing]);
        control_music_output_mark_dead(entity_id, (uint8_t)existing);
        return 1U;
    }

    if (existing >= 0)
    {
        control_music_action_t retrigger = *action;
        retrigger.kind = (uint8_t)(CONTROL_MUSIC_ACTION_RETRIGGER
            | (action->kind & (CONTROL_MUSIC_ACTION_EXTERNAL_FLAG
                | CONTROL_MUSIC_ACTION_CHANNEL_MASK)));
        if (control_music_output_publish(&retrigger) == 0U)
            return 0U;
        control_music_output_t *const output =
            &control_music_output_ledger()[entity_id][(uint8_t)existing];
        control_music_output_send_midi_off(output);
        output->age = (g_control_music_window_active != 0U)
            ? ++g_control_music_output_age_staged
            : ++g_control_music_output_age;
        output->note = action->note;
        output->velocity = action->velocity;
        output->causal_source_id = causal_source_id;
        output->generation = generation;
        output->midi_channel = control_music_action_channel(action);
        output->midi_dest_mask = midi_note_on_admit(
            MIDI_DEST_BOTH, output->midi_channel, output->note,
            output->velocity);
        return 1U;
    }

    const uint8_t live_count = control_music_output_live_count(entity_id);
    const uint8_t limit = control_music_output_limit(entity_id);
    control_music_action_t batch[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    brick_entity_id_t victim_entities[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    uint8_t victim_indices[CONTROL_MUSIC_OUTPUTS_PER_ENTITY + 1U];
    uint16_t count = 0U;
    uint8_t victim_count = 0U;
    if (control_music_output_is_multi(entity_id) != 0U)
    {
        const uint8_t per_track_stops = (live_count >= limit)
            ? (uint8_t)(live_count - limit + 1U) : 0U;
        uint8_t excluded_mask = 0U;
        for (; victim_count < per_track_stops; ++victim_count)
        {
            const int8_t target = control_music_output_find_oldest(
                entity_id, excluded_mask);
            if (target < 0)
                return 0U;
            victim_entities[victim_count] = entity_id;
            victim_indices[victim_count] = (uint8_t)target;
            excluded_mask |= (uint8_t)(1U << (uint8_t)target);
        }
        const uint8_t multi_live_count = control_music_output_multi_live_count();
        if (((uint16_t)multi_live_count - victim_count + 1U)
                > BRICK6_SAMPLER_MULTI_MAX_VOICES)
        {
            brick_entity_id_t target_entity = BRICK_ENTITY_INVALID_ID;
            uint8_t target_index = UINT8_MAX;
            if (control_music_output_find_oldest_multi(
                    victim_entities, victim_indices, victim_count,
                    &target_entity, &target_index) == 0U)
                return 0U;
            victim_entities[victim_count] = target_entity;
            victim_indices[victim_count++] = target_index;
        }
    }
    else
    {
        const uint8_t stop_count = (live_count >= limit)
            ? (uint8_t)(live_count - limit + 1U) : 0U;
        uint8_t excluded_mask = 0U;
        for (victim_count = 0U; victim_count < stop_count; ++victim_count)
        {
            const int8_t target = control_music_output_find_oldest(
                entity_id, excluded_mask);
            if (target < 0)
                return 0U;
            victim_entities[victim_count] = entity_id;
            victim_indices[victim_count] = (uint8_t)target;
            excluded_mask |= (uint8_t)(1U << (uint8_t)target);
        }
    }
    for (uint8_t i = 0U; i < victim_count; ++i)
    {
        const brick_entity_id_t victim_entity = victim_entities[i];
        const uint8_t victim_index = victim_indices[i];
        const control_music_output_t *const victim =
            &control_music_output_ledger()[victim_entity][victim_index];
        batch[count++] = (control_music_action_t){
            .due_sample = action->due_sample,
            .output_id = victim->output_id,
            .kind = (uint8_t)(CONTROL_MUSIC_ACTION_STOP
                | (control_music_output_cause_is_external(
                    victim->causal_source_id)
                    ? CONTROL_MUSIC_ACTION_EXTERNAL_FLAG : 0U)),
            .entity_id = victim_entity,
            .note = victim->note
        };
    }
    batch[count++] = *action;
    if (control_music_output_publish_batch(batch, count) == 0U)
    {
        return 0U;
    }

    for (uint8_t i = 0U; i < victim_count; ++i)
    {
        control_music_output_send_midi_off(
            &control_music_output_ledger()[victim_entities[i]]
                [victim_indices[i]]);
        control_music_output_mark_dead(victim_entities[i], victim_indices[i]);
    }
    const int8_t target = control_music_output_find_free(entity_id);
    if (target < 0)
        return 0U;

    control_music_output_ledger()[entity_id][(uint8_t)target] =
        (control_music_output_t){
            .output_id = action->output_id,
            .causal_source_id = causal_source_id,
            .generation = generation,
            .age = (g_control_music_window_active != 0U)
                ? ++g_control_music_output_age_staged
                : ++g_control_music_output_age,
            .note = action->note,
            .velocity = action->velocity,
            .alive = 1U,
            .multi = (control_music_output_is_multi(entity_id) != 0U),
            .midi_channel = control_music_action_channel(action),
            .midi_dest_mask = midi_note_on_admit(
                MIDI_DEST_BOTH, control_music_action_channel(action),
                action->note, action->velocity),
        };
    return 1U;
}

void control_music_output_set_multi(brick_entity_id_t entity_id,
                                    uint8_t is_multi)
{
    if (entity_id >= BRICK_ENTITY_CAPACITY)
        return;
    for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        if (control_music_output_ledger()[entity_id][i].alive != 0U)
            control_music_output_ledger()[entity_id][i].multi =
                (is_multi != 0U) ? 1U : 0U;
}

uint8_t control_music_output_count(brick_entity_id_t entity_id)
{
    return (entity_id < BRICK_ENTITY_CAPACITY)
        ? control_music_output_live_count(entity_id) : 0U;
}

static uint8_t control_music_output_close_selected(
    const uint8_t selected[BRICK_ENTITY_CAPACITY]
                          [CONTROL_MUSIC_OUTPUTS_PER_ENTITY],
    uint64_t due_sample)
{
    if (selected == NULL)
        return 0U;
    uint16_t counts[2U] = { 0U, 0U };
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const output =
                &control_music_output_ledger()[entity_id][i];
            if ((selected[entity_id][i] == 0U) || (output->alive == 0U))
                continue;
            const uint8_t external =
                control_music_output_cause_is_external(
                    output->causal_source_id);
            ++counts[external];
        }

    if ((counts[0U] == 0U) && (counts[1U] == 0U))
        return 1U;

    uint8_t opened_window = 0U;
    if (g_control_music_window_active == 0U)
    {
        due_sample = control_music_output_first_unpublished_sample(due_sample);
        if (control_music_output_begin_window(due_sample, 1U) == 0U)
            return 0U;
        opened_window = 1U;
    }

    if ((g_control_music_window_internal.count + counts[0U]
            > g_control_music_window_internal_limit)
            || (g_control_music_window_external.count + counts[1U]
                > g_control_music_window_external_limit))
    {
        if (opened_window != 0U)
            control_music_output_abort_window();
        return 0U;
    }

    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
    {
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const output =
                &control_music_output_ledger()[entity_id][i];
            if ((selected[entity_id][i] == 0U) || (output->alive == 0U))
                continue;
            const control_music_action_t action = {
                .due_sample = due_sample,
                .output_id = output->output_id,
                .entity_id = entity_id,
                .kind = (uint8_t)(CONTROL_MUSIC_ACTION_STOP
                    | (control_music_output_cause_is_external(
                        output->causal_source_id)
                        ? CONTROL_MUSIC_ACTION_EXTERNAL_FLAG : 0U)),
                .note = output->note
            };
            if (control_music_output_publish(&action) == 0U)
            {
                if (opened_window != 0U)
                    control_music_output_abort_window();
                return 0U;
            }
        }
    }

    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U;
             i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            if ((selected[entity_id][i] != 0U)
                    && (control_music_output_ledger()[entity_id][i].alive != 0U))
            {
                control_music_output_send_midi_off(
                    &control_music_output_ledger()[entity_id][i]);
                control_music_output_mark_dead(entity_id, i);
            }

    if ((opened_window != 0U)
            && (control_music_output_commit_window() == 0U))
    {
        control_music_output_abort_window();
        return 0U;
    }

    if ((opened_window != 0U)
            && (control_music_output_finalize_window() == 0U))
        return 0U;

    return 1U;
}

uint8_t control_music_output_close_causal_sources(
    const uint32_t *causal_source_ids, uint16_t source_count,
    uint64_t due_sample)
{
    if ((causal_source_ids == NULL) || (source_count == 0U))
        return 0U;
    uint8_t selected[BRICK_ENTITY_CAPACITY]
                    [CONTROL_MUSIC_OUTPUTS_PER_ENTITY] = {{0U}};
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t output_index = 0U;
             output_index < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++output_index)
        {
            const control_music_output_t *const output =
                &control_music_output_ledger()[entity_id][output_index];
            if (output->alive == 0U)
                continue;
            for (uint16_t source_index = 0U;
                 source_index < source_count; ++source_index)
                if (output->causal_source_id
                        == causal_source_ids[source_index])
                {
                    selected[entity_id][output_index] = 1U;
                    break;
                }
        }
    return control_music_output_close_selected(selected, due_sample);
}

static void control_music_output_close_all_midi(void)
{
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            if (control_music_output_ledger()[entity_id][i].alive != 0U)
                control_music_output_send_midi_off(
                    &control_music_output_ledger()[entity_id][i]);
}

uint8_t control_music_output_panic_all(uint8_t send_transport_stop)
{
    uint64_t due_sample = 0U;
    (void)live_clock_read_audio_sample(&due_sample);
    if (control_audio_publish_panic(CONTROL_AUDIO_PANIC_GLOBAL, 0U,
            control_music_output_first_unpublished_sample(due_sample)) == 0U)
        return 0U;
    control_music_output_close_all_midi();
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            control_music_output_mark_dead(entity_id, i);
    if (send_transport_stop != 0U)
        midi_stop(MIDI_DEST_BOTH);
    return 1U;
}

uint8_t control_music_output_panic_all_fenced(uint32_t *out_consumer_fence)
{
    if (out_consumer_fence == NULL)
        return 0U;
    uint64_t due_sample = 0U;
    (void)live_clock_read_audio_sample(&due_sample);
    if (control_audio_publish_panic_fenced(CONTROL_AUDIO_PANIC_GLOBAL, 0U,
            control_music_output_first_unpublished_sample(due_sample),
            out_consumer_fence) == 0U)
        return 0U;
    control_music_output_close_all_midi();
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            control_music_output_mark_dead(entity_id, i);
    return 1U;
}

uint8_t control_music_output_has_alive(void)
{
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
            if (control_music_output_ledger()[entity_id][i].alive != 0U)
                return 1U;
    return 0U;
}

uint8_t control_music_output_is_note_active_on_channel(
    uint8_t channel_zero_based, uint8_t note)
{
    if ((channel_zero_based >= 16U) || (note >= 128U))
        return 0U;
    for (brick_entity_id_t entity_id = 0U;
         entity_id < BRICK_ENTITY_CAPACITY; ++entity_id)
        for (uint8_t i = 0U; i < CONTROL_MUSIC_OUTPUTS_PER_ENTITY; ++i)
        {
            const control_music_output_t *const output =
                &control_music_output_ledger()[entity_id][i];
            if ((output->alive != 0U)
                    && (output->midi_dest_mask != 0U)
                    && (output->midi_channel == channel_zero_based)
                    && (output->note == note))
                return 1U;
        }
    return 0U;
}
