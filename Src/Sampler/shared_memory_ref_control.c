#include "IPC/shared_memory_ref_control.h"

#include <stddef.h>
#include <string.h>

#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sample_page_cache_shared_contract.h"

uint8_t shared_memory_ref_make_page_pool(uint16_t first_page_slot,
                                         uint32_t byte_offset,
                                         uint32_t length,
                                         audio_shared_memory_ref_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if ((out == NULL) || (first_page_slot >= SAMPLE_PAGE_MAX_COUNT)) return 0U;
    const uint64_t offset = ((uint64_t)first_page_slot * SAMPLE_PAGE_BYTES)
                            + byte_offset;
    const uint64_t pool_bytes = (uint64_t)SAMPLE_PAGE_MAX_COUNT
                                * SAMPLE_PAGE_BYTES;
    if ((offset > pool_bytes) || ((uint64_t)length > (pool_bytes - offset)))
        return 0U;
    out->offset = (uint32_t)offset;
    out->length = length;
    out->region = AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL;
    return 1U;
}

void *shared_memory_ref_control_resolve_page_pool(
    const audio_shared_memory_ref_t *ref)
{
    const uint32_t pool_bytes = SAMPLE_PAGE_MAX_COUNT * SAMPLE_PAGE_BYTES;
    if ((ref == NULL) || (ref->region != AUDIO_SHARED_REGION_SAMPLE_PAGE_POOL)
        || (ref->offset > pool_bytes)
        || (ref->length > (pool_bytes - ref->offset))) return NULL;
    return &((uint8_t *)g_sample_page_shared_data)[ref->offset];
}
