#pragma once

#include <stdint.h>
#include "IPC/shared_memory_ref.h"

const void *audio_shared_memory_resolve(const audio_shared_memory_ref_t *ref);
/* AUDIO consumer resolution for cacheable bulk payload. */
const void *audio_shared_memory_consume(const audio_shared_memory_ref_t *ref);
