#include "Sampler/sample_page_cache_port.h"

#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Audio/audio_shared_memory.h"
#include "Sampler/sample_stream_fatfs_map.h"
#include "Sampler/sample_stream_publish.h"
#include "Sampler/sample_stream_transport.h"

uint8_t sample_page_cache_port_alloc_shared(
    uint32_t bytes, sample_page_loader_allocation_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (out == NULL) return 0U;
    sample_page_raw_allocation_t local;
    if (sample_page_cache_alloc_slot_pool_bytes(bytes, &local) == 0U)
        return 0U;
    if (audio_shared_memory_page_ref(local.first_slot, 0U, bytes,
                                     &out->data) == 0U)
    {
        sample_page_cache_release_slot_pool_allocation(local.first_slot,
                                                       local.page_count);
        return 0U;
    }
    out->capacity_bytes = local.capacity_bytes;
    out->first_slot = local.first_slot;
    out->page_count = local.page_count;
    return 1U;
}

void sample_page_cache_port_release_shared(uint16_t first_slot,
                                           uint16_t page_count)
{
    sample_page_cache_release_slot_pool_allocation(first_slot, page_count);
}

void *sample_page_cache_port_resolve_shared(
    const sample_page_loader_allocation_t *allocation)
{
    return (allocation != NULL)
        ? (void *)audio_shared_memory_resolve(&allocation->data) : NULL;
}

uint32_t sample_page_cache_port_shared_total_bytes(void)
{
    return sample_page_cache_slot_pool_total_bytes();
}

uint32_t sample_page_cache_port_shared_free_bytes(void)
{
    return sample_page_cache_slot_pool_free_bytes();
}

static uint8_t sample_page_cache_port_prepare_registration(
    sample_audio_key_t key, const char *path, const wav_info_t *info,
    uint32_t total_frames, uint32_t data_offset, FIL *map_file,
    uint8_t allow_fatfs,
    sample_page_stream_info_t *out)
{
    if ((path == NULL) || (info == NULL) || (out == NULL)
        || (total_frames == 0U)) return 0U;
    memset(out, 0, sizeof(*out));
    out->key = key;
    out->info = *info;
    out->total_frames = total_frames;
    out->data_offset = data_offset;
    out->format = sample_audio_format_from_channels(info->channels);
    out->stride_floats = (uint16_t)sample_audio_format_stride_floats(out->format);
    out->frames_per_page = sample_audio_format_frames_per_page(out->format);
    if (sample_audio_format_is_valid(out->format) == 0U) return 0U;
    if (strlen(path) >= sizeof(out->path)) return 0U;
    memcpy(out->path, path, strlen(path) + 1U);
    sample_stream_safe_metadata_init_fatfs(key, info, total_frames,
                                           data_offset, &out->stream_safe);
    const uint8_t map_ok = (map_file != NULL)
        ? sample_stream_fatfs_map_build_from_file(map_file, &out->stream_safe)
        : sample_stream_fatfs_map_build_from_path(path, &out->stream_safe);
    const uint64_t data_end = (uint64_t)data_offset + info->data_size;
    if (((map_ok == 0U) && (allow_fatfs == 0U))
        || ((map_ok != 0U) && (data_end > out->stream_safe.file_size)))
    {
        sample_stream_physical_map_release(&out->stream_safe.physical_map);
        return 0U;
    }
    out->physical_only = (map_ok != 0U) ? 1U : 0U;
    return 1U;
}

static uint8_t sample_page_cache_port_install(sample_audio_key_t key,
                                              const char *path,
                                              const wav_info_t *info,
                                              uint32_t total_frames,
                                              uint32_t data_offset,
                                              FIL *map_file)
{
    sample_page_stream_info_t registration;
    if (sample_page_cache_port_prepare_registration(
            key, path, info, total_frames, data_offset, map_file, 0U,
            &registration) == 0U) return 0U;
    if (sample_page_cache_register_prepared_stream(&registration) == 0U)
    {
        sample_stream_physical_map_release(&registration.stream_safe.physical_map);
        return 0U;
    }
    return 1U;
}

uint8_t sample_page_cache_port_register_path(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset)
{
    return sample_page_cache_port_install(key, path, info, total_frames,
                                          data_offset, NULL);
}

uint8_t sample_page_cache_port_register_file(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset,
                                             FIL *map_file)
{
    return sample_page_cache_port_install(key, path, info, total_frames,
                                          data_offset, map_file);
}

