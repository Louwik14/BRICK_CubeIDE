#include "Sampler/sample_stream_fatfs_map.h"

#include <string.h>

#include "Storage/sd_access_gate.h"
#include "Storage/audio_recorder.h"
#include "SD/sd_block_device.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"

#include "ff.h"

#define SAMPLE_STREAM_FATFS_SECTOR_SIZE (512U)
#define SAMPLE_STREAM_FATFS_CLMT_ITEMS  \
    (2U + (2U * SAMPLE_STREAM_PHYSICAL_MAP_MAX_EXTENTS))

#define SAMPLE_STREAM_COOP_IO_OPEN (1U)
#define SAMPLE_STREAM_COOP_IO_MAP  (2U)
#define SAMPLE_STREAM_COOP_IO_READ (3U)

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
static sample_stream_coop_file_t *g_sample_stream_coop_active;
SDRAM_STREAM_SCRATCH ALIGN32 static sample_stream_coop_file_t
    g_sample_stream_coop_shared;

sample_stream_coop_file_t *sample_stream_coop_file_shared(void)
{
    return &g_sample_stream_coop_shared;
}

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(sample_stream_physical_extent_t) == 12U,
               "physical extent budget changed");
_Static_assert(sizeof(sample_stream_physical_map_t) == 60U,
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

static void sample_stream_physical_map_reset_blocks(
    sample_stream_physical_map_t *map)
{
    map->first_pool_block = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    for (uint32_t i = 0U; i < SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS; ++i)
    {
        map->pool_block[i] = SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
    }
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
        sample_stream_physical_map_reset_blocks(map);
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
    sample_stream_physical_map_reset_blocks(map);
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
    if (required_block >= SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS)
    {
        return 0U;
    }
    uint16_t block_index = map->pool_block[required_block];
    if (block_index >= SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
    {
        const int32_t allocated =
            sample_stream_physical_pool_allocate(map->generation);
        if (allocated < 0) return 0U;
        block_index = (uint16_t)allocated;
        map->pool_block[required_block] = block_index;
        if (required_block == 0U)
        {
            map->first_pool_block = block_index;
        }
        else
        {
            const uint16_t previous = map->pool_block[required_block - 1U];
            if (previous >= SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
            {
                memset(&g_sample_stream_physical_pool[block_index], 0,
                       sizeof(g_sample_stream_physical_pool[block_index]));
                map->pool_block[required_block] =
                    SAMPLE_STREAM_PHYSICAL_MAP_INVALID_BLOCK;
                return 0U;
            }
            g_sample_stream_physical_pool[previous].next = block_index;
        }
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
    sample_stream_physical_map_reset_blocks(map);
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

    const uint16_t overflow_index = (uint16_t)(extent_index - 1U);
    const uint16_t block_ordinal = (uint16_t)(
        overflow_index / SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK);
    if (block_ordinal >= SAMPLE_STREAM_PHYSICAL_MAP_MAX_BLOCKS) return 0U;
    const uint16_t block_index = map->pool_block[block_ordinal];
    if (block_index >= SAMPLE_STREAM_PHYSICAL_MAP_POOL_BLOCKS)
    {
        return 0U;
    }
    const sample_stream_physical_pool_block_t *const block =
        &g_sample_stream_physical_pool[block_index];
    const uint16_t in_block = (uint16_t)(
        overflow_index % SAMPLE_STREAM_PHYSICAL_MAP_EXTENTS_PER_BLOCK);
    if ((block->owner_generation != map->generation) || (in_block >= block->used))
    {
        return 0U;
    }
    *out_extent = block->extents[in_block];
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
    sample_stream_physical_map_reset_blocks(&out_meta->physical_map);
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
    sample_stream_physical_map_reset_blocks(map);

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

static uint8_t sample_stream_physical_map_from_clmt(
    FIL *fp, const DWORD *clmt, sample_stream_physical_map_t *map,
    uint32_t *out_file_size)
{
    if ((fp == NULL) || (clmt == NULL) || (map == NULL)
        || (fp->obj.fs == NULL) || (fp->obj.fs->csize == 0U)
        || (f_size(fp) == 0U) || (f_size(fp) > (FSIZE_t)UINT32_MAX))
        return 0U;
    sample_stream_physical_map_release(map);
    map->generation = sample_stream_physical_next_generation();
    map->media_epoch = sd_access_media_epoch();
    sample_stream_physical_map_reset_blocks(map);
    const uint32_t file_size = (uint32_t)f_size(fp);
    uint32_t remaining_sectors = (uint32_t)(((uint64_t)file_size
        + SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U) / SAMPLE_STREAM_FATFS_SECTOR_SIZE);
    uint32_t file_sector = 0U;
    uint32_t item = 1U;
    while ((item + 1U < SAMPLE_STREAM_COOP_CLMT_ITEMS)
        && (clmt[item] != 0U) && (remaining_sectors != 0U))
    {
        const uint32_t cluster_count = clmt[item++];
        const uint32_t first_cluster = clmt[item++];
        const uint64_t run64 = (uint64_t)cluster_count * fp->obj.fs->csize;
        const uint64_t lba64 = (uint64_t)fp->obj.fs->database
            + ((uint64_t)(first_cluster - 2U) * fp->obj.fs->csize);
        if ((first_cluster < 2U) || (run64 == 0U) || (run64 > UINT32_MAX)
            || (lba64 > UINT32_MAX)) goto fail;
        uint32_t run = (uint32_t)run64;
        if (run > remaining_sectors) run = remaining_sectors;
        const sample_stream_physical_extent_t extent = {
            .file_sector_start = file_sector,
            .lba_start = (uint32_t)lba64,
            .sector_count = run,
        };
        if (sample_stream_physical_map_append(map, &extent) == 0U) goto fail;
        file_sector += run;
        remaining_sectors -= run;
    }
    if ((remaining_sectors != 0U) || (map->extent_count == 0U)) goto fail;
    map->valid = 1U;
    if (out_file_size != NULL) *out_file_size = file_size;
    return 1U;
fail:
    sample_stream_physical_map_release(map);
    return 0U;
}

static void sample_stream_coop_fail(sample_stream_coop_file_t *file,
                                    FRESULT result)
{
    file->result = result;
    file->state = SAMPLE_STREAM_COOP_ERROR;
}

void sample_stream_coop_file_init(sample_stream_coop_file_t *file)
{
    if (file == NULL) return;
    memset(file, 0, sizeof(*file));
    sample_stream_physical_map_reset_blocks(&file->physical_map);
    file->cached_sector = UINT32_MAX;
    file->state = SAMPLE_STREAM_COOP_IDLE;
    file->result = FR_OK;
}

uint8_t sample_stream_coop_file_begin(sample_stream_coop_file_t *file,
                                      const char *path)
{
    if ((file == NULL) || (path == NULL) || (path[0] == '\0')
        || (strlen(path) >= sizeof(file->path))
        || (file->state != SAMPLE_STREAM_COOP_IDLE)
        || (g_sample_stream_coop_active != NULL)
        || (audio_recorder_is_active() != 0U)
        || (sd_access_gate_recorder_fs_logical_active() != 0U)
        || (sd_access_storage_status() != SD_STORAGE_STATUS_READY))
        return 0U;
    sample_stream_coop_file_init(file);
    memcpy(file->path, path, strlen(path) + 1U);
    file->media_epoch = sd_access_media_epoch();
    sd_access_gate_set_cooperative_fs_active(1U);
    g_sample_stream_coop_active = file;
    const FRESULT result = f_brick_read_open_begin(
        &file->open, &file->file, file->path, file->sector, sizeof(file->sector));
    if (result != FR_OK)
    {
        sample_stream_coop_fail(file, result);
        sample_stream_coop_file_close(file);
        return 0U;
    }
    file->state = SAMPLE_STREAM_COOP_OPENING;
    return 1U;
}

static uint8_t sample_stream_coop_submit(sample_stream_coop_file_t *file,
                                         const FF_META_REQUEST *request,
                                         uint8_t kind)
{
    if ((request == NULL) || (request->operation != FF_META_IO_READ)
        || (request->count != 1U)) return 0U;
    const sd_scheduler_background_request_t admission = {
        .byte_count = 0U,
        .media_epoch = file->media_epoch,
        .kind = SD_SCHEDULER_BACKGROUND_METADATA,
    };
    if (sd_scheduler_runtime_background_try_begin(&admission)
        != SD_SCHEDULER_BACKGROUND_GO) return 2U;
    if (++file->io_identity == 0U) file->io_identity = 1U;
    const sd_block_device_result_t submitted = sd_block_device_async_read_submit(
        request->sector, 1U, request->buffer, file->io_identity);
    if (submitted != SD_BLOCK_DEVICE_OK)
    {
        sd_scheduler_runtime_background_end();
        return (submitted == SD_BLOCK_DEVICE_BUSY
            || submitted == SD_BLOCK_DEVICE_QUEUE_FULL) ? 2U : 0U;
    }
    const FRESULT started = (kind == SAMPLE_STREAM_COOP_IO_OPEN)
        ? f_brick_read_open_io_started(&file->open, request->sequence)
        : f_brick_read_map_io_started(&file->map, request->sequence);
    if (started != FR_OK)
    {
        (void)sd_block_device_async_abort_generation(file->io_identity);
        sample_stream_coop_fail(file, started);
        return 0U;
    }
    file->io_lba = request->sector;
    file->io_sequence = request->sequence;
    file->io_kind = kind;
    file->io_active = 1U;
    return 1U;
}

static uint8_t sample_stream_coop_submit_raw(sample_stream_coop_file_t *file,
                                             uint32_t lba)
{
    const sd_scheduler_background_request_t admission = {
        .byte_count = 0U,
        .media_epoch = file->media_epoch,
        .kind = SD_SCHEDULER_BACKGROUND_METADATA,
    };
    if (sd_scheduler_runtime_background_try_begin(&admission)
        != SD_SCHEDULER_BACKGROUND_GO) return 2U;
    if (++file->io_identity == 0U) file->io_identity = 1U;
    const sd_block_device_result_t submitted = sd_block_device_async_read_submit(
        lba, 1U, file->sector, file->io_identity);
    if (submitted != SD_BLOCK_DEVICE_OK)
    {
        sd_scheduler_runtime_background_end();
        return (submitted == SD_BLOCK_DEVICE_BUSY
            || submitted == SD_BLOCK_DEVICE_QUEUE_FULL) ? 2U : 0U;
    }
    file->io_lba = lba;
    file->io_sequence = 0U;
    file->io_kind = SAMPLE_STREAM_COOP_IO_READ;
    file->io_active = 1U;
    return 1U;
}

static void sample_stream_coop_poll(sample_stream_coop_file_t *file)
{
    sd_block_device_async_poll();
    sd_block_device_async_completion_t completion;
    if (sd_block_device_async_take_completion(&completion) == 0U) return;
    file->io_active = 0U;
    const FRESULT result = (completion.result == SD_BLOCK_DEVICE_OK)
        ? FR_OK : FR_DISK_ERR;
    const uint8_t valid = (uint8_t)(completion.operation == SD_BLOCK_DEVICE_OPERATION_READ
        && completion.lba == file->io_lba && completion.sector_count == 1U
        && completion.media_epoch == file->media_epoch
        && completion.owner_generation == file->io_identity);
    if (valid != 0U)
    {
        if (file->io_kind == SAMPLE_STREAM_COOP_IO_OPEN)
            (void)f_brick_read_open_io_complete(
                &file->open, file->io_sequence, result);
        else if (file->io_kind == SAMPLE_STREAM_COOP_IO_MAP)
            (void)f_brick_read_map_io_complete(
                &file->map, file->io_sequence, result);
        else if (file->io_kind == SAMPLE_STREAM_COOP_IO_READ)
        {
            file->cached_sector = file->read_offset / SAMPLE_STREAM_FATFS_SECTOR_SIZE;
            file->cached_valid = (result == FR_OK) ? 1U : 0U;
        }
    }
    sd_scheduler_runtime_background_end();
    if ((valid == 0U) || (result != FR_OK)) sample_stream_coop_fail(file, result);
}

void sample_stream_coop_file_service(sample_stream_coop_file_t *file,
                                     uint32_t cpu_step_budget)
{
    if ((file == NULL) || (g_sample_stream_coop_active != file)
        || (file->state == SAMPLE_STREAM_COOP_IDLE)
        || (file->state == SAMPLE_STREAM_COOP_ERROR)) return;
    if (file->media_epoch != sd_access_media_epoch())
    {
        sample_stream_coop_fail(file, FR_DISK_ERR);
        return;
    }
    if (file->io_active != 0U)
    {
        sample_stream_coop_poll(file);
        if (file->io_active != 0U || file->state == SAMPLE_STREAM_COOP_ERROR) return;
    }
    if (cpu_step_budget == 0U) cpu_step_budget = 1U;
    while (cpu_step_budget-- != 0U)
    {
        if (file->state == SAMPLE_STREAM_COOP_READING)
        {
            if (file->read_remaining == 0U)
            {
                file->state = SAMPLE_STREAM_COOP_READY;
                return;
            }
            const uint32_t sector_index = file->read_offset / SAMPLE_STREAM_FATFS_SECTOR_SIZE;
            const uint16_t skip = (uint16_t)(file->read_offset
                & (SAMPLE_STREAM_FATFS_SECTOR_SIZE - 1U));
            if ((file->cached_valid != 0U) && (file->cached_sector == sector_index))
            {
                uint32_t copy = SAMPLE_STREAM_FATFS_SECTOR_SIZE - skip;
                if (copy > file->read_remaining) copy = file->read_remaining;
                memcpy(file->read_cursor, &file->sector[skip], copy);
                file->read_cursor += copy;
                file->read_offset += copy;
                file->read_remaining -= copy;
                continue;
            }
            sample_stream_physical_span_t span;
            sample_stream_physical_cursor_t cursor = {0};
            if (sample_stream_physical_map_resolve(&file->physical_map,
                    (uint64_t)sector_index * SAMPLE_STREAM_FATFS_SECTOR_SIZE,
                    SAMPLE_STREAM_FATFS_SECTOR_SIZE, &cursor, &span) == 0U)
            {
                sample_stream_coop_fail(file, FR_INT_ERR);
                return;
            }
            const uint8_t submit = sample_stream_coop_submit_raw(file, span.lba);
            if (submit == 0U) sample_stream_coop_fail(file, FR_DISK_ERR);
            return;
        }

        const FF_META_REQUEST *request = NULL;
        const uint8_t opening = (file->state == SAMPLE_STREAM_COOP_OPENING) ? 1U : 0U;
        const FF_META_STEP_RESULT step = opening
            ? f_brick_read_open_step(&file->open, &request)
            : f_brick_read_map_step(&file->map, &request);
        if (step == FF_META_STEP_YIELD) continue;
        if (step == FF_META_STEP_NEED_IO)
        {
            const uint8_t submit = sample_stream_coop_submit(file, request,
                opening ? SAMPLE_STREAM_COOP_IO_OPEN : SAMPLE_STREAM_COOP_IO_MAP);
            if (submit == 0U) sample_stream_coop_fail(file, FR_DISK_ERR);
            return;
        }
        if (step == FF_META_STEP_ERROR)
        {
            sample_stream_coop_fail(file,
                opening ? file->open.result : file->map.result);
            return;
        }
        if (step != FF_META_STEP_DONE)
        {
            sample_stream_coop_fail(file, FR_INT_ERR);
            return;
        }
        if (opening != 0U)
        {
            file->file_open = 1U;
            file->file_size = (uint32_t)f_size(&file->file);
            memset(file->clmt, 0, sizeof(file->clmt));
            file->clmt[0] = SAMPLE_STREAM_COOP_CLMT_ITEMS;
            file->file.cltbl = file->clmt;
            const FRESULT begin = f_brick_read_map_begin(
                &file->map, &file->file, file->sector, sizeof(file->sector));
            if (begin != FR_OK)
            {
                sample_stream_coop_fail(file, begin);
                return;
            }
            file->state = SAMPLE_STREAM_COOP_MAPPING;
            continue;
        }
        if (sample_stream_physical_map_from_clmt(&file->file, file->clmt,
                &file->physical_map, &file->file_size) == 0U)
        {
            sample_stream_coop_fail(file, FR_INT_ERR);
            return;
        }
        file->state = SAMPLE_STREAM_COOP_READY;
        return;
    }
}

uint8_t sample_stream_coop_file_read_begin(sample_stream_coop_file_t *file,
                                           uint32_t offset,
                                           void *dst,
                                           uint32_t bytes)
{
    if ((file == NULL) || (g_sample_stream_coop_active != file)
        || (file->state != SAMPLE_STREAM_COOP_READY) || (dst == NULL)
        || (bytes == 0U) || (offset > file->file_size)
        || (bytes > (file->file_size - offset))) return 0U;
    file->read_dst = dst;
    file->read_cursor = dst;
    file->read_offset = offset;
    file->read_remaining = bytes;
    file->state = SAMPLE_STREAM_COOP_READING;
    return 1U;
}

uint8_t sample_stream_coop_file_read_done(const sample_stream_coop_file_t *file)
{
    return (file != NULL && file->state == SAMPLE_STREAM_COOP_READY
        && file->read_dst != NULL && file->read_remaining == 0U) ? 1U : 0U;
}

uint8_t sample_stream_coop_file_take_map(sample_stream_coop_file_t *file,
                                         sample_stream_physical_map_t *out_map,
                                         uint32_t *out_file_size)
{
    if ((file == NULL) || (out_map == NULL)
        || (file->state != SAMPLE_STREAM_COOP_READY)
        || (file->physical_map.valid == 0U)) return 0U;
    *out_map = file->physical_map;
    memset(&file->physical_map, 0, sizeof(file->physical_map));
    sample_stream_physical_map_reset_blocks(&file->physical_map);
    if (out_file_size != NULL) *out_file_size = file->file_size;
    return 1U;
}

void sample_stream_coop_file_close(sample_stream_coop_file_t *file)
{
    if (file == NULL) return;
    if (file->io_active != 0U)
    {
        (void)sd_block_device_async_abort_generation(file->io_identity);
        file->io_active = 0U;
        sd_scheduler_runtime_background_end();
    }
    sample_stream_physical_map_release(&file->physical_map);
    if (file->file_open != 0U) (void)f_close(&file->file);
    if (g_sample_stream_coop_active == file)
    {
        g_sample_stream_coop_active = NULL;
        sd_access_gate_set_cooperative_fs_active(0U);
    }
    sample_stream_coop_file_init(file);
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
