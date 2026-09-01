#include "IPC/sample_classic_audio_projection_contract.h"
#include "Sampler/sample_classic_audio_projection_control.h"

#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/sample_cache.h"
#include "stm32h7xx.h"

void sample_classic_audio_projection_init(void)
{
    memset(g_sample_classic_audio_source, 0, sizeof(g_sample_classic_audio_source));
    __DMB();
}

void sample_classic_audio_projection_withdraw(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY) return;
    sample_classic_audio_source_t *const dst = &g_sample_classic_audio_source[sample_id];
    dst->seq++;
    __DMB();
    dst->ready = 0U;
    __DMB();
    dst->seq++;
}

uint8_t sample_classic_audio_projection_publish(uint16_t sample_id)
{
    sample_resolved_source_t source;
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY)
        || (sample_cache_is_ready(sample_id) == 0U)
        || (sample_cache_resolve_classic_source(sample_id, &source) == 0U)) return 0U;
    sample_classic_audio_source_t *const dst = &g_sample_classic_audio_source[sample_id];
    dst->seq++;
    __DMB();
    dst->ready = 0U;
    dst->key = source.key;
    dst->total_frames = source.total_frames;
    dst->data_offset = source.data_offset;
    dst->data_size = source.data_size;
    dst->sample_rate = source.sample_rate;
    dst->registration_epoch = source.registration_epoch;
    dst->format = source.format;
    dst->channels = source.channels;
    dst->bits_per_sample = source.bits_per_sample;
    dst->block_align = source.block_align;
    dst->stride_floats = source.stride_floats;
    dst->frames_per_page = source.frames_per_page;
    dst->ready = 1U;
    __DMB();
    dst->seq++;
    return 1U;
}
