#include "Audio/audio_shared_memory.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Platform/intercore_cache.h"

uint8_t audio_shared_memory_page_ref(uint16_t first_page_slot,
                                     uint32_t byte_offset,
                                     uint32_t length,
                                     audio_shared_memory_ref_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL)
        || (sample_page_cache_slot_pool_offset(first_page_slot, byte_offset,
                                               length, &out->offset) == 0U))
        return 0U;
    out->length = length;
    out->region = AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL;
    return 1U;
}

const void *audio_shared_memory_resolve(const audio_shared_memory_ref_t *ref)
{
    if ((ref == NULL) || (ref->region != AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL))
        return NULL;
    return sample_page_cache_slot_pool_resolve(ref->offset, ref->length);
}

const void *audio_shared_memory_consume(const audio_shared_memory_ref_t *ref)
{
    const void *const data = audio_shared_memory_resolve(ref);
    if (data != NULL)
        intercore_cache_consume(data, ref->length);
    return data;
}
