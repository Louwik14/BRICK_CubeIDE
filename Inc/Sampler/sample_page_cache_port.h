#pragma once

#include <stdint.h>

#include "Sampler/sample_page_cache.h"
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

/* Common Classic/Multi preparation boundary.  It registers the immutable
 * stream metadata/link-map and reserves the product startup pages, but leaves
 * their asynchronous fill to the Stream Manager. */
uint8_t sample_page_cache_port_prepare_stream(
    sample_audio_key_t key,
    const char *path,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    FIL *map_file,
    sample_page_stream_info_t *out_stream,
    uint16_t *out_prefill_pages);
uint8_t sample_page_cache_port_prepare_stream_mapped(
    sample_audio_key_t key,
    const char *path,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    sample_stream_physical_map_t *map,
    uint32_t file_size,
    sample_page_stream_info_t *out_stream,
    uint16_t *out_prefill_pages);
/* REC_SOURCE keeps its own startup-page lifecycle. */
uint8_t sample_page_cache_port_register_file(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset,
                                             FIL *map_file);
void sample_page_cache_port_clear(sample_audio_key_t key);
