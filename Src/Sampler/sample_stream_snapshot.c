#include "Sampler/sample_stream_snapshot.h"

#include <string.h>

#include "Sampler/sample_stream_admission.h"

typedef struct
{
    volatile uint32_t sequence;
    sample_stream_snapshot_t value;
} sample_stream_snapshot_slot_t;

static sample_stream_snapshot_slot_t
    g_sample_stream_snapshot_slots[SAMPLE_STREAM_SNAPSHOT_CAPACITY];

static uint8_t sample_stream_snapshot_slot_index(sample_stream_snapshot_source_t source,
                                                 uint8_t voice_id,
                                                 uint8_t *out_index)
{
    uint32_t index = 0U;
    if (source == SAMPLE_STREAM_SNAPSHOT_CLASSIC)
    {
        if (voice_id >= SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY)
        {
            return 0U;
        }
        index = voice_id;
    }
    else if (source == SAMPLE_STREAM_SNAPSHOT_MULTI)
    {
        if (voice_id >= SAMPLE_STREAM_SNAPSHOT_MULTI_CAPACITY)
        {
            return 0U;
        }
        index = SAMPLE_STREAM_SNAPSHOT_CLASSIC_CAPACITY + voice_id;
    }
    else
    {
        return 0U;
    }

    *out_index = (uint8_t)index;
    return 1U;
}

void sample_stream_snapshot_init(sample_stream_snapshot_t *snapshot)
{
    if (snapshot != 0)
    {
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->key.domain = SAMPLE_AUDIO_DOMAIN_CLASSIC;
        snapshot->voice_id = UINT8_MAX;
        snapshot->direction = 1;
    }
}

void sample_stream_snapshot_registry_reset(void)
{
    memset(g_sample_stream_snapshot_slots, 0, sizeof(g_sample_stream_snapshot_slots));
    for (uint32_t i = 0U; i < SAMPLE_STREAM_SNAPSHOT_CAPACITY; ++i)
    {
        sample_stream_snapshot_init(&g_sample_stream_snapshot_slots[i].value);
    }
}

uint8_t sample_stream_snapshot_publish(sample_stream_snapshot_source_t source,
                                       uint8_t voice_id,
                                       const sample_stream_snapshot_t *snapshot)
{
    uint8_t index = 0U;
    if ((snapshot == 0)
        || (sample_stream_snapshot_slot_index(source, voice_id, &index) == 0U))
    {
        return 0U;
    }

    sample_stream_snapshot_slot_t *const slot = &g_sample_stream_snapshot_slots[index];
    uint32_t sequence = slot->sequence;
    if ((sequence & 1U) != 0U)
    {
        sequence++;
    }
    slot->sequence = sequence + 1U;
    sample_stream_snapshot_t copy = *snapshot;
    copy.source = (uint8_t)source;
    copy.voice_id = voice_id;
    if (sample_stream_admission_sync_snapshot(source, voice_id, &copy)
        != SAMPLE_STREAM_ADMISSION_OK)
    {
        /* Keep the published slot readable when admission rejects a refresh. */
        slot->sequence = sequence;
        return 0U;
    }
    slot->value = copy;
    slot->sequence = sequence + 2U;
    return 1U;
}

uint8_t sample_stream_snapshot_read(sample_stream_snapshot_source_t source,
                                    uint8_t voice_id,
                                    sample_stream_snapshot_t *out_snapshot)
{
    uint8_t index = 0U;
    if ((out_snapshot == 0)
        || (sample_stream_snapshot_slot_index(source, voice_id, &index) == 0U))
    {
        return 0U;
    }

    const sample_stream_snapshot_slot_t *const slot = &g_sample_stream_snapshot_slots[index];
    for (uint8_t attempt = 0U; attempt < 3U; ++attempt)
    {
        const uint32_t before = slot->sequence;
        if ((before == 0U) || ((before & 1U) != 0U))
        {
            continue;
        }
        const sample_stream_snapshot_t copy = slot->value;
        const uint32_t after = slot->sequence;
        if (before == after)
        {
            *out_snapshot = copy;
            return 1U;
        }
    }
    return 0U;
}

void sample_stream_snapshot_clear(sample_stream_snapshot_source_t source,
                                  uint8_t voice_id)
{
    sample_stream_snapshot_t snapshot;
    sample_stream_snapshot_init(&snapshot);
    snapshot.active = 0U;
    (void)sample_stream_snapshot_publish(source, voice_id, &snapshot);
}

uint8_t sample_stream_snapshot_clear_generation(sample_stream_snapshot_source_t source,
                                                uint8_t voice_id,
                                                uint32_t generation)
{
    sample_stream_snapshot_t current;
    if ((generation == 0U)
        || (sample_stream_snapshot_read(source, voice_id, &current) == 0U)
        || (current.active == 0U)
        || (current.generation != generation))
    {
        return 0U;
    }
    sample_stream_snapshot_clear(source, voice_id);
    return 1U;
}
