#include "Sampler/sample_stream_needs.h"

#include <string.h>

#include "Sampler/sample_audio_format.h"
#include "Sampler/sample_stream_sequence.h"
#include "Storage/memory_layout.h"
#define SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY
#define SAMPLE_STREAM_NEEDS_MULTI_CAPACITY   SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY
#define SAMPLE_STREAM_NEEDS_CAPACITY \
    (SAMPLE_STREAM_NEEDS_CLASSIC_CAPACITY + SAMPLE_STREAM_NEEDS_MULTI_CAPACITY)

typedef struct
{
    volatile uint32_t sequence;
    sample_stream_target_voice_registry_entry_t value;
} sample_stream_needs_slot_t;

D2_IPC static sample_stream_needs_slot_t g_sample_stream_needs_slots[SAMPLE_STREAM_NEEDS_CAPACITY];

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

static uint8_t sample_stream_needs_contains(
    const sample_stream_target_voice_registry_entry_t *entry,
    const sample_audio_key_t *key,
    uint32_t page_index,
    uint32_t registration_epoch)
{
    if ((entry->active == 0U)
        || (entry->registration_epoch != registration_epoch)
        || (entry->key.domain != key->domain)
        || (entry->key.object_id != key->object_id))
        return 0U;
    const uint8_t count = (uint8_t)(entry->mobile_page_count
                                    + entry->loop_preload_count);
    for (uint8_t i = 0U; i < count; ++i)
    {
        uint32_t published_page = 0U;
        if ((sample_stream_needs_entry_page_at(entry, i, &published_page) != 0U)
            && (published_page == page_index))
            return 1U;
    }
    return 0U;
}

uint8_t sample_stream_needs_entry_page_at(
    const sample_stream_target_voice_registry_entry_t *entry,
    uint8_t index,
    uint32_t *out_page_index)
{
    if ((entry == NULL) || (out_page_index == NULL) || (entry->active == 0U))
        return 0U;
    if (index < entry->mobile_page_count)
    {
        uint32_t page = entry->current_page + index;
        if ((entry->loop_enabled != 0U) && (page > entry->loop_last_page))
            page = entry->loop_first_page
                 + ((page - entry->loop_last_page - 1U)
                    % (entry->loop_last_page - entry->loop_first_page + 1U));
        *out_page_index = page;
        return 1U;
    }
    index = (uint8_t)(index - entry->mobile_page_count);
    if (index < entry->loop_preload_count)
    {
        *out_page_index = entry->loop_first_page + index;
        return 1U;
    }
    return 0U;
}

uint8_t sample_stream_needs_build(
    const sample_stream_snapshot_t *snapshot,
    sample_stream_target_voice_registry_entry_t *out_entry)
{
    if ((snapshot == NULL) || (out_entry == NULL)
        || (snapshot->active == 0U)
        || (snapshot->owner_token == 0U)
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
    out_entry->owner_token = snapshot->owner_token;
    out_entry->key = snapshot->key;
    out_entry->registration_epoch = snapshot->registration_epoch;

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
        (uint8_t)sample_audio_format_multi_mobile_pages(
            sample_audio_format_or_stereo(snapshot->format));
    if (sample_stream_sequence_build(&sequence_input,
                                    pages,
                                    mobile_capacity,
                                    &page_count) == 0U)
    {
        return 0U;
    }

    const uint8_t loop_valid = sample_stream_needs_loop_valid(snapshot);
    out_entry->current_page = pages[0];
    out_entry->mobile_page_count = page_count;
    out_entry->loop_enabled = loop_valid;

    /* Forward-loop preload is part of the voice-owned need set. It is kept
     * after the mobile horizon, so it cannot outrank current playback. */
    if (loop_valid != 0U)
    {
        const uint32_t frames_per_page = sample_stream_needs_frames_per_page(snapshot);
        const uint32_t first_loop_page = snapshot->loop_begin / frames_per_page;
        const uint32_t last_loop_page = (snapshot->loop_end - 1U) / frames_per_page;
        uint32_t preload_pages = sample_audio_format_presocle_pages(
            sample_audio_format_or_stereo(snapshot->format));
        const uint32_t loop_pages = last_loop_page - first_loop_page + 1U;
        if (preload_pages > loop_pages) preload_pages = loop_pages;
        out_entry->loop_first_page = first_loop_page;
        out_entry->loop_last_page = last_loop_page;
        out_entry->loop_preload_count = (uint8_t)preload_pages;
    }
    return (out_entry->mobile_page_count != 0U) ? 1U : 0U;
}

void sample_stream_needs_registry_reset(void)
{
    memset(g_sample_stream_needs_slots, 0, sizeof(g_sample_stream_needs_slots));
}

uint8_t sample_stream_needs_registry_update(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    const sample_stream_snapshot_t *snapshot)
{
    uint8_t index = 0U;
    if ((snapshot == NULL)
        || (sample_stream_needs_slot_index(source, voice_id, &index) == 0U))
    {
        return 0U;
    }

    sample_stream_target_voice_registry_entry_t entry;
    if (sample_stream_needs_build(snapshot, &entry) == 0U)
    {
        return 0U;
    }
    entry.voice_index = voice_id;
    sample_stream_needs_slot_t *const slot = &g_sample_stream_needs_slots[index];
    uint32_t sequence = slot->sequence;
    if ((sequence & 1U) != 0U)
    {
        sequence++;
    }
    slot->sequence = sequence + 1U;
    slot->value = entry;
    slot->sequence = sequence + 2U;
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
    }
}

uint8_t sample_stream_needs_registry_drop_owner(
    sample_stream_snapshot_source_t source,
    uint8_t voice_id,
    uint32_t owner_token)
{
    sample_stream_target_voice_registry_entry_t entry;
    if ((owner_token == 0U)
        || (sample_stream_needs_registry_read(source, voice_id, &entry) == 0U)
        || (entry.owner_token != owner_token))
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
                                               uint32_t owner_token)
{
    sample_stream_target_voice_registry_entry_t entry;
    if (sample_stream_needs_registry_read(source, voice_id, &entry) == 0U)
    {
        return 0U;
    }
    if ((owner_token != 0U) && (entry.owner_token != owner_token))
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
            if ((entry.key.domain == key.domain)
                && (entry.key.object_id == key.object_id))
                return 1U;
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