uint8_t sample_page_cache_port_prepare_page(sample_audio_key_t key,
                                            uint32_t page_index,
                                            sample_page_alloc_type_t alloc_type,
                                            uint8_t pin,
                                            sample_stream_io_command_t *out_command)
{
    if (out_command == NULL) return 0U;
    memset(out_command, 0, sizeof(*out_command));
    if ((sample_page_cache_prepare_bulk_page_key_alloc(key, page_index,
                                                       alloc_type) == 0U)
        || ((pin != 0U) && (sample_page_cache_pin_page_key_alloc(
                                key, page_index, alloc_type) == 0U))) return 0U;
    sample_page_load_target_t target;
    sample_page_stream_info_t stream_info;
    sample_page_load_token_t token;
    if ((sample_page_cache_get_bulk_load_target_key(key, page_index, &target) == 0U)
        || (sample_page_cache_get_stream_info_key(key, &stream_info) == 0U)
        || (sample_page_cache_begin_loading(&target, &token) == 0U)
        || (sample_stream_io_command_init(out_command, &token, &target,
                                          &stream_info) == 0U)) return 0U;
    return 1U;
}

uint8_t sample_page_cache_port_reserve_pin(sample_audio_key_t key,
                                           uint32_t page_index,
                                           sample_page_alloc_type_t alloc_type)
{
    return (uint8_t)((sample_page_cache_prepare_bulk_page_key_alloc(
                          key, page_index, alloc_type) != 0U)
        && (sample_page_cache_pin_page_key_alloc(
                key, page_index, alloc_type) != 0U));
}

uint8_t sample_page_cache_port_reserve(sample_audio_key_t key,
                                       uint32_t page_index,
                                       sample_page_alloc_type_t alloc_type)
{
    return sample_page_cache_prepare_bulk_page_key_alloc(
        key, page_index, alloc_type);
}

uint8_t sample_page_cache_port_complete(const sample_stream_io_result_t *result)
{
    return sample_stream_publish_result(result);
}

void sample_page_cache_port_abort(const sample_stream_io_command_t *command)
{
    if (command != NULL)
        (void)sample_page_cache_finish_loading(&command->token,
                                               SAMPLE_PAGE_FINISH_ERROR);
}

void sample_page_cache_port_clear(sample_audio_key_t key)
{
    sample_page_cache_clear_key(key);
}

sample_page_load_result_t sample_page_cache_port_load_full(
    sample_audio_key_t key, const char *path, FIL *map_file,
    const wav_info_t *info, uint32_t total_frames, uint32_t data_offset,
    sample_page_alloc_type_t alloc_type)
{
    sample_page_stream_info_t registration;
    if (sample_page_cache_port_prepare_registration(
            key, path, info, total_frames, data_offset, map_file, 1U,
            &registration) == 0U) return SAMPLE_PAGE_LOAD_INVALID_ARG;
    uint32_t page_count = 0U;
    if (sample_page_cache_begin_full_reservation(&registration, alloc_type,
                                                 &page_count) == 0U)
    {
        sample_stream_physical_map_release(&registration.stream_safe.physical_map);
        return SAMPLE_PAGE_LOAD_NO_SPACE;
    }
    if (sample_page_cache_get_stream_info_key(key, &registration) == 0U)
    { sample_page_cache_port_clear(key); return SAMPLE_PAGE_LOAD_INVALID_ARG; }
    for (uint32_t page = 0U; page < page_count; ++page)
    {
        sample_stream_io_command_t command;
        sample_page_load_target_t target;
        sample_page_load_token_t token;
        if ((sample_page_cache_get_bulk_load_target_key(key, page, &target) == 0U)
            || (sample_page_cache_begin_loading(&target, &token) == 0U)
            || (sample_stream_io_command_init(&command, &token, &target,
                                              &registration) == 0U))
        { sample_page_cache_port_clear(key); return SAMPLE_PAGE_LOAD_INVALID_ARG; }
        command.deadline_margin_us = UINT32_MAX;
        sample_stream_io_result_t result;
        sample_stream_transport_execute_monocore(&command, &result);
        if ((result.load_result != SAMPLE_PAGE_LOAD_OK)
            || (sample_page_cache_port_complete(&result) == 0U))
        { sample_page_cache_port_clear(key); return result.load_result; }
    }
    if (sample_page_cache_finish_full_reservation(key) == 0U)
    { sample_page_cache_port_clear(key); return SAMPLE_PAGE_LOAD_INVALID_ARG; }
    return SAMPLE_PAGE_LOAD_OK;
}
