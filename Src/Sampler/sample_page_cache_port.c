#include "Sampler/sample_page_cache_port.h"

#include <string.h>

#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_stream_fatfs_map.h"

uint8_t sample_page_cache_port_alloc_local(
    uint32_t bytes, sample_page_loader_allocation_t *out)
{
    if (out != NULL) memset(out, 0, sizeof(*out));
    if (out == NULL) return 0U;
    sample_page_raw_allocation_t local;
    if (sample_page_cache_alloc_slot_pool_bytes(bytes, &local) == 0U)
        return 0U;
    out->data = local.data;
    out->capacity_bytes = local.capacity_bytes;
    out->first_slot = local.first_slot;
    out->page_count = local.page_count;
    return 1U;
}

void sample_page_cache_port_release_local(uint16_t first_slot,
                                          uint16_t page_count)
{
    sample_page_cache_release_slot_pool_allocation(first_slot, page_count);
}

void sample_page_cache_port_mark_local_cpu_clean(uint16_t first_slot,
                                                 uint16_t page_count)
{
    sample_page_cache_mark_slot_pool_payload_cpu_clean(first_slot, page_count);
}

void *sample_page_cache_port_resolve_local(
    const sample_page_loader_allocation_t *allocation)
{
    return (allocation != NULL) ? allocation->data : NULL;
}

uint32_t sample_page_cache_port_local_total_bytes(void)
{
    return sample_page_cache_slot_pool_total_bytes();
}

uint32_t sample_page_cache_port_local_free_bytes(void)
{
    return sample_page_cache_slot_pool_free_bytes();
}

static uint8_t sample_page_cache_port_prepare_registration(
    sample_audio_key_t key, const char *path, const wav_info_t *info,
    uint32_t total_frames, uint32_t data_offset, FIL *map_file,
    sample_page_stream_info_t *out)
{
    if ((path == NULL) || (info == NULL) || (out == NULL)
        || (total_frames == 0U)) return 0U;
    if (((key.domain == SAMPLE_AUDIO_DOMAIN_CLASSIC)
         || (key.domain == SAMPLE_AUDIO_DOMAIN_MULTI))
        && (wav_parser_is_canonical_brick_float(info) == 0U)) return 0U;
    memset(out, 0, sizeof(*out));
    out->key = key;
    out->total_frames = total_frames;
    out->data_offset = data_offset;
    out->format = sample_audio_format_from_channels(info->channels);
    out->stride_floats = (uint16_t)sample_audio_format_stride_floats(out->format);
    out->frames_per_page = sample_audio_format_frames_per_page(out->format);
    out->encoding = info->encoding;
    out->canonical_float = wav_parser_is_canonical_brick_float(info);
    if (sample_audio_format_is_valid(out->format) == 0U) return 0U;
    sample_stream_safe_metadata_init_fatfs(key, info, total_frames,
                                           data_offset, &out->stream_safe);
    const uint8_t map_ok = (map_file != NULL)
        ? sample_stream_fatfs_map_build_from_file(map_file, &out->stream_safe)
        : sample_stream_fatfs_map_build_from_path(path, &out->stream_safe);
    const uint64_t data_end = (uint64_t)data_offset + info->data_size;
    if ((map_ok == 0U) || (data_end > out->stream_safe.file_size))
    {
        sample_stream_physical_map_release(&out->stream_safe.physical_map);
        return 0U;
    }
    return 1U;
}

