#include "Sampler/sample_stream_fatfs_map.h"

#include <string.h>

#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"

#include "ff.h"

#define SAMPLE_STREAM_FATFS_SECTOR_SIZE (512U)
#define SAMPLE_STREAM_FATFS_CLMT_ITEMS  \
    (2U + (2U * SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS))

typedef struct
{
    uint16_t next;
    uint16_t used;
    uint32_t owner_generation;
    sample_stream_physical_extent_t extents[SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK];
} sample_stream_physical_pool_block_t;

SDRAM_STREAM_SERVICE static sample_stream_physical_pool_block_t
    g_sample_stream_physical_pool[SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS];
SDRAM_STREAM_SCRATCH static DWORD g_sample_stream_clmt_scratch[SAMPLE_STREAM_FATFS_CLMT_ITEMS];
static uint32_t g_sample_stream_physical_generation;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sample_stream_physical_extent_t) == 12U,
               "physical extent budget changed");
_Static_assert(sizeof(sample_stream_physical_map_t) == 28U,
               "physical map header budget changed");
_Static_assert(sizeof(sample_stream_physical_pool_block_t) == 104U,
               "physical extent pool block budget changed");
#endif

static uint32_t sample_stream_physical_next_generation(void)
{
    g_sample_stream_physical_generation++;
    if (g_sample_stream_physical_generation == 0U)
    {
        g_sample_stream_physical_generation = 1U;
    }
    return g_sample_stream_physical_generation;
}

void sample_stream_physical_map_pool_reset(void)
{
    memset(g_sample_stream_physical_pool, 0, sizeof(g_sample_stream_physical_pool));
    (void)sample_stream_physical_next_generation();
}

