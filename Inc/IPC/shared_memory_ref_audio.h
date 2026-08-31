#pragma once

#include "IPC/shared_memory_ref.h"

const void *shared_memory_ref_resolve_page_pool(
    const audio_shared_memory_ref_t *ref);