uint8_t sample_page_cache_port_prepare_stream(
    sample_audio_key_t key,
    const char *path,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    FIL *map_file,
    sample_page_stream_info_t *out_stream,
    uint16_t *out_prefill_pages)
{
    if (out_stream != NULL) memset(out_stream, 0, sizeof(*out_stream));
    if (out_prefill_pages != NULL) *out_prefill_pages = 0U;
    sample_page_stream_info_t registration;
    if (sample_page_cache_port_prepare_registration(
            key, path, info, total_frames, data_offset, map_file,
            &registration) == 0U) return 0U;
    if (sample_page_cache_register_prepared_stream(&registration) == 0U)
    {
        sample_stream_physical_map_release(&registration.stream_safe.physical_map);
        return 0U;
    }
    sample_page_stream_info_t installed;
    if (sample_page_cache_get_stream_info_key(key, &installed) == 0U)
    {
        sample_page_cache_clear_key(key);
        return 0U;
    }
    const uint32_t startup_frames = (total_frames < SAMPLE_PAGE_MIN_READY_FRAMES)
        ? total_frames : SAMPLE_PAGE_MIN_READY_FRAMES;
    const uint32_t startup_pages = sample_audio_format_required_page_count(
        installed.format, startup_frames);
    if ((startup_pages == 0U) || (startup_pages > UINT16_MAX))
    {
        sample_page_cache_clear_key(key);
        return 0U;
    }
    for (uint32_t page = 0U; page < startup_pages; ++page)
    {
        if ((sample_page_cache_prepare_reserved_page_key_alloc(
                key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U)
            || (sample_page_cache_mark_static_page_key_alloc(
                key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U))
        {
            sample_page_cache_clear_key(key);
            return 0U;
        }
    }
    if (out_stream != NULL) *out_stream = installed;
    if (out_prefill_pages != NULL) *out_prefill_pages = (uint16_t)startup_pages;
    return 1U;
}

uint8_t sample_page_cache_port_prepare_stream_mapped(
    sample_audio_key_t key,
    const char *path,
    const wav_info_t *info,
    uint32_t total_frames,
    uint32_t data_offset,
    sample_stream_physical_map_t *map,
    uint32_t file_size,
    sample_page_stream_info_t *out_stream,
    uint16_t *out_prefill_pages)
{
    if (out_stream != NULL) memset(out_stream, 0, sizeof(*out_stream));
    if (out_prefill_pages != NULL) *out_prefill_pages = 0U;
    if ((path == NULL) || (info == NULL) || (map == NULL)
        || (map->valid == 0U) || (total_frames == 0U)
        || (wav_parser_is_canonical_brick_float(info) == 0U)) return 0U;
    sample_page_stream_info_t registration;
    memset(&registration, 0, sizeof(registration));
    registration.key = key;
    registration.total_frames = total_frames;
    registration.data_offset = data_offset;
    registration.format = sample_audio_format_from_channels(info->channels);
    registration.stride_floats = (uint16_t)sample_audio_format_stride_floats(
        registration.format);
    registration.frames_per_page = sample_audio_format_frames_per_page(
        registration.format);
    registration.encoding = info->encoding;
    registration.canonical_float = wav_parser_is_canonical_brick_float(info);
    if ((sample_audio_format_is_valid(registration.format) == 0U)
        || ((uint64_t)data_offset + info->data_size > file_size)) return 0U;
    sample_stream_safe_metadata_init_fatfs(key, info, total_frames,
        data_offset, &registration.stream_safe);
    registration.stream_safe.file_size = file_size;
    registration.stream_safe.physical_map = *map;
    memset(map, 0, sizeof(*map));
    map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS; ++i)
        map->pool_block[i] = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    if (sample_page_cache_register_prepared_stream(&registration) == 0U)
    {
        sample_stream_physical_map_release(&registration.stream_safe.physical_map);
        return 0U;
    }
    sample_page_stream_info_t installed;
    if (sample_page_cache_get_stream_info_key(key, &installed) == 0U)
    {
        sample_page_cache_clear_key(key);
        return 0U;
    }
    const uint32_t startup_frames = (total_frames < SAMPLE_PAGE_MIN_READY_FRAMES)
        ? total_frames : SAMPLE_PAGE_MIN_READY_FRAMES;
    const uint32_t startup_pages = sample_audio_format_required_page_count(
        installed.format, startup_frames);
    if ((startup_pages == 0U) || (startup_pages > UINT16_MAX))
    {
        sample_page_cache_clear_key(key);
        return 0U;
    }
    for (uint32_t page = 0U; page < startup_pages; ++page)
    {
        if ((sample_page_cache_prepare_reserved_page_key_alloc(
                key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U)
            || (sample_page_cache_mark_static_page_key_alloc(
                key, page, SAMPLE_PAGE_ALLOC_SLOT_PERMANENT) == 0U))
        {
            sample_page_cache_clear_key(key);
            return 0U;
        }
    }
    if (out_stream != NULL) *out_stream = installed;
    if (out_prefill_pages != NULL) *out_prefill_pages = (uint16_t)startup_pages;
    return 1U;
}

uint8_t sample_page_cache_port_register_file(sample_audio_key_t key,
                                             const char *path,
                                             const wav_info_t *info,
                                             uint32_t total_frames,
                                             uint32_t data_offset,
                                             FIL *map_file)
{
    sample_page_stream_info_t registration;
    if (sample_page_cache_port_prepare_registration(
            key, path, info, total_frames, data_offset, map_file,
            &registration) == 0U) return 0U;
    if (sample_page_cache_register_prepared_stream(&registration) == 0U)
    {
        sample_stream_physical_map_release(&registration.stream_safe.physical_map);
        return 0U;
    }
    return 1U;
}

void sample_page_cache_port_clear(sample_audio_key_t key)
{
    sample_page_cache_clear_key(key);
}
