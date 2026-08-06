#include "Sampler/sample_stream_request_queue.h"

#include <string.h>

#include "Storage/memory_layout.h"

SDRAM_STREAM_SERVICE static sample_stream_request_entry_t
    g_sample_stream_request_queue[SAMPLE_STREAM_REQUEST_QUEUE_CAPACITY];

void sample_stream_request_queue_init(void)
{
    memset(g_sample_stream_request_queue, 0, sizeof(g_sample_stream_request_queue));
}

sample_stream_request_entry_t *sample_stream_request_queue_entries(void)
{
    return g_sample_stream_request_queue;
}

const sample_stream_request_entry_t *sample_stream_request_queue_entries_const(void)
{
    return g_sample_stream_request_queue;
}

sample_stream_request_publish_result_t sample_stream_request_queue_publish(
    const sample_stream_request_contract_t *request,
    const sample_stream_request_geometry_t *geometry,
    sample_stream_request_entry_t **out_entry)
{
    if (out_entry != 0)
    {
        *out_entry = 0;
    }
    if ((request == 0) || (geometry == 0))
    {
        return SAMPLE_STREAM_REQUEST_FULL;
    }

    const sample_audio_key_t request_key = {
        .domain = (sample_audio_domain_t)request->domain,
        .object_id = request->object_id,
    };
    sample_stream_request_entry_t *free_entry = 0;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_REQUEST_QUEUE_CAPACITY; ++i)
    {
        sample_stream_request_entry_t *const entry = &g_sample_stream_request_queue[i];
        if (entry->active == 0U)
        {
            if (free_entry == 0)
            {
                free_entry = entry;
            }
            continue;
        }
        if ((sample_audio_key_equal(&entry->key, &request_key) == 0U)
            || (entry->page_index != request->page_index))
        {
            continue;
        }

        const sample_stream_audio_frame_t previous_deadline =
            entry->consume_deadline_audio_frame;
        if (request->flags > entry->priority)
        {
            entry->priority = request->flags;
        }
        if (request->consume_deadline_audio_frame < entry->consume_deadline_audio_frame)
        {
            entry->consume_deadline_audio_frame = request->consume_deadline_audio_frame;
        }
        if ((request->owner_kind != 0U)
            && ((entry->owner_kind == 0U)
                || (request->consume_deadline_audio_frame <= previous_deadline)))
        {
            entry->owner_kind = request->owner_kind;
            entry->owner_id = request->owner_id;
            entry->owner_generation = request->owner_generation;
            entry->role = request->role;
        }
        if (out_entry != 0)
        {
            *out_entry = entry;
        }
        return SAMPLE_STREAM_REQUEST_MERGED;
    }

    if (free_entry == 0)
    {
        return SAMPLE_STREAM_REQUEST_FULL;
    }
    memset(free_entry, 0, sizeof(*free_entry));
    free_entry->key.domain = (sample_audio_domain_t)request->domain;
    free_entry->key.object_id = request->object_id;
    free_entry->sample_id = request->object_id;
    free_entry->format = geometry->format;
    free_entry->stride_floats = geometry->stride_floats;
    free_entry->frames_per_page = geometry->frames_per_page;
    free_entry->registration_epoch = request->registration_epoch;
    free_entry->page_index = request->page_index;
    free_entry->created_audio_frame = request->created_audio_frame;
    free_entry->consume_deadline_audio_frame = request->consume_deadline_audio_frame;
    free_entry->owner_generation = request->owner_generation;
    free_entry->priority = request->flags;
    free_entry->owner_kind = request->owner_kind;
    free_entry->owner_id = request->owner_id;
    free_entry->role = request->role;
    free_entry->active = 1U;
    if (out_entry != 0)
    {
        *out_entry = free_entry;
    }
    return SAMPLE_STREAM_REQUEST_INSERTED;
}
