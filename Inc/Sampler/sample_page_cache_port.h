#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_io.h"
#include "ff.h"

typedef struct
{
    void *data;
    uint32_t capacity_bytes;
    uint16_t first_slot;
    uint16_t page_count;
} sample_page_loader_allocation_t;

uint8_t sample_page_cache_port_alloc_local(
    uint32_t bytes, sample_page_loader_allocation_t *out);
void sample_page_cache_port_release_local(uint16_t first_slot,
                                          uint16_t page_count);
void sample_page_cache_port_mark_local_cpu_clean(uint16_t first_slot,
                                                 uint16_t page_count);
void *sample_page_cache_port_resolve_local(
    const sample_page_loader_allocation_t *allocation);
uint32_t sample_page_cache_port_local_total_bytes(void);
uint32_t sample_page_cache_port_local_free_bytes(void);

/* H743 local adapter between loader clients and the page-cache owner. */
uint8_t sample_page_cache_port_register_path(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset);
uint8_t sample_page_cache_port_register_file(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset,
                                             FIL *map_file);
uint8_t sample_page_cache_port_prepare_page(sample_audio_key_t key,
                                            uint32_t page_index,
                                            sample_page_alloc_type_t alloc_type,
                                            uint8_t static_resident,
                                            sample_page_load_token_t *out_token);
uint8_t sample_page_cache_port_reserve_static(sample_audio_key_t key,
                                           uint32_t page_index,
                                           sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_port_reserve(sample_audio_key_t key,
                                       uint32_t page_index,
                                       sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_port_complete(const sample_stream_io_result_t *result);
void sample_page_cache_port_abort(const sample_page_load_token_t *token);
void sample_page_cache_port_clear(sample_audio_key_t key);
