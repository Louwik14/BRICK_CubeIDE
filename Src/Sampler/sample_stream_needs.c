#include "Sampler/sample_stream_needs.h"

#include <string.h>

#include "Sampler/sample_audio_format.h"
#include "Sampler/sample_stream_sequence.h"
#include "Sampler/sample_stream_trace.h"
#include "Sampler/sample_stream_underrun_trace.h"

#define SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY
#define SAMPLE_STREAM_NEEDS_MULTI_CAPACITY   SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY
#define SAMPLE_STREAM_NEEDS_CAPACITY \
    (SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY + SAMPLE_STREAM_NEEDS_MULTI_CAPACITY)

typedef struct
{
    volatile uint32_t sequence;
    sample_stream_target_voice_registry_entry_t value;
} sample_stream_needs_slot_t;

static sample_stream_needs_slot_t g_sample_stream_needs_slots[SAMPLE_STREAM_NEEDS_CAPACITY];
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
static uint8_t g_sample_stream_needs_calibration_depth =
    SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE;
#endif

static uint8_t sample_stream_needs_slot_index(sample_stream_snapshot_source_t source,
                                              uint8_t voice_id,
                                              uint8_t *out_index)
{
    uint32_t index = 0U;
    if (source == SAMPLE_STREAM_SNAPSHOT_CLASSIC)
    {
        if (voice_id >= SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY)
        {
            return 0U;
        }
        index = voice_id;
    }
    else if (source == SAMPLE_STREAM_SNAPSHOT_MULTI)
    {
        if (voice_id >= SAMPLE_STREAM_NEEDS_MULTI_CAPACITY)
        {
            return 0U;
        }
        index = SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY + voice_id;
    }
    else
    {
        return 0U;
    }

    *out_index = (uint8_t)index;
    return 1U;
}

static uint32_t sample_stream_needs_frames_per_page(
    const sample_stream_snapshot_t *snapshot)
{
    if (snapshot->frames_per_page != 0U)
    {
        return snapshot->frames_per_page;
    }
    return sample_audio_format_frames_per_page(sample_audio_format_or_stereo(snapshot->format));
}

static uint8_t sample_stream_needs_loop_valid(const sample_stream_snapshot_t *snapshot)
{
    return ((snapshot->loop_enabled != 0U)
            && (snapshot->loop_begin >= snapshot->region_begin)
            && (snapshot->loop_end > snapshot->loop_begin)
            && (snapshot->loop_end <= snapshot->region_end))
               ? 1U
               : 0U;
}

static uint32_t sample_stream_needs_source_distance(const sample_stream_snapshot_t *snapshot,
                                                    uint32_t page,
                                                    uint8_t page_order,
                                                    uint32_t current)
{
    const uint32_t frames_per_page = sample_stream_needs_frames_per_page(snapshot);
    if ((frames_per_page == 0U) || (page_order == 0U))
    {
        return 0U;
    }

    const uint32_t page_start = page * frames_per_page;
    if (page_start >= current)
    {
        return page_start - current;
    }
    if (sample_stream_needs_loop_valid(snapshot) != 0U)
    {
        const uint32_t to_loop = snapshot->loop_end - current;
        const uint32_t loop_offset = (page_start >= snapshot->loop_begin)
                                          ? (page_start - snapshot->loop_begin)
                                          : 0U;
        return to_loop + loop_offset;
    }
    return 0U;
}

static sample_stream_audio_frame_t sample_stream_needs_deadline(
    const sample_stream_snapshot_t *snapshot,
    sample_stream_audio_frame_t now_audio_frame,
    uint32_t source_distance)
{
    const uint32_t step_q16 = (snapshot->step_q16 != 0U) ? snapshot->step_q16 : 65536U;
    const uint32_t output_distance = sample_stream_time_source_to_output_frames(
        source_distance,
        step_q16);
    return sample_stream_time_deadline_after(now_audio_frame, output_distance);
}

