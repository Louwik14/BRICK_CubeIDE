#include "Audio/audio_shared_memory.h"
#include "IPC/shared_memory_ref_audio.h"

#include <stddef.h>
#include "Platform/intercore_cache.h"

const void *audio_shared_memory_resolve(const audio_shared_memory_ref_t *ref)
{
    return shared_memory_ref_resolve_page_pool(ref);
}

const void *audio_shared_memory_consume(const audio_shared_memory_ref_t *ref)
{
    const void *const data = audio_shared_memory_resolve(ref);
    if (data != NULL)
        intercore_cache_consume(data, ref->length);
    return data;
}
