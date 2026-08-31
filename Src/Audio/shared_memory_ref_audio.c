#include "IPC/shared_memory_ref_audio.h"

#include <stddef.h>

#include "Sampler/sample_page_cache_shared_contract.h"

const void *shared_memory_ref_resolve_page_pool(
    const audio_shared_memory_ref_t *ref)
{
    const uint32_t pool_bytes = SAMPLE_PAGE_MAX_COUNT * SAMPLE_PAGE_BYTES;
    if ((ref == NULL) || (ref->region != AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL)
        || (ref->offset > pool_bytes)
        || (ref->length > (pool_bytes - ref->offset))) return NULL;
    return &((const uint8_t *)g_sample_page_shared_data)[ref->offset];
}