static uint8_t sample_stream_needs_contains(
    const sample_stream_target_voice_registry_entry_t *entry,
    const sample_audio_key_t *key,
    uint32_t page_index,
    uint32_t registration_epoch)
{
    for (uint8_t i = 0U; i < entry->need_count; ++i)
    {
        const sample_stream_target_voice_need_t *const need = &entry->needs[i];
        if ((need->valid != 0U)
            && (need->page_index == page_index)
            && (need->registration_epoch == registration_epoch)
            && (need->key.domain == key->domain)
            && (need->key.object_id == key->object_id))
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t sample_stream_needs_append(
    sample_stream_target_voice_registry_entry_t *entry,
    const sample_stream_snapshot_t *snapshot,
    uint32_t page_index,
    sample_stream_need_role_t role,
    sample_stream_audio_frame_t deadline)
{
    if (sample_stream_needs_contains(entry,
                                     &snapshot->key,
                                     page_index,
                                     snapshot->registration_epoch) != 0U)
    {
        return 1U;
    }
    if (entry->need_count >= SAMPLE_STREAM_TARGET_NEEDS_PER_VOICE)
    {
        return 0U;
    }
    sample_stream_target_voice_need_t *const need = &entry->needs[entry->need_count++];
    need->key = snapshot->key;
    need->page_index = page_index;
    need->registration_epoch = snapshot->registration_epoch;
    need->consume_deadline_audio_frame = deadline;
    need->role = (uint8_t)role;
    need->valid = 1U;
    return 1U;
}

uint8_t sample_stream_needs_build(
    const sample_stream_snapshot_t *snapshot,
    sample_stream_audio_frame_t now_audio_frame,
    sample_stream_target_voice_registry_entry_t *out_entry)
{
    if ((snapshot == NULL) || (out_entry == NULL)
        || (snapshot->active == 0U)
        || (snapshot->generation == 0U)
        || (snapshot->region_end <= snapshot->region_begin)
        || (snapshot->current_frame >= snapshot->region_end)
        || (snapshot->key.domain > SAMPLE_AUDIO_DOMAIN_MULTI)
        || (snapshot->direction != 1)
        || (sample_stream_needs_frames_per_page(snapshot) == 0U))
    {
        return 0U;
    }

    memset(out_entry, 0, sizeof(*out_entry));
    out_entry->active = 1U;
    out_entry->voice_index = snapshot->voice_id;
    out_entry->generation = snapshot->generation;

    sample_stream_sequence_input_t sequence_input = {
        .current_frame = snapshot->current_frame,
        .region_begin = snapshot->region_begin,
        .region_end = snapshot->region_end,
        .loop_begin = snapshot->loop_begin,
        .loop_end = snapshot->loop_end,
        .frames_per_page = sample_stream_needs_frames_per_page(snapshot),
        .direction = snapshot->direction,
        .loop_enabled = snapshot->loop_enabled,
    };
    uint32_t pages[SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE] = { 0U };
    uint8_t page_count = 0U;
    const uint8_t mobile_capacity =
#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
        g_sample_stream_needs_calibration_depth;
#else
        SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE;
#endif
    if (sample_stream_sequence_build(&sequence_input,
                                    pages,
                                    mobile_capacity,
                                    &page_count) == 0U)
    {
        return 0U;
    }

    uint32_t current = snapshot->current_frame;
    if (current < snapshot->region_begin)
    {
        current = snapshot->region_begin;
    }
    if (current >= snapshot->region_end)
    {
        current = snapshot->region_end - 1U;
    }

    const uint8_t loop_valid = sample_stream_needs_loop_valid(snapshot);
    if (loop_valid != 0U)
    {
        if (current >= snapshot->loop_end)
        {
            current = snapshot->loop_begin;
        }
    }
    for (uint8_t i = 0U; i < page_count; ++i)
    {
        const sample_stream_audio_frame_t deadline = sample_stream_needs_deadline(
            snapshot,
            now_audio_frame,
            sample_stream_needs_source_distance(snapshot, pages[i], i, current));
        if (sample_stream_needs_append(
                out_entry,
                snapshot,
                pages[i],
                (i == 0U) ? SAMPLE_STREAM_NEED_ROLE_CURRENT
                          : SAMPLE_STREAM_NEED_ROLE_ANTICIPATION,
                deadline) == 0U)
        {
            return 0U;
        }
    }

    /* Forward-loop preload is part of the voice-owned need set. It is kept
     * after the mobile horizon, so it cannot outrank current playback. */
    if (loop_valid != 0U)
    {
        const uint32_t frames_per_page = sample_stream_needs_frames_per_page(snapshot);
        const uint32_t first_loop_page = snapshot->loop_begin / frames_per_page;
        const uint32_t last_loop_page = (snapshot->loop_end - 1U) / frames_per_page;
        const uint32_t preload_pages = sample_audio_format_presocle_pages(
            sample_audio_format_or_stereo(snapshot->format));
        for (uint32_t ahead = 0U;
             (ahead < preload_pages) && ((first_loop_page + ahead) <= last_loop_page);
             ++ahead)
        {
            if (sample_stream_needs_append(out_entry,
                                           snapshot,
                                           first_loop_page + ahead,
                                           SAMPLE_STREAM_NEED_ROLE_LOOP,
                                           SAMPLE_STREAM_AUDIO_FRAME_NEVER) == 0U)
            {
                return 0U;
            }
        }
    }
    return (out_entry->need_count != 0U) ? 1U : 0U;
}

void sample_stream_needs_registry_reset(void)
{
    memset(g_sample_stream_needs_slots, 0, sizeof(g_sample_stream_needs_slots));
}

uint8_t sample_stream_needs_registry_update(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot,
    sample_stream_audio_frame_t now_audio_frame)
{
    uint8_t index = 0U;
    if ((snapshot == NULL)
        || (sample_stream_needs_slot_index(source, voice_id, &index) == 0U))
    {
        return 0U;
    }

    sample_stream_target_voice_registry_entry_t entry;
    if (sample_stream_needs_build(snapshot, now_audio_frame, &entry) == 0U)
    {
        return 0U;
    }
    entry.voice_index = voice_id;
    sample_stream_needs_slot_t *const slot = &g_sample_stream_needs_slots[index];
    const sample_stream_target_voice_registry_entry_t previous = slot->value;
    uint32_t sequence = slot->sequence;
    if ((sequence & 1U) != 0U)
    {
        sequence++;
    }
    slot->sequence = sequence + 1U;
    slot->value = entry;
    slot->sequence = sequence + 2U;
    for (uint8_t need_index = 0U; need_index < entry.need_count; ++need_index)
    {
        const sample_stream_target_voice_need_t *const need = &entry.needs[need_index];
        const uint8_t was_present =
            ((previous.active != 0U) && (previous.generation == entry.generation))
                ? sample_stream_needs_contains(&previous,
                                               &need->key,
                                               need->page_index,
                                               need->registration_epoch)
                : 0U;
        if (was_present == 0U)
        {
            const uint64_t now = now_audio_frame;
            const uint32_t frames_ahead =
                (need->consume_deadline_audio_frame > now)
                    ? (uint32_t)((need->consume_deadline_audio_frame - now) > UINT32_MAX
                                     ? UINT32_MAX
                                     : (need->consume_deadline_audio_frame - now))
                    : 0U;
            brick6_stream_underrun_trace_need_created(
                source, voice_id, entry.generation, need, frames_ahead);
        }
    }
    (void)sample_stream_event_trace_record(
        SAMPLE_STREAM_EVENT_NEED_ADD,
        snapshot->key,
        (entry.need_count != 0U) ? entry.needs[0].page_index : UINT32_MAX,
        (uint8_t)source,
        voice_id,
        entry.generation,
        0U,
        entry.need_count,
        snapshot->current_frame,
        0U);
    return 1U;
}

uint8_t sample_stream_needs_registry_read(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    sample_stream_target_voice_registry_entry_t *out_entry)
{
    uint8_t index = 0U;
    if ((out_entry == NULL)
        || (sample_stream_needs_slot_index(source, voice_id, &index) == 0U))
    {
        return 0U;
    }

    const sample_stream_needs_slot_t *const slot = &g_sample_stream_needs_slots[index];
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = slot->sequence;
        if ((before == 0U) || ((before & 1U) != 0U))
        {
            continue;
        }
        const sample_stream_target_voice_registry_entry_t copy = slot->value;
        const uint32_t after = slot->sequence;
        if (before == after)
        {
            *out_entry = copy;
            return (copy.active != 0U) ? 1U : 0U;
        }
    }
    return 0U;
}

void sample_stream_needs_registry_drop(sample_stream_snapshot_source_t source,
                                       uint8_t voice_id)
{
    uint8_t index = 0U;
    if (sample_stream_needs_slot_index(source, voice_id, &index) == 0U)
    {
        return;
    }

    sample_stream_needs_slot_t *const slot = &g_sample_stream_needs_slots[index];
    const sample_stream_target_voice_registry_entry_t dropped = slot->value;
    uint32_t sequence = slot->sequence;
    if ((sequence & 1U) != 0U)
    {
        sequence++;
    }
    slot->sequence = sequence + 1U;
    memset(&slot->value, 0, sizeof(slot->value));
    slot->sequence = sequence + 2U;
    if (dropped.active != 0U)
    {
        (void)sample_stream_event_trace_record(
            SAMPLE_STREAM_EVENT_NEED_DROP,
            (dropped.need_count != 0U) ? dropped.needs[0].key
                                      : (sample_audio_key_t){ 0U, 0U },
            (dropped.need_count != 0U) ? dropped.needs[0].page_index : UINT32_MAX,
            (uint8_t)source,
            voice_id,
            dropped.generation,
            0U,
            dropped.need_count,
            0U,
            0U);
    }
}

uint8_t sample_stream_needs_registry_drop_generation(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    uint32_t generation)
{
    sample_stream_target_voice_registry_entry_t entry;
    if ((generation == 0U)
        || (sample_stream_needs_registry_read(source, voice_id, &entry) == 0U)
        || (entry.generation != generation))
    {
        return 0U;
    }
    sample_stream_needs_registry_drop(source, voice_id);
    return 1U;
}

uint8_t sample_stream_needs_registry_contains(sample_stream_snapshot_source_t source,
                                               uint8_t voice_id,
                                               sample_audio_key_t key,
                                               uint32_t page_index,
                                               uint32_t registration_epoch,
                                               uint32_t generation)
{
    sample_stream_target_voice_registry_entry_t entry;
    if (sample_stream_needs_registry_read(source, voice_id, &entry) == 0U)
    {
        return 0U;
    }
    if ((generation != 0U) && (entry.generation != generation))
    {
        return 0U;
    }
    return sample_stream_needs_contains(&entry, &key, page_index, registration_epoch);
}

uint8_t sample_stream_needs_registry_contains_any(sample_audio_key_t key,
                                                  uint32_t page_index,
                                                  uint32_t registration_epoch)
{
    sample_stream_target_voice_registry_entry_t entry;
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY; ++voice_id)
    {
        if ((sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                               voice_id,
                                               &entry) != 0U)
            && (sample_stream_needs_contains(&entry,
                                             &key,
                                             page_index,
                                             registration_epoch) != 0U))
        {
            return 1U;
        }
    }
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_MULTI_CAPACITY; ++voice_id)
    {
        if ((sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                               voice_id,
                                               &entry) != 0U)
            && (sample_stream_needs_contains(&entry,
                                             &key,
                                             page_index,
                                             registration_epoch) != 0U))
        {
            return 1U;
        }
    }
    return 0U;
}

