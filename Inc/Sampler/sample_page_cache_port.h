#pragma once

#include <stdint.h>

#include "Sampler/sample_stream_io.h"
#include "Audio/audio_shared_memory.h"
#include "ff.h"

typedef struct
{
    audio_shared_memory_ref_t data;
    uint32_t capacity_bytes;
    uint16_t first_slot;
    uint16_t page_count;
} sample_page_loader_allocation_t;

uint8_t sample_page_cache_port_alloc_shared(
    uint32_t bytes, sample_page_loader_allocation_t *out);
void sample_page_cache_port_release_shared(uint16_t first_slot,
                                           uint16_t page_count);
void *sample_page_cache_port_resolve_shared(
    const sample_page_loader_allocation_t *allocation);
uint32_t sample_page_cache_port_shared_total_bytes(void);
uint32_t sample_page_cache_port_shared_free_bytes(void);

/* H743 local adapter for the future M4-loader <-> M7-page-owner boundary.
 * Loader clients never receive a page pointer and only exchange immutable
 * registration, load command and completion values. */
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
                                            uint8_t pin,
                                            sample_stream_io_command_t *out_command);
uint8_t sample_page_cache_port_reserve_pin(sample_audio_key_t key,
                                           uint32_t page_index,
                                           sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_port_reserve(sample_audio_key_t key,
                                       uint32_t page_index,
                                       sample_page_alloc_type_t alloc_type);
uint8_t sample_page_cache_port_complete(const sample_stream_io_result_t *result);
void sample_page_cache_port_abort(const sample_stream_io_command_t *command);
void sample_page_cache_port_clear(sample_audio_key_t key);
sample_page_load_result_t sample_page_cache_port_load_full(
    sample_audio_key_t key,
    const char *path,
    FIL *map_file,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    sample_page_alloc_type_t alloc_type);
