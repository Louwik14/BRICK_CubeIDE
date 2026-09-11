#include "IPC/sample_classic_audio_projection_contract.h"
#include "Sampler/sample_classic_audio_projection_control.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Platform/intercore_cache.h"
#include "Sampler/sample_cache.h"

void sample_classic_audio_projection_init(void)
{
    memset(g_sample_classic_audio_source, 0, sizeof(g_sample_classic_audio_source));
    intercore_cache_publish(g_sample_classic_audio_source,
                            sizeof(g_sample_classic_audio_source));
}

void sample_classic_audio_projection_withdraw(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY) return;
    sample_classic_audio_source_t *const dst = &g_sample_classic_audio_source[sample_id];
    const uint32_t inactive = dst->active_snapshot ^ 1U;
    sample_classic_audio_snapshot_t *const next = &dst->snapshots[inactive];
    memset(next, 0, sizeof(*next));
    intercore_cache_publish(next, sizeof(*next));
    dst->active_snapshot = inactive;
    intercore_cache_publish((const void *)&dst->active_snapshot,
                            sizeof(dst->active_snapshot));
}

uint8_t sample_classic_audio_projection_publish(uint16_t sample_id)
{
    sample_resolved_source_t source;
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY)
        || (sample_cache_is_ready(sample_id) == 0U)
        || (sample_cache_resolve_classic_source(sample_id, &source) == 0U)) return 0U;
    sample_classic_audio_source_t *const dst = &g_sample_classic_audio_source[sample_id];
    const uint32_t inactive = dst->active_snapshot ^ 1U;
    sample_classic_audio_snapshot_t *const next = &dst->snapshots[inactive];
    next->ready = 0U;
    next->key = source.key;
    next->total_frames = source.total_frames;
    next->data_offset = source.data_offset;
    next->data_size = source.data_size;
    next->sample_rate = source.sample_rate;
    next->registration_epoch = source.registration_epoch;
    next->format = source.format;
    next->channels = source.channels;
    next->bits_per_sample = source.bits_per_sample;
    next->block_align = source.block_align;
    next->stride_floats = source.stride_floats;
    next->frames_per_page = source.frames_per_page;
    next->ready = 1U;
    intercore_cache_publish(next, sizeof(*next));
    dst->active_snapshot = inactive;
    intercore_cache_publish((const void *)&dst->active_snapshot,
                            sizeof(dst->active_snapshot));
    return 1U;
}
