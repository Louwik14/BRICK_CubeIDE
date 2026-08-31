#pragma once

#include "IPC/shared_memory_ref.h"

uint8_t shared_memory_ref_make_page_pool(uint16_t first_page_slot,
                                         uint32_t byte_offset,
                                         uint32_t length,
                                         audio_shared_memory_ref_t *out);
void *shared_memory_ref_control_resolve_page_pool(
    const audio_shared_memory_ref_t *ref);