void sample_stream_physical_map_release(sample_stream_physical_map_t *map)
{
    if (map == 0)
    {
        return;
    }

    if (map->generation == 0U)
    {
        memset(map, 0, sizeof(*map));
        map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
        return;
    }

    uint16_t block = map->first_pool_block;
    uint16_t visited = 0U;
    while ((block < SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
           && (visited < SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS))
    {
        sample_stream_physical_pool_block_t *const entry =
            &g_sample_stream_physical_pool[block];
        const uint16_t next = entry->next;
        if (entry->owner_generation == map->generation)
        {
            memset(entry, 0, sizeof(*entry));
        }
        block = next;
        visited++;
    }
    memset(map, 0, sizeof(*map));
    map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
}

static int32_t sample_stream_physical_pool_allocate(uint32_t owner_generation)
{
    for (uint16_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS; ++i)
    {
        if (g_sample_stream_physical_pool[i].owner_generation == 0U)
        {
            sample_stream_physical_pool_block_t *const block =
                &g_sample_stream_physical_pool[i];
            memset(block, 0, sizeof(*block));
            block->next = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
            block->owner_generation = owner_generation;
            return (int32_t)i;
        }
    }
    return -1;
}

static uint8_t sample_stream_physical_map_append(sample_stream_physical_map_t *map,
                                                 const sample_stream_physical_extent_t *extent)
{
    if ((map == 0) || (extent == 0) || (extent->sector_count == 0U)
        || (map->extent_count >= SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS))
    {
        return 0U;
    }
    if (map->extent_count == 0U)
    {
        map->inline_extent = *extent;
        map->extent_count = 1U;
        return 1U;
    }

    const uint16_t overflow_index = (uint16_t)(map->extent_count - 1U);
    const uint16_t required_block =
        (uint16_t)(overflow_index / SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK);
    uint16_t block_index = map->first_pool_block;
    uint16_t previous = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    for (uint16_t ordinal = 0U; ordinal <= required_block; ++ordinal)
    {
        if (block_index >= SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
        {
            const int32_t allocated =
                sample_stream_physical_pool_allocate(map->generation);
            if (allocated < 0)
            {
                return 0U;
            }
            block_index = (uint16_t)allocated;
            if (previous < SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
            {
                g_sample_stream_physical_pool[previous].next = block_index;
            }
            else
            {
                map->first_pool_block = block_index;
            }
        }
        if (ordinal == required_block)
        {
            break;
        }
        if (g_sample_stream_physical_pool[block_index].owner_generation != map->generation)
        {
            return 0U;
        }
        previous = block_index;
        block_index = g_sample_stream_physical_pool[block_index].next;
    }

    sample_stream_physical_pool_block_t *const block =
        &g_sample_stream_physical_pool[block_index];
    if ((block->owner_generation != map->generation)
        || (block->used >= SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK))
    {
        return 0U;
    }
    block->extents[block->used++] = *extent;
    map->extent_count++;
    return 1U;
}

uint8_t sample_stream_physical_map_import(
    sample_stream_physical_map_t *map,
    const sample_stream_physical_extent_t *extents,
    uint16_t extent_count,
    uint32_t media_epoch)
{
    if ((map == 0) || (extents == 0) || (extent_count == 0U)
            || (extent_count > SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS)
            || (media_epoch == 0U))
    {
        return 0U;
    }
    sample_stream_physical_map_release(map);
    map->generation = sample_stream_physical_next_generation();
    map->media_epoch = media_epoch;
    map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    for (uint16_t i = 0U; i < extent_count; ++i)
    {
        if (sample_stream_physical_map_append(map, &extents[i]) == 0U)
        {
            sample_stream_physical_map_release(map);
            return 0U;
        }
    }
    map->valid = 1U;
    return 1U;
}

uint8_t sample_stream_physical_map_is_current(const sample_stream_physical_map_t *map)
{
    return ((map != 0) && (map->valid != 0U) && (map->extent_count != 0U)
            && (map->generation != 0U)
            && (map->media_epoch == sd_access_media_epoch())) ? 1U : 0U;
}

sample_stream_backend_kind_t sample_stream_safe_metadata_backend(
    const sample_stream_safe_metadata_t *metadata)
{
    if ((metadata != 0)
        && (sample_stream_physical_map_is_current(&metadata->physical_map) != 0U))
    {
        return SAMPLE_STREAM_BACKEND_PHYSICAL;
    }
    return SAMPLE_STREAM_BACKEND_FATFS;
}

uint8_t sample_stream_physical_map_get_extent(const sample_stream_physical_map_t *map,
                                              uint16_t extent_index,
                                              sample_stream_physical_extent_t *out_extent)
{
    if ((out_extent == 0) || (sample_stream_physical_map_is_current(map) == 0U)
        || (extent_index >= map->extent_count))
    {
        return 0U;
    }
    if (extent_index == 0U)
    {
        *out_extent = map->inline_extent;
        return 1U;
    }

    uint16_t overflow_index = (uint16_t)(extent_index - 1U);
    uint16_t block_index = map->first_pool_block;
    while ((overflow_index >= SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK)
           && (block_index < SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS))
    {
        const sample_stream_physical_pool_block_t *const block =
            &g_sample_stream_physical_pool[block_index];
        if (block->owner_generation != map->generation)
        {
            return 0U;
        }
        overflow_index = (uint16_t)(overflow_index
                         - SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK);
        block_index = block->next;
    }
    if (block_index >= SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
    {
        return 0U;
    }
    const sample_stream_physical_pool_block_t *const block =
        &g_sample_stream_physical_pool[block_index];
    if ((block->owner_generation != map->generation) || (overflow_index >= block->used))
    {
        return 0U;
    }
    *out_extent = block->extents[overflow_index];
    return 1U;
}

static uint8_t sample_stream_physical_extent_contains(
    const sample_stream_physical_extent_t *extent,
    uint32_t file_sector)
{
    if (extent == 0)
    {
        return 0U;
    }
    const uint64_t extent_end = (uint64_t)extent->file_sector_start
                              + (uint64_t)extent->sector_count;
    return ((file_sector >= extent->file_sector_start)
            && ((uint64_t)file_sector < extent_end)) ? 1U : 0U;
}

uint8_t sample_stream_physical_map_resolve(const sample_stream_physical_map_t *map,
                                           uint64_t file_byte_offset,
                                           uint32_t requested_bytes,
                                           sample_stream_physical_cursor_t *cursor,
                                           sample_stream_physical_span_t *out_span)
{
    if ((out_span == 0) || (requested_bytes == 0U)
        || (sample_stream_physical_map_is_current(map) == 0U))
    {
        return 0U;
    }
    memset(out_span, 0, sizeof(*out_span));

    const uint64_t file_sector_64 = file_byte_offset / SAMPLE_STREAM_FATFS_SECTOR_SIZE;
    if (file_sector_64 > UINT32_MAX)
    {
        return 0U;
    }
    const uint32_t file_sector = (uint32_t)file_sector_64;
    uint16_t extent_index = 0U;
    sample_stream_physical_extent_t extent;
    uint8_t found = 0U;

    if ((cursor != 0) && (cursor->map_generation == map->generation)
        && (cursor->extent_index < map->extent_count)
        && (sample_stream_physical_map_get_extent(map, cursor->extent_index, &extent) != 0U))
    {
        extent_index = cursor->extent_index;
        if (sample_stream_physical_extent_contains(&extent, file_sector) != 0U)
        {
            found = 1U;
        }
        else if (file_sector >= extent.file_sector_start)
        {
            while (++extent_index < map->extent_count)
            {
                if (sample_stream_physical_map_get_extent(map, extent_index, &extent) == 0U)
                {
                    return 0U;
                }
                if (sample_stream_physical_extent_contains(&extent, file_sector) != 0U)
                {
                    found = 1U;
                    break;
                }
                if (file_sector < extent.file_sector_start)
                {
                    break;
                }
            }
        }
    }

    if (found == 0U)
    {
        uint16_t low = 0U;
        uint16_t high = map->extent_count;
        while (low < high)
        {
            const uint16_t mid = (uint16_t)(low + ((high - low) / 2U));
            if (sample_stream_physical_map_get_extent(map, mid, &extent) == 0U)
            {
                return 0U;
            }
            if (file_sector < extent.file_sector_start)
            {
                high = mid;
            }
            else if (sample_stream_physical_extent_contains(&extent, file_sector) == 0U)
            {
                low = (uint16_t)(mid + 1U);
            }
            else
            {
                extent_index = mid;
                found = 1U;
                break;
            }
        }
    }
    if (found == 0U)
    {
        return 0U;
    }

    const uint32_t sector_in_extent = file_sector - extent.file_sector_start;
    const uint32_t sectors_available = extent.sector_count - sector_in_extent;
    const uint16_t first_skip = (uint16_t)(file_byte_offset
                                & (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U));
    const uint64_t extent_bytes_available =
        ((uint64_t)sectors_available * SAMPLE_STREAM_FATFS_SECTOR_SIZE) - first_skip;
    const uint32_t logical_bytes = (extent_bytes_available < requested_bytes)
        ? (uint32_t)extent_bytes_available
        : requested_bytes;
    const uint32_t sector_count = (first_skip + logical_bytes
        + (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U)) / SAMPLE_STREAM_FATFS_SECTOR_SIZE;
    if ((logical_bytes == 0U) || (sector_count == 0U)
        || (sector_count > sectors_available))
    {
        return 0U;
    }

    out_span->lba = extent.lba_start + sector_in_extent;
    out_span->sector_count = sector_count;
    out_span->logical_bytes = logical_bytes;
    out_span->first_sector_skip = first_skip;
    out_span->extent_index = extent_index;
    if (cursor != 0)
    {
        cursor->map_generation = map->generation;
        cursor->extent_index = extent_index;
        cursor->reserved = 0U;
    }
    return 1U;
}

void sample_stream_safe_metadata_init_fatfs(sample_audio_key_t key,
                                            const wav_info_t *info,
                                            uint32_t total_frames,
                                            uint32_t data_offset,
                                            sample_stream_safe_metadata_t *out_meta)
{
    if (out_meta == 0)
    {
        return;
    }

    memset(out_meta, 0, sizeof(*out_meta));
    out_meta->physical_map.first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    out_meta->key = key;
    out_meta->data_offset_bytes = data_offset;
    out_meta->total_frames = total_frames;
    out_meta->sector_size = SAMPLE_STREAM_FATFS_SECTOR_SIZE;
    if (info != 0)
    {
        out_meta->block_align = info->block_align;
        out_meta->bytes_per_frame = info->block_align;
        out_meta->channels = info->channels;
        out_meta->bits_per_sample = info->bits_per_sample;
        out_meta->sample_rate = info->sample_rate;
        out_meta->data_size = info->data_size;
        out_meta->data_sector_offset = (uint16_t)(data_offset & (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U));
    }
}

uint8_t sample_stream_fatfs_map_build_from_file(FIL *fp,
                                                sample_stream_safe_metadata_t *out_meta)
{
    if ((fp == 0) || (out_meta == 0) || (fp->obj.fs == 0)
        || (fp->obj.fs->csize == 0U) || (fp->obj.sclust < 2U))
    {
        return 0U;
    }

    const FSIZE_t file_size_fs = f_size(fp);
    if ((file_size_fs == 0U) || (file_size_fs > (FSIZE_t)UINT32_MAX))
    {
        return 0U;
    }

    sample_stream_physical_map_release(&out_meta->physical_map);
    sample_stream_physical_map_t *const map = &out_meta->physical_map;
    map->generation = sample_stream_physical_next_generation();
    map->media_epoch = sd_access_media_epoch();
    map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;

    memset(g_sample_stream_clmt_scratch, 0, sizeof(g_sample_stream_clmt_scratch));
    g_sample_stream_clmt_scratch[0] = SAMPLE_STREAM_FATFS_CLMT_ITEMS;
    DWORD *const previous_cltbl = fp->cltbl;
    fp->cltbl = g_sample_stream_clmt_scratch;
    const FRESULT map_fr = f_lseek(fp, CREATE_LINKMAP);
    fp->cltbl = previous_cltbl;
    if (map_fr != FR_OK)
    {
        sample_stream_physical_map_release(map);
        return 0U;
    }

    const uint32_t file_size = (uint32_t)file_size_fs;
    uint32_t remaining_sectors = (uint32_t)(
        ((uint64_t)file_size + (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U))
        / SAMPLE_STREAM_FATFS_SECTOR_SIZE);
    uint32_t file_sector = 0U;
    uint32_t item = 1U;
    while ((item + 1U < SAMPLE_STREAM_FATFS_CLMT_ITEMS)
           && (g_sample_stream_clmt_scratch[item] != 0U)
           && (remaining_sectors != 0U))
    {
        const uint32_t cluster_count = g_sample_stream_clmt_scratch[item++];
        const uint32_t first_cluster = g_sample_stream_clmt_scratch[item++];
        const uint64_t run_sectors_64 =
            (uint64_t)cluster_count * (uint64_t)fp->obj.fs->csize;
        if ((first_cluster < 2U) || (run_sectors_64 == 0U)
            || (run_sectors_64 > UINT32_MAX))
        {
            sample_stream_physical_map_release(map);
            return 0U;
        }
        uint32_t run_sectors = (uint32_t)run_sectors_64;
        if (run_sectors > remaining_sectors)
        {
            run_sectors = remaining_sectors;
        }
        const uint64_t lba_64 = (uint64_t)fp->obj.fs->database
            + ((uint64_t)(first_cluster - 2U) * (uint64_t)fp->obj.fs->csize);
        if (lba_64 > UINT32_MAX)
        {
            sample_stream_physical_map_release(map);
            return 0U;
        }
        const sample_stream_physical_extent_t extent = {
            .file_sector_start = file_sector,
            .lba_start = (uint32_t)lba_64,
            .sector_count = run_sectors,
        };
        if (sample_stream_physical_map_append(map, &extent) == 0U)
        {
            sample_stream_physical_map_release(map);
            return 0U;
        }
        file_sector += run_sectors;
        remaining_sectors -= run_sectors;
    }

    if ((remaining_sectors != 0U) || (map->extent_count == 0U))
    {
        sample_stream_physical_map_release(map);
        return 0U;
    }

    map->valid = 1U;
    out_meta->file_size = file_size;
    return 1U;
}

uint8_t sample_stream_fatfs_map_build_from_path(const char *path,
                                                sample_stream_safe_metadata_t *out_meta)
{
    if ((path == 0) || (path[0] == '\0') || (out_meta == 0))
    {
        return 0U;
    }
    uint8_t acquired = 0U;
    if (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_NONE)
    {
        if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
        {
            return 0U;
        }
        acquired = 1U;
    }
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        if (acquired != 0U)
        {
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        }
        return 0U;
    }
    FIL fp;
    if (f_open(&fp, path, FA_READ) != FR_OK)
    {
        if (acquired != 0U)
        {
            sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
        }
        return 0U;
    }
    const uint8_t ok = sample_stream_fatfs_map_build_from_file(&fp, out_meta);
    (void)f_close(&fp);
    if (acquired != 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    }
    return ok;
}