uint8_t sample_stream_needs_registry_contains_key(sample_audio_key_t key)
{
    sample_stream_target_voice_registry_entry_t entry;
    for (uint8_t source = (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC;
         source <= (uint8_t)SAMPLE_STREAM_SNAPSHOT_MULTI;
         ++source)
    {
        const uint8_t capacity =
            (source == (uint8_t)SAMPLE_STREAM_SNAPSHOT_CLASSIC)
                ? SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY
                : SAMPLE_STREAM_NEEDS_MULTI_CAPACITY;
        for (uint8_t voice_id = 0U; voice_id < capacity; ++voice_id)
        {
            if (sample_stream_needs_registry_read(
                    (sample_stream_snapshot_source_t)source,
                    voice_id,
                    &entry) == 0U)
            {
                continue;
            }
            for (uint8_t i = 0U; i < entry.need_count; ++i)
            {
                if ((entry.needs[i].valid != 0U)
                    && (entry.needs[i].key.domain == key.domain)
                    && (entry.needs[i].key.object_id == key.object_id))
                {
                    return 1U;
                }
            }
        }
    }
    return 0U;
}

uint8_t sample_stream_needs_registry_has_active(void)
{
    sample_stream_target_voice_registry_entry_t entry;
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY; ++voice_id)
    {
        if (sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                              voice_id,
                                              &entry) != 0U)
        {
            return 1U;
        }
    }
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_MULTI_CAPACITY; ++voice_id)
    {
        if (sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                              voice_id,
                                              &entry) != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

uint32_t sample_stream_needs_registry_count_active(void)
{
    uint32_t count = 0U;
    sample_stream_target_voice_registry_entry_t entry;
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY; ++voice_id)
    {
        count += (sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_CLASSIC,
                                                    voice_id,
                                                    &entry) != 0U) ? 1U : 0U;
    }
    for (uint8_t voice_id = 0U; voice_id < SAMPLE_STREAM_NEEDS_MULTI_CAPACITY; ++voice_id)
    {
        count += (sample_stream_needs_registry_read(SAMPLE_STREAM_SNAPSHOT_MULTI,
                                                    voice_id,
                                                    &entry) != 0U) ? 1U : 0U;
    }
    return count;
}

#if defined(BRICK6_STREAM_CALIBRATION) && BRICK6_STREAM_CALIBRATION
void sample_stream_needs_calibration_set_depth(uint8_t pages)
{
    if (pages == 0U)
    {
        pages = 1U;
    }
    if (pages > SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE)
    {
        pages = SAMPLE_STREAM_TARGET_MOBILE_NEEDS_PER_VOICE;
    }
    g_sample_stream_needs_calibration_depth = pages;
}
#endif
