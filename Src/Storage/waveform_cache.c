#include "Storage/waveform_cache.h"

#include "Core/rec_live_debug.h"
#include "Sampler/sample_cache.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/multi_record_writer.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/sample_capture.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/wav_audio_codec.h"
#include "wav_parser.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>

#define WAVEFORM_CACHE_DIR_ROOT "0:/BRICK"
#define WAVEFORM_CACHE_DIR_PATH "0:/BRICK/.wavecache"
#define WAVEFORM_CACHE_VERSION 1U
#define WAVEFORM_CACHE_ENDIAN_LE 0x1234U
#define WAVEFORM_CACHE_HASH_BYTES 65536U
#define WAVEFORM_CACHE_QUEUE_CAPACITY 4U
#define WAVEFORM_CACHE_TILE_QUEUE_CAPACITY 8U
#define WAVEFORM_CACHE_RAM_TILE_COUNT 64U
#define WAVEFORM_CACHE_IO_FRAMES 1024U
#define WAVEFORM_CACHE_MAX_BLOCK_ALIGN 8U
#define WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT 4U
#define WAVEFORM_CACHE_FORMAT_LEVEL_COUNT ((uint8_t)WAVEFORM_CACHE_LEVEL_COUNT)

#if defined(__GNUC__)
#define WAVEFORM_CACHE_PACKED __attribute__((packed))
#define WAVEFORM_CACHE_ALIGNED4 __attribute__((aligned(4)))
#define WAVEFORM_CACHE_ALIGNED8 __attribute__((aligned(8)))
#else
#define WAVEFORM_CACHE_PACKED
#define WAVEFORM_CACHE_ALIGNED4
#define WAVEFORM_CACHE_ALIGNED8
#endif

typedef struct WAVEFORM_CACHE_PACKED
{
    uint8_t magic[8];
    uint16_t version;
    uint16_t endian;
    uint16_t header_size;
    uint8_t state;
    uint8_t flags;
    uint8_t sample_id[WAVEFORM_CACHE_SAMPLE_ID_BYTES];
    uint64_t path_hash;
    uint32_t wav_size;
    uint32_t data_offset;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    uint16_t level_count;
    uint64_t head_hash;
    uint64_t tail_hash;
    uint32_t fat_date_time;
    uint32_t reserved[8];
} waveform_cache_file_header_t;

typedef struct WAVEFORM_CACHE_PACKED
{
    uint16_t level_id;
    uint16_t reserved;
    uint32_t frames_per_column;
    uint32_t column_count;
    uint32_t data_offset;
    uint32_t data_size;
    uint32_t tile_columns;
} waveform_cache_file_level_t;

typedef struct WAVEFORM_CACHE_PACKED
{
    int16_t min;
    int16_t max;
} waveform_cache_column_t;

typedef enum
{
    WAVEFORM_CACHE_JOB_EMPTY = 0,
    WAVEFORM_CACHE_JOB_QUEUED,
    WAVEFORM_CACHE_JOB_VALIDATE,
    WAVEFORM_CACHE_JOB_BUILDING
} waveform_cache_job_state_t;

typedef struct
{
    uint32_t frames_per_column;
    uint32_t column_count;
    uint32_t data_offset;
    uint32_t columns_done;
    uint32_t frames_in_column;
    int16_t min;
    int16_t max;
} waveform_cache_build_level_t;

typedef struct
{
    waveform_cache_job_state_t state;
    waveform_cache_reason_t reason;
    char wav_path[96U];
    char cache_path[96U];
    WAVEFORM_CACHE_ALIGNED8 waveform_cache_file_header_t header;
    WAVEFORM_CACHE_ALIGNED4 waveform_cache_file_level_t table[WAVEFORM_CACHE_FORMAT_LEVEL_COUNT];
    waveform_cache_build_level_t levels[WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT];
    uint32_t next_frame;
    uint8_t header_written;
} waveform_cache_job_t;

typedef struct
{
    uint8_t valid;
    uint8_t loading;
    uint8_t sample_id[WAVEFORM_CACHE_SAMPLE_ID_BYTES];
    uint16_t level_id;
    uint32_t tile_index;
    uint32_t column_count;
    uint32_t last_used;
    waveform_cache_minmax_t columns[WAVEFORM_CACHE_TILE_COLUMNS];
} waveform_cache_ram_tile_t;

typedef struct
{
    uint8_t pending;
    waveform_cache_handle_t handle;
    waveform_cache_level_id_t level_id;
    uint32_t tile_index;
    waveform_cache_reason_t reason;
} waveform_cache_tile_request_t;

typedef struct
{
    waveform_cache_diag_t diag;
    waveform_cache_job_t active;
    waveform_cache_job_t queue[WAVEFORM_CACHE_QUEUE_CAPACITY];
    waveform_cache_tile_request_t tile_queue[WAVEFORM_CACHE_TILE_QUEUE_CAPACITY];
    uint32_t tile_lru_tick;
    uint8_t service_defer_passes;
} waveform_cache_state_t;

STORAGE_STATE_SDRAM static waveform_cache_state_t g_waveform_cache;
STORAGE_STATE_SDRAM static waveform_cache_ram_tile_t
    g_waveform_cache_ram_tiles[WAVEFORM_CACHE_RAM_TILE_COUNT];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_waveform_cache_io[WAVEFORM_CACHE_IO_FRAMES * WAVEFORM_CACHE_MAX_BLOCK_ALIGN];

static const uint32_t g_waveform_cache_level_frames[WAVEFORM_CACHE_LEVEL_COUNT] = {
    16384U,
    4096U,
    1024U,
    256U,
    64U
};

static uint32_t waveform_cache_persist_min_frames_for_rate(uint32_t sample_rate)
{
    const uint32_t rate = (sample_rate != 0U)
        ? sample_rate
        : WAVEFORM_CACHE_PERSIST_DEFAULT_SAMPLE_RATE;
    if(rate > (0xFFFFFFFFUL / WAVEFORM_CACHE_PERSIST_MIN_SECONDS))
    {
        return 0xFFFFFFFFUL;
    }
    return rate * WAVEFORM_CACHE_PERSIST_MIN_SECONDS;
}

static uint8_t waveform_cache_duration_is_persistable(uint32_t frame_count,
                                                       uint32_t sample_rate)
{
    return (uint8_t)(frame_count >= waveform_cache_persist_min_frames_for_rate(sample_rate));
}

static uint64_t waveform_cache_hash_init(void)
{
    return 1469598103934665603ULL;
}

static uint64_t waveform_cache_hash_update(uint64_t hash, const uint8_t *data, uint32_t len)
{
    for(uint32_t i = 0U; i < len; ++i)
    {
        hash ^= (uint64_t)data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint64_t waveform_cache_hash_u32(uint64_t hash, uint32_t value)
{
    uint8_t b[4];
    b[0] = (uint8_t)(value & 0xFFU);
    b[1] = (uint8_t)((value >> 8) & 0xFFU);
    b[2] = (uint8_t)((value >> 16) & 0xFFU);
    b[3] = (uint8_t)((value >> 24) & 0xFFU);
    return waveform_cache_hash_update(hash, b, sizeof(b));
}

static uint8_t waveform_cache_path_char(uint8_t c)
{
    if(c == '\\')
    {
        c = '/';
    }
    if((c >= 'a') && (c <= 'z'))
    {
        c = (uint8_t)(c - ('a' - 'A'));
    }
    return c;
}

static uint8_t waveform_cache_path_has_suffix_ci(const char *path, const char *suffix)
{
    if((path == 0) || (suffix == 0))
    {
        return 0U;
    }
    uint32_t path_len = 0U;
    uint32_t suffix_len = 0U;
    while(path[path_len] != '\0') { path_len++; }
    while(suffix[suffix_len] != '\0') { suffix_len++; }
    if((suffix_len == 0U) || (path_len < suffix_len))
    {
        return 0U;
    }
    const uint32_t start = path_len - suffix_len;
    for(uint32_t i = 0U; i < suffix_len; ++i)
    {
        if(waveform_cache_path_char((uint8_t)path[start + i])
                != waveform_cache_path_char((uint8_t)suffix[i]))
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t waveform_cache_path_contains_ci(const char *path, const char *needle)
{
    if((path == 0) || (needle == 0) || (needle[0] == '\0'))
    {
        return 0U;
    }
    uint32_t needle_len = 0U;
    while(needle[needle_len] != '\0') { needle_len++; }
    for(uint32_t i = 0U; path[i] != '\0'; ++i)
    {
        uint32_t j = 0U;
        while((j < needle_len) && (path[i + j] != '\0')
                && (waveform_cache_path_char((uint8_t)path[i + j])
                    == waveform_cache_path_char((uint8_t)needle[j])))
        {
            j++;
        }
        if(j == needle_len)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t waveform_cache_path_is_temporary(const char *path)
{
    if((path == 0) || (path[0] == '\0'))
    {
        return 1U;
    }
    if(waveform_cache_path_has_suffix_ci(path, "/AUDIOREC_TMP.WAV") != 0U)
    {
        return 1U;
    }
    if(waveform_cache_path_contains_ci(path, "/PROJECT/REC/") != 0U)
    {
        return 1U;
    }
    if((waveform_cache_path_contains_ci(path, "_TMP.") != 0U)
            || (waveform_cache_path_contains_ci(path, "_TMP/") != 0U)
            || (waveform_cache_path_has_suffix_ci(path, "_TMP") != 0U))
    {
        return 1U;
    }
    return 0U;
}

static uint64_t waveform_cache_hash_path(const char *path)
{
    uint64_t hash = waveform_cache_hash_init();
    if(path == 0)
    {
        return hash;
    }
    for(uint32_t i = 0U; path[i] != '\0'; ++i)
    {
        const uint8_t c = waveform_cache_path_char((uint8_t)path[i]);
        hash = waveform_cache_hash_update(hash, &c, 1U);
    }
    return hash;
}

static void waveform_cache_debug_mark(rec_live_debug_code_t code,
                                      const char *path,
                                      uint32_t result)
{
    multi_record_writer_status_t status;
    uint32_t writer_state = 0U;
    uint32_t frames = 0U;
    uint32_t last_error = result;
    if(multi_record_writer_get_status(SAMPLE_CAPTURE_RECORD_CLIENT_ID, &status) != 0U)
    {
        writer_state = (uint32_t)status.state;
        frames = (status.frames_written != 0U) ? status.frames_written : status.frames_received;
        if(result == 0U)
        {
            last_error = (uint32_t)status.error;
        }
    }

    rec_live_debug_mark((uint32_t)code,
                        frames,
                        rec_live_debug_path_hash(path),
                        writer_state,
                        0U,
                        last_error);
}

static uint8_t waveform_cache_has_tile_work(void)
{
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_TILE_QUEUE_CAPACITY; ++i)
    {
        if(g_waveform_cache.tile_queue[i].pending != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t waveform_cache_has_queued_work(void)
{
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_QUEUE_CAPACITY; ++i)
    {
        if(g_waveform_cache.queue[i].state != WAVEFORM_CACHE_JOB_EMPTY)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t waveform_cache_has_service_work(void)
{
    return (uint8_t)((g_waveform_cache.service_defer_passes != 0U)
        || (g_waveform_cache.active.state != WAVEFORM_CACHE_JOB_EMPTY)
        || (waveform_cache_has_queued_work() != 0U)
        || (waveform_cache_has_tile_work() != 0U));
}

static uint8_t waveform_cache_finish_request(const char *path,
                                             waveform_cache_reason_t reason,
                                             uint8_t result)
{
    if((result != 0U) && (reason == WAVEFORM_CACHE_REASON_POST_AUDIO_REC)
            && (g_waveform_cache.service_defer_passes < 2U))
    {
        g_waveform_cache.service_defer_passes = 2U;
    }
    waveform_cache_debug_mark(REC_LIVE_DEBUG_WAVECACHE_REQUEST_EXIT, path, result);
    return result;
}

static uint8_t waveform_cache_copy_path(char *dst, uint32_t dst_len, const char *src)
{
    if((dst == 0) || (dst_len == 0U) || (src == 0) || (src[0] == '\0'))
    {
        return 0U;
    }
    for(uint32_t i = 0U; i < dst_len; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            return 1U;
        }
    }
    dst[0] = '\0';
    return 0U;
}

static void waveform_cache_hex32(const uint8_t *id, char *out)
{
    static const char hex[] = "0123456789ABCDEF";
    for(uint32_t i = 0U; i < WAVEFORM_CACHE_SAMPLE_ID_BYTES; ++i)
    {
        out[i * 2U] = hex[(id[i] >> 4) & 0x0FU];
        out[(i * 2U) + 1U] = hex[id[i] & 0x0FU];
    }
    out[32U] = '\0';
}

static void waveform_cache_make_sample_id(waveform_cache_file_header_t *header)
{
    uint64_t h0 = waveform_cache_hash_init();
    uint64_t h1 = 1099511628211ULL;
    h0 = waveform_cache_hash_u32(h0, (uint32_t)header->path_hash);
    h0 = waveform_cache_hash_u32(h0, (uint32_t)(header->path_hash >> 32));
    h0 = waveform_cache_hash_u32(h0, header->wav_size);
    h0 = waveform_cache_hash_u32(h0, header->data_offset);
    h0 = waveform_cache_hash_u32(h0, header->frame_count);
    h0 = waveform_cache_hash_u32(h0, header->sample_rate);
    h0 = waveform_cache_hash_u32(h0, ((uint32_t)header->channels << 16) | header->bits_per_sample);
    h0 = waveform_cache_hash_u32(h0, (uint32_t)header->head_hash);
    h0 = waveform_cache_hash_u32(h0, (uint32_t)(header->head_hash >> 32));
    h0 = waveform_cache_hash_u32(h0, (uint32_t)header->tail_hash);
    h0 = waveform_cache_hash_u32(h0, (uint32_t)(header->tail_hash >> 32));

    h1 = waveform_cache_hash_u32(h1, (uint32_t)(header->tail_hash >> 32));
    h1 = waveform_cache_hash_u32(h1, (uint32_t)header->tail_hash);
    h1 = waveform_cache_hash_u32(h1, header->frame_count);
    h1 = waveform_cache_hash_u32(h1, header->wav_size);
    h1 = waveform_cache_hash_u32(h1, (uint32_t)(header->path_hash >> 32));
    h1 = waveform_cache_hash_u32(h1, (uint32_t)header->path_hash);

    for(uint32_t i = 0U; i < 8U; ++i)
    {
        header->sample_id[i] = (uint8_t)((h0 >> (i * 8U)) & 0xFFU);
        header->sample_id[i + 8U] = (uint8_t)((h1 >> (i * 8U)) & 0xFFU);
    }
}

static uint8_t waveform_cache_hash_file_range(FIL *fp,
                                               uint32_t offset,
                                               uint32_t bytes,
                                               uint32_t file_size,
                                               uint64_t *out_hash)
{
    if((fp == 0) || (out_hash == 0))
    {
        return 0U;
    }
    *out_hash = waveform_cache_hash_init();
    if((bytes == 0U) || (offset >= file_size))
    {
        return 0U;
    }
    if(bytes > (file_size - offset))
    {
        bytes = file_size - offset;
    }
    if(f_lseek(fp, offset) != FR_OK)
    {
        return 0U;
    }
    uint64_t hash = waveform_cache_hash_init();
    uint32_t left = bytes;
    while(left != 0U)
    {
        uint32_t chunk = left;
        if(chunk > sizeof(g_waveform_cache_io))
        {
            chunk = sizeof(g_waveform_cache_io);
        }
        UINT br = 0U;
        if((chunk == 0U) || (f_read(fp, g_waveform_cache_io, chunk, &br) != FR_OK))
        {
            return 0U;
        }
        if(br == 0U)
        {
            break;
        }
        hash = waveform_cache_hash_update(hash, g_waveform_cache_io, br);
        left -= br;
        if(br < chunk)
        {
            break;
        }
    }
    *out_hash = hash;
    return 1U;
}

static uint8_t waveform_cache_build_identity(const char *path,
                                              waveform_cache_file_header_t *out_header)
{
    FIL fp;
    FILINFO fno;
    wav_info_t info;
    WAVEFORM_CACHE_ALIGNED8 waveform_cache_file_header_t header;
    uint8_t ok = 0U;

    if((path == 0) || (path[0] == '\0') || (out_header == 0)
            || (waveform_cache_path_is_temporary(path) != 0U))
    {
        return 0U;
    }

    memset(&header, 0, sizeof(header));
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        return 0U;
    }
    if(f_stat(path, &fno) != FR_OK)
    {
        return 0U;
    }
    if(f_open(&fp, path, FA_READ) != FR_OK)
    {
        return 0U;
    }
    do
    {
        memset(&info, 0, sizeof(info));
        if(wav_parser_parse_info(&fp, &info) == 0)
        {
            break;
        }
        const uint32_t wav_size = (uint32_t)f_size(&fp);
        if((info.channels == 0U) || (info.channels > 2U)
                || ((info.bits_per_sample != 16U)
                    && (info.bits_per_sample != 24U)
                    && (info.bits_per_sample != 32U))
                || (info.block_align == 0U)
                || (info.block_align > WAVEFORM_CACHE_MAX_BLOCK_ALIGN)
                || (info.data_size < info.block_align)
                || (info.data_offset >= wav_size)
                || (info.data_size > (wav_size - info.data_offset)))
        {
            break;
        }

        const uint32_t hashable_data_size = info.data_size;
        const uint32_t head_bytes =
            (hashable_data_size < WAVEFORM_CACHE_HASH_BYTES)
                ? hashable_data_size : WAVEFORM_CACHE_HASH_BYTES;
        const uint32_t tail_bytes = head_bytes;
        const uint32_t tail_offset = info.data_offset + hashable_data_size - tail_bytes;

        memcpy(header.magic, "BRKWAVE", 7U);
        header.version = WAVEFORM_CACHE_VERSION;
        header.endian = WAVEFORM_CACHE_ENDIAN_LE;
        header.header_size =
            (uint16_t)(sizeof(waveform_cache_file_header_t)
                + (sizeof(waveform_cache_file_level_t) * WAVEFORM_CACHE_FORMAT_LEVEL_COUNT));
        header.state = (uint8_t)WAVEFORM_CACHE_STATE_BUILDING;
        header.path_hash = waveform_cache_hash_path(path);
        header.wav_size = wav_size;
        header.data_offset = info.data_offset;
        header.frame_count = info.data_size / (uint32_t)info.block_align;
        header.sample_rate = info.sample_rate;
        header.channels = info.channels;
        header.bits_per_sample = info.bits_per_sample;
        header.block_align = info.block_align;
        header.level_count = WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT;
        header.fat_date_time = ((uint32_t)fno.fdate << 16) | (uint32_t)fno.ftime;
        uint64_t head_hash = 0ULL;
        uint64_t tail_hash = 0ULL;
        if((header.frame_count == 0U)
                || (waveform_cache_hash_file_range(&fp,
                                                   info.data_offset,
                                                   head_bytes,
                                                   wav_size,
                                                   &head_hash) == 0U)
                || (waveform_cache_hash_file_range(&fp,
                                                   tail_offset,
                                                   tail_bytes,
                                                   wav_size,
                                                   &tail_hash) == 0U))
        {
            break;
        }
        header.head_hash = head_hash;
        header.tail_hash = tail_hash;
        waveform_cache_make_sample_id(&header);
        ok = 1U;
    } while(0);

    (void)f_close(&fp);
    if(ok != 0U)
    {
        memcpy(out_header, &header, sizeof(header));
    }
    else
    {
        memset(out_header, 0, sizeof(*out_header));
    }
    return ok;
}

static void waveform_cache_make_cache_path(const uint8_t *sample_id, char *out, uint32_t out_len)
{
    char hex[33U];
    waveform_cache_hex32(sample_id, hex);
    (void)snprintf(out, out_len, WAVEFORM_CACHE_DIR_PATH "/%s.brkwave", hex);
}

static uint32_t waveform_cache_column_count(uint32_t frame_count, uint32_t frames_per_column)
{
    if((frame_count == 0U) || (frames_per_column == 0U))
    {
        return 0U;
    }
    return (frame_count + frames_per_column - 1U) / frames_per_column;
}

static void waveform_cache_fill_table(waveform_cache_job_t *job)
{
    uint32_t offset = job->header.header_size;
    memset(job->table, 0, sizeof(job->table));
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_FORMAT_LEVEL_COUNT; ++i)
    {
        waveform_cache_file_level_t *const level = &job->table[i];
        level->level_id = i;
        level->frames_per_column = g_waveform_cache_level_frames[i];
        level->tile_columns = WAVEFORM_CACHE_TILE_COLUMNS;
        if(i < WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT)
        {
            level->column_count =
                waveform_cache_column_count(job->header.frame_count, level->frames_per_column);
            level->data_offset = offset;
            level->data_size = level->column_count * sizeof(waveform_cache_column_t);
            offset += level->data_size;

            job->levels[i].frames_per_column = level->frames_per_column;
            job->levels[i].column_count = level->column_count;
            job->levels[i].data_offset = level->data_offset;
            job->levels[i].columns_done = 0U;
            job->levels[i].frames_in_column = 0U;
            job->levels[i].min = 32767;
            job->levels[i].max = (int16_t)-32768;
        }
    }
}

static uint8_t waveform_cache_header_matches(const waveform_cache_file_header_t *a,
                                              const waveform_cache_file_header_t *b)
{
    return (uint8_t)((memcmp(a->magic, "BRKWAVE", 7U) == 0)
            && (a->version == WAVEFORM_CACHE_VERSION)
            && (a->endian == WAVEFORM_CACHE_ENDIAN_LE)
            && (a->state == (uint8_t)WAVEFORM_CACHE_STATE_READY)
            && (memcmp(a->sample_id, b->sample_id, WAVEFORM_CACHE_SAMPLE_ID_BYTES) == 0)
            && (a->path_hash == b->path_hash)
            && (a->wav_size == b->wav_size)
            && (a->data_offset == b->data_offset)
            && (a->frame_count == b->frame_count)
            && (a->sample_rate == b->sample_rate)
            && (a->channels == b->channels)
            && (a->bits_per_sample == b->bits_per_sample)
            && (a->block_align == b->block_align)
            && (a->head_hash == b->head_hash)
            && (a->tail_hash == b->tail_hash));
}

static uint8_t waveform_cache_validate_existing(const char *cache_path,
                                                 const waveform_cache_file_header_t *identity)
{
    FIL fp;
    waveform_cache_file_header_t header;
    waveform_cache_file_level_t table[WAVEFORM_CACHE_FORMAT_LEVEL_COUNT];
    UINT br = 0U;
    uint8_t valid = 0U;

    if(f_open(&fp, cache_path, FA_READ) != FR_OK)
    {
        return 0U;
    }
    do
    {
        if((f_read(&fp, &header, sizeof(header), &br) != FR_OK) || (br != sizeof(header)))
        {
            break;
        }
        if((f_read(&fp, table, sizeof(table), &br) != FR_OK) || (br != sizeof(table)))
        {
            break;
        }
        if(waveform_cache_header_matches(&header, identity) == 0U)
        {
            break;
        }
        if(header.level_count != WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT)
        {
            break;
        }
        const uint32_t file_size = (uint32_t)f_size(&fp);
        valid = 1U;
        for(uint8_t i = 0U; i < WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT; ++i)
        {
            const uint32_t expected_cols =
                waveform_cache_column_count(identity->frame_count, g_waveform_cache_level_frames[i]);
            if((table[i].level_id != i)
                    || (table[i].frames_per_column != g_waveform_cache_level_frames[i])
                    || (table[i].column_count != expected_cols)
                    || (table[i].data_size != (expected_cols * sizeof(waveform_cache_column_t)))
                    || ((table[i].data_offset + table[i].data_size) > file_size))
            {
                valid = 0U;
                break;
            }
        }
    } while(0);

    (void)f_close(&fp);
    if(valid == 0U)
    {
        (void)f_unlink(cache_path);
    }
    return valid;
}

static uint8_t waveform_cache_read_ready_header(const uint8_t *sample_id,
                                                waveform_cache_file_header_t *out_header,
                                                waveform_cache_file_level_t *out_table)
{
    if((sample_id == 0) || (out_header == 0) || (out_table == 0))
    {
        return 0U;
    }

    char cache_path[96U];
    waveform_cache_make_cache_path(sample_id, cache_path, sizeof(cache_path));

    FIL fp;
    UINT br = 0U;
    uint8_t ok = 0U;
    WAVEFORM_CACHE_ALIGNED8 waveform_cache_file_header_t header;
    WAVEFORM_CACHE_ALIGNED4 waveform_cache_file_level_t table[WAVEFORM_CACHE_FORMAT_LEVEL_COUNT];
    if(f_open(&fp, cache_path, FA_READ) != FR_OK)
    {
        return 0U;
    }
    do
    {
        if((f_read(&fp, &header, sizeof(header), &br) != FR_OK)
                || (br != sizeof(header)))
        {
            break;
        }
        if((f_read(&fp,
                   table,
                   sizeof(waveform_cache_file_level_t) * WAVEFORM_CACHE_FORMAT_LEVEL_COUNT,
                   &br) != FR_OK)
                || (br != (sizeof(waveform_cache_file_level_t)
                    * WAVEFORM_CACHE_FORMAT_LEVEL_COUNT)))
        {
            break;
        }
        if((memcmp(header.magic, "BRKWAVE", 7U) != 0)
                || (header.version != WAVEFORM_CACHE_VERSION)
                || (header.endian != WAVEFORM_CACHE_ENDIAN_LE)
                || (header.state != (uint8_t)WAVEFORM_CACHE_STATE_READY)
                || (memcmp(header.sample_id,
                           sample_id,
                           WAVEFORM_CACHE_SAMPLE_ID_BYTES) != 0)
                || (header.level_count != WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT))
        {
            break;
        }
        const uint32_t file_size = (uint32_t)f_size(&fp);
        ok = 1U;
        for(uint8_t i = 0U; i < WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT; ++i)
        {
            if((table[i].level_id != i)
                    || (table[i].frames_per_column != g_waveform_cache_level_frames[i])
                    || (table[i].tile_columns != WAVEFORM_CACHE_TILE_COLUMNS)
                    || ((table[i].data_offset + table[i].data_size) > file_size))
            {
                ok = 0U;
                break;
            }
        }
    } while(0);

    (void)f_close(&fp);
    if(ok != 0U)
    {
        memcpy(out_header, &header, sizeof(header));
        memcpy(out_table,
               table,
               sizeof(waveform_cache_file_level_t) * WAVEFORM_CACHE_FORMAT_LEVEL_COUNT);
    }
    return ok;
}

static uint8_t waveform_cache_same_tile(const waveform_cache_ram_tile_t *tile,
                                        const waveform_cache_handle_t *handle,
                                        waveform_cache_level_id_t level_id,
                                        uint32_t tile_index)
{
    return (uint8_t)((tile != 0)
            && (handle != 0)
            && (tile->valid != 0U)
            && (tile->level_id == (uint16_t)level_id)
            && (tile->tile_index == tile_index)
            && (memcmp(tile->sample_id,
                       handle->sample_id,
                       WAVEFORM_CACHE_SAMPLE_ID_BYTES) == 0));
}

static int16_t waveform_cache_find_ram_tile(const waveform_cache_handle_t *handle,
                                            waveform_cache_level_id_t level_id,
                                            uint32_t tile_index)
{
    for(uint16_t i = 0U; i < WAVEFORM_CACHE_RAM_TILE_COUNT; ++i)
    {
        if(waveform_cache_same_tile(&g_waveform_cache_ram_tiles[i],
                                    handle,
                                    level_id,
                                    tile_index) != 0U)
        {
            return (int16_t)i;
        }
    }
    return -1;
}

static uint8_t waveform_cache_tile_request_exists(const waveform_cache_handle_t *handle,
                                                  waveform_cache_level_id_t level_id,
                                                  uint32_t tile_index)
{
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_TILE_QUEUE_CAPACITY; ++i)
    {
        const waveform_cache_tile_request_t *const req = &g_waveform_cache.tile_queue[i];
        if((req->pending != 0U)
                && (req->level_id == level_id)
                && (req->tile_index == tile_index)
                && (memcmp(req->handle.sample_id,
                           handle->sample_id,
                           WAVEFORM_CACHE_SAMPLE_ID_BYTES) == 0))
        {
            return 1U;
        }
    }
    return 0U;
}

static int16_t waveform_cache_pick_tile_slot(void)
{
    int16_t best = -1;
    uint32_t best_tick = 0xFFFFFFFFUL;
    for(uint16_t i = 0U; i < WAVEFORM_CACHE_RAM_TILE_COUNT; ++i)
    {
        if((g_waveform_cache_ram_tiles[i].valid == 0U)
                && (g_waveform_cache_ram_tiles[i].loading == 0U))
        {
            return (int16_t)i;
        }
    }
    for(uint16_t i = 0U; i < WAVEFORM_CACHE_RAM_TILE_COUNT; ++i)
    {
        if(g_waveform_cache_ram_tiles[i].loading != 0U)
        {
            continue;
        }
        if((best < 0) || (g_waveform_cache_ram_tiles[i].last_used < best_tick))
        {
            best = (int16_t)i;
            best_tick = g_waveform_cache_ram_tiles[i].last_used;
        }
    }
    return best;
}

static int16_t waveform_cache_float_to_i16(float v)
{
    if(v > 0.999969f)
    {
        v = 0.999969f;
    }
    else if(v < -1.0f)
    {
        v = -1.0f;
    }
    return (int16_t)(v * 32767.0f);
}

static int16_t waveform_cache_frame_to_i16(const uint8_t *frame,
                                           uint16_t channels,
                                           uint16_t bits_per_sample)
{
    float l = 0.0f;
    float r = 0.0f;
    wav_audio_codec_decode_stereo_frame(frame, channels, bits_per_sample, &l, &r);
    const int16_t li = waveform_cache_float_to_i16(l);
    const int16_t ri = waveform_cache_float_to_i16(r);
    const int16_t amin = (li < ri) ? li : ri;
    const int16_t amax = (li > ri) ? li : ri;
    return ((int32_t)amax > -(int32_t)amin) ? amax : amin;
}

static uint8_t waveform_cache_write_header(FIL *fp, waveform_cache_job_t *job, uint8_t state)
{
    UINT bw = 0U;
    job->header.state = state;
    if(f_lseek(fp, 0U) != FR_OK)
    {
        return 0U;
    }
    if((f_write(fp, &job->header, sizeof(job->header), &bw) != FR_OK)
            || (bw != sizeof(job->header)))
    {
        return 0U;
    }
    if((f_write(fp, job->table, sizeof(job->table), &bw) != FR_OK)
            || (bw != sizeof(job->table)))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t waveform_cache_write_column(FIL *fp,
                                            const waveform_cache_build_level_t *level,
                                            int16_t min_v,
                                            int16_t max_v)
{
    waveform_cache_column_t col;
    UINT bw = 0U;
    col.min = min_v;
    col.max = max_v;
    const uint32_t offset =
        level->data_offset + (level->columns_done * sizeof(waveform_cache_column_t));
    if(f_lseek(fp, offset) != FR_OK)
    {
        return 0U;
    }
    return (uint8_t)(((f_write(fp, &col, sizeof(col), &bw) == FR_OK) && (bw == sizeof(col))) ? 1U : 0U);
}

static uint8_t waveform_cache_accumulate_frame(FIL *cache_fp,
                                                waveform_cache_job_t *job,
                                                int16_t sample,
                                                uint8_t flush_last)
{
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT; ++i)
    {
        waveform_cache_build_level_t *const level = &job->levels[i];
        if(level->columns_done >= level->column_count)
        {
            continue;
        }
        if(level->frames_in_column == 0U)
        {
            level->min = sample;
            level->max = sample;
        }
        else
        {
            if(sample < level->min) { level->min = sample; }
            if(sample > level->max) { level->max = sample; }
        }
        level->frames_in_column++;
        if((level->frames_in_column >= level->frames_per_column)
                || ((flush_last != 0U) && (job->next_frame >= job->header.frame_count)))
        {
            if(waveform_cache_write_column(cache_fp, level, level->min, level->max) == 0U)
            {
                return 0U;
            }
            level->columns_done++;
            level->frames_in_column = 0U;
            level->min = 32767;
            level->max = (int16_t)-32768;
        }
    }
    return 1U;
}

static void waveform_cache_fail_active(void)
{
    if(g_waveform_cache.active.cache_path[0] != '\0')
    {
        (void)f_unlink(g_waveform_cache.active.cache_path);
    }
    memset(&g_waveform_cache.active, 0, sizeof(g_waveform_cache.active));
    g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_ERROR;
    g_waveform_cache.diag.jobs_failed++;
}

static uint8_t waveform_cache_start_next_job(void)
{
    int8_t best = -1;
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_QUEUE_CAPACITY; ++i)
    {
        if(g_waveform_cache.queue[i].state != WAVEFORM_CACHE_JOB_QUEUED)
        {
            continue;
        }
        if((best < 0)
                || ((uint8_t)g_waveform_cache.queue[i].reason
                    < (uint8_t)g_waveform_cache.queue[(uint8_t)best].reason))
        {
            best = (int8_t)i;
        }
    }
    if(best < 0)
    {
        return 0U;
    }
    g_waveform_cache.active = g_waveform_cache.queue[(uint8_t)best];
    memset(&g_waveform_cache.queue[(uint8_t)best], 0, sizeof(g_waveform_cache.queue[(uint8_t)best]));
    g_waveform_cache.active.state = WAVEFORM_CACHE_JOB_VALIDATE;
    g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_VALIDATING;
    return 1U;
}

void waveform_cache_init(void)
{
    memset(&g_waveform_cache, 0, sizeof(g_waveform_cache));
    memset(g_waveform_cache_ram_tiles, 0, sizeof(g_waveform_cache_ram_tiles));
    g_waveform_cache.diag.active_level_count = WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT;
}

uint8_t waveform_cache_ensure_dirs(void)
{
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
    {
        return 0U;
    }
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        FRESULT fr = f_mkdir(WAVEFORM_CACHE_DIR_ROOT);
        if((fr == FR_OK) || (fr == FR_EXIST))
        {
            fr = f_mkdir(WAVEFORM_CACHE_DIR_PATH);
            if((fr == FR_OK) || (fr == FR_EXIST))
            {
                ok = 1U;
            }
        }
        g_waveform_cache.diag.last_fresult = (uint32_t)fr;
    }
    else
    {
        g_waveform_cache.diag.last_fresult = (uint32_t)FR_NOT_READY;
    }
    g_waveform_cache.diag.dirs_ready = ok;
    sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
    return ok;
}

uint8_t waveform_cache_request_for_wav(const char *path, waveform_cache_reason_t reason)
{
    waveform_cache_debug_mark(REC_LIVE_DEBUG_WAVECACHE_REQUEST_ENTER, path, (uint32_t)reason);
    if((path == 0) || (path[0] == '\0') || (waveform_cache_path_is_temporary(path) != 0U))
    {
        return waveform_cache_finish_request(path, reason, 1U);
    }
    if((g_waveform_cache.active.state != WAVEFORM_CACHE_JOB_EMPTY)
            && (strncmp(g_waveform_cache.active.wav_path, path, sizeof(g_waveform_cache.active.wav_path)) == 0))
    {
        if((uint8_t)reason < (uint8_t)g_waveform_cache.active.reason)
        {
            g_waveform_cache.active.reason = reason;
        }
        return waveform_cache_finish_request(path, reason, 1U);
    }
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_QUEUE_CAPACITY; ++i)
    {
        if((g_waveform_cache.queue[i].state != WAVEFORM_CACHE_JOB_EMPTY)
                && (strncmp(g_waveform_cache.queue[i].wav_path, path, sizeof(g_waveform_cache.queue[i].wav_path)) == 0))
        {
            if((uint8_t)reason < (uint8_t)g_waveform_cache.queue[i].reason)
            {
                g_waveform_cache.queue[i].reason = reason;
            }
            return waveform_cache_finish_request(path, reason, 1U);
        }
    }
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_QUEUE_CAPACITY; ++i)
    {
        if(g_waveform_cache.queue[i].state == WAVEFORM_CACHE_JOB_EMPTY)
        {
            memset(&g_waveform_cache.queue[i], 0, sizeof(g_waveform_cache.queue[i]));
            if(waveform_cache_copy_path(g_waveform_cache.queue[i].wav_path,
                                        sizeof(g_waveform_cache.queue[i].wav_path),
                                        path) == 0U)
            {
                return waveform_cache_finish_request(path, reason, 0U);
            }
            g_waveform_cache.queue[i].reason = reason;
            g_waveform_cache.queue[i].state = WAVEFORM_CACHE_JOB_QUEUED;
            g_waveform_cache.diag.jobs_queued++;
            g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_QUEUED;
            return waveform_cache_finish_request(path, reason, 1U);
        }
    }
    return waveform_cache_finish_request(path, reason, 0U);
}

uint8_t waveform_cache_request_for_wav_known_duration(const char *path,
                                                      waveform_cache_reason_t reason,
                                                      uint32_t frame_count,
                                                      uint32_t sample_rate)
{
    if((path == 0) || (path[0] == '\0') || (waveform_cache_path_is_temporary(path) != 0U))
    {
        return waveform_cache_finish_request(path, reason, 1U);
    }
    if(waveform_cache_duration_is_persistable(frame_count, sample_rate) == 0U)
    {
        return waveform_cache_finish_request(path, reason, 1U);
    }
    return waveform_cache_request_for_wav(path, reason);
}

static void waveform_cache_service_validate(void)
{
    if(waveform_cache_path_is_temporary(g_waveform_cache.active.wav_path) != 0U)
    {
        memset(&g_waveform_cache.active, 0, sizeof(g_waveform_cache.active));
        g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_IDLE;
        return;
    }
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
    {
        return;
    }
    if(waveform_cache_build_identity(g_waveform_cache.active.wav_path,
                                     &g_waveform_cache.active.header) == 0U)
    {
        g_waveform_cache.diag.last_fresult = (uint32_t)FR_INVALID_OBJECT;
        sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
        waveform_cache_fail_active();
        return;
    }
    if(waveform_cache_duration_is_persistable(g_waveform_cache.active.header.frame_count,
                                              g_waveform_cache.active.header.sample_rate) == 0U)
    {
        memset(&g_waveform_cache.active, 0, sizeof(g_waveform_cache.active));
        g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_IDLE;
        g_waveform_cache.diag.jobs_done++;
        sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
        return;
    }
    waveform_cache_make_cache_path(g_waveform_cache.active.header.sample_id,
                                   g_waveform_cache.active.cache_path,
                                   sizeof(g_waveform_cache.active.cache_path));
    if(waveform_cache_validate_existing(g_waveform_cache.active.cache_path,
                                        &g_waveform_cache.active.header) != 0U)
    {
        memset(&g_waveform_cache.active, 0, sizeof(g_waveform_cache.active));
        g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_READY;
        g_waveform_cache.diag.jobs_done++;
        sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
        return;
    }
    waveform_cache_fill_table(&g_waveform_cache.active);
    g_waveform_cache.active.state = WAVEFORM_CACHE_JOB_BUILDING;
    g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_BUILDING;
    g_waveform_cache.diag.frame_count = g_waveform_cache.active.header.frame_count;
    g_waveform_cache.diag.frames_done = 0U;
    sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
}

static void waveform_cache_service_build(uint32_t byte_budget)
{
    FIL wav_fp;
    FIL cache_fp;
    uint8_t wav_open = 0U;
    uint8_t cache_open = 0U;
    uint8_t ok = 0U;
    waveform_cache_job_t *const job = &g_waveform_cache.active;

    if((byte_budget == 0U) || (job->header.block_align == 0U))
    {
        return;
    }
    if(waveform_cache_path_is_temporary(job->wav_path) != 0U)
    {
        waveform_cache_fail_active();
        return;
    }
    if(sample_cache_has_pending_sd_work() != 0U
            || multi_record_writer_any_active() != 0U
            || looper_storage_raw_export_is_active() != 0U
            || sd_preview_is_active() != 0U
            || pattern_load_is_pending() != 0U)
    {
        return;
    }
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
    {
        return;
    }
    do
    {
        if(sd_access_fs_mount_if_needed() == 0U)
        {
            break;
        }
        if(g_waveform_cache.diag.dirs_ready == 0U)
        {
            (void)f_mkdir(WAVEFORM_CACHE_DIR_ROOT);
            (void)f_mkdir(WAVEFORM_CACHE_DIR_PATH);
        }
        if(f_open(&wav_fp, job->wav_path, FA_READ) != FR_OK)
        {
            break;
        }
        wav_open = 1U;
        const BYTE cache_mode = (job->header_written == 0U)
            ? (BYTE)(FA_READ | FA_WRITE | FA_CREATE_ALWAYS)
            : (BYTE)(FA_READ | FA_WRITE | FA_OPEN_EXISTING);
        if(f_open(&cache_fp, job->cache_path, cache_mode) != FR_OK)
        {
            break;
        }
        cache_open = 1U;
        if(job->header_written == 0U)
        {
            if(waveform_cache_write_header(&cache_fp, job, (uint8_t)WAVEFORM_CACHE_STATE_BUILDING) == 0U)
            {
                break;
            }
            job->header_written = 1U;
        }

        uint32_t frames_to_read = byte_budget / (uint32_t)job->header.block_align;
        if(frames_to_read > WAVEFORM_CACHE_IO_FRAMES)
        {
            frames_to_read = WAVEFORM_CACHE_IO_FRAMES;
        }
        const uint32_t frames_left = job->header.frame_count - job->next_frame;
        if(frames_to_read > frames_left)
        {
            frames_to_read = frames_left;
        }
        if(frames_to_read == 0U)
        {
            if(waveform_cache_write_header(&cache_fp, job, (uint8_t)WAVEFORM_CACHE_STATE_READY) == 0U)
            {
                break;
            }
            if(f_sync(&cache_fp) != FR_OK)
            {
                break;
            }
            memset(job, 0, sizeof(*job));
            g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_READY;
            g_waveform_cache.diag.jobs_done++;
            ok = 1U;
            break;
        }
        if(f_lseek(&wav_fp,
                   job->header.data_offset + (job->next_frame * (uint32_t)job->header.block_align)) != FR_OK)
        {
            break;
        }
        const uint32_t bytes_to_read = frames_to_read * (uint32_t)job->header.block_align;
        UINT br = 0U;
        if((f_read(&wav_fp, g_waveform_cache_io, bytes_to_read, &br) != FR_OK)
                || (br < job->header.block_align))
        {
            break;
        }
        const uint32_t frames_read = br / (uint32_t)job->header.block_align;
        uint8_t accum_ok = 1U;
        for(uint32_t i = 0U; i < frames_read; ++i)
        {
            const uint8_t *const frame =
                &g_waveform_cache_io[i * (uint32_t)job->header.block_align];
            job->next_frame++;
            const uint8_t last = (job->next_frame >= job->header.frame_count) ? 1U : 0U;
            const int16_t sample = waveform_cache_frame_to_i16(frame,
                                                               job->header.channels,
                                                               job->header.bits_per_sample);
            if(waveform_cache_accumulate_frame(&cache_fp, job, sample, last) == 0U)
            {
                accum_ok = 0U;
                break;
            }
        }
        if(accum_ok == 0U)
        {
            break;
        }
        g_waveform_cache.diag.frames_done = job->next_frame;
        if(job->next_frame >= job->header.frame_count)
        {
            if(waveform_cache_write_header(&cache_fp, job, (uint8_t)WAVEFORM_CACHE_STATE_READY) == 0U)
            {
                break;
            }
            if(f_sync(&cache_fp) != FR_OK)
            {
                break;
            }
            memset(job, 0, sizeof(*job));
            g_waveform_cache.diag.status = WAVEFORM_CACHE_STATUS_READY;
            g_waveform_cache.diag.jobs_done++;
        }
        ok = 1U;
    } while(0);

    if(cache_open != 0U)
    {
        (void)f_close(&cache_fp);
    }
    if(wav_open != 0U)
    {
        (void)f_close(&wav_fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
    if(ok == 0U)
    {
        waveform_cache_fail_active();
    }
}

static void waveform_cache_service_tile_request(uint32_t byte_budget)
{
    int8_t req_idx = -1;
    for(uint8_t i = 0U; i < WAVEFORM_CACHE_TILE_QUEUE_CAPACITY; ++i)
    {
        if(g_waveform_cache.tile_queue[i].pending == 0U)
        {
            continue;
        }
        if((req_idx < 0)
                || ((uint8_t)g_waveform_cache.tile_queue[i].reason
                    < (uint8_t)g_waveform_cache.tile_queue[(uint8_t)req_idx].reason))
        {
            req_idx = (int8_t)i;
        }
    }
    if(req_idx < 0)
    {
        return;
    }

    waveform_cache_tile_request_t req = g_waveform_cache.tile_queue[(uint8_t)req_idx];
    if(waveform_cache_find_ram_tile(&req.handle, req.level_id, req.tile_index) >= 0)
    {
        memset(&g_waveform_cache.tile_queue[(uint8_t)req_idx],
               0,
               sizeof(g_waveform_cache.tile_queue[(uint8_t)req_idx]));
        return;
    }
    if(byte_budget < (WAVEFORM_CACHE_TILE_COLUMNS * sizeof(waveform_cache_minmax_t)))
    {
        return;
    }
    if(sample_cache_has_pending_sd_work() != 0U
            || multi_record_writer_any_active() != 0U
            || looper_storage_raw_export_is_active() != 0U
            || sd_preview_is_active() != 0U
            || pattern_load_is_pending() != 0U)
    {
        return;
    }

    const int16_t slot = waveform_cache_pick_tile_slot();
    if(slot < 0)
    {
        return;
    }
    g_waveform_cache_ram_tiles[(uint16_t)slot].valid = 0U;
    g_waveform_cache_ram_tiles[(uint16_t)slot].loading = 1U;

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
    {
        g_waveform_cache_ram_tiles[(uint16_t)slot].loading = 0U;
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    char cache_path[96U];
    waveform_cache_file_header_t header;
    waveform_cache_file_level_t table[WAVEFORM_CACHE_FORMAT_LEVEL_COUNT];
    waveform_cache_make_cache_path(req.handle.sample_id, cache_path, sizeof(cache_path));
    do
    {
        if(sd_access_fs_mount_if_needed() == 0U)
        {
            break;
        }
        if(waveform_cache_read_ready_header(req.handle.sample_id, &header, table) == 0U)
        {
            break;
        }
        if(req.level_id >= WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT)
        {
            break;
        }
        const waveform_cache_file_level_t *const level = &table[(uint8_t)req.level_id];
        const uint32_t column_start = req.tile_index * WAVEFORM_CACHE_TILE_COLUMNS;
        if(column_start >= level->column_count)
        {
            break;
        }
        uint32_t column_count = level->column_count - column_start;
        if(column_count > WAVEFORM_CACHE_TILE_COLUMNS)
        {
            column_count = WAVEFORM_CACHE_TILE_COLUMNS;
        }
        if(f_open(&fp, cache_path, FA_READ) != FR_OK)
        {
            break;
        }
        file_open = 1U;
        if(f_lseek(&fp,
                   level->data_offset
                       + (column_start * sizeof(waveform_cache_minmax_t))) != FR_OK)
        {
            break;
        }
        UINT br = 0U;
        const uint32_t bytes = column_count * sizeof(waveform_cache_minmax_t);
        if((f_read(&fp, g_waveform_cache_ram_tiles[(uint16_t)slot].columns, bytes, &br) != FR_OK)
                || (br != bytes))
        {
            break;
        }
        if(column_count < WAVEFORM_CACHE_TILE_COLUMNS)
        {
            memset(&g_waveform_cache_ram_tiles[(uint16_t)slot].columns[column_count],
                   0,
                   (WAVEFORM_CACHE_TILE_COLUMNS - column_count)
                       * sizeof(waveform_cache_minmax_t));
        }
        memcpy(g_waveform_cache_ram_tiles[(uint16_t)slot].sample_id,
               req.handle.sample_id,
               WAVEFORM_CACHE_SAMPLE_ID_BYTES);
        g_waveform_cache_ram_tiles[(uint16_t)slot].level_id = (uint16_t)req.level_id;
        g_waveform_cache_ram_tiles[(uint16_t)slot].tile_index = req.tile_index;
        g_waveform_cache_ram_tiles[(uint16_t)slot].column_count = column_count;
        g_waveform_cache_ram_tiles[(uint16_t)slot].last_used = ++g_waveform_cache.tile_lru_tick;
        g_waveform_cache_ram_tiles[(uint16_t)slot].valid = 1U;
        ok = 1U;
    } while(0);

    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    g_waveform_cache_ram_tiles[(uint16_t)slot].loading = 0U;
    sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);

    if(ok != 0U)
    {
        memset(&g_waveform_cache.tile_queue[(uint8_t)req_idx],
               0,
               sizeof(g_waveform_cache.tile_queue[(uint8_t)req_idx]));
    }
}

void waveform_cache_service(uint32_t byte_budget)
{
    const uint8_t trace_service = waveform_cache_has_service_work();
    if(trace_service != 0U)
    {
        waveform_cache_debug_mark(REC_LIVE_DEBUG_WAVECACHE_SERVICE_ENTER,
                                  g_waveform_cache.active.wav_path,
                                  (uint32_t)g_waveform_cache.diag.status);
    }
    if(g_waveform_cache.service_defer_passes != 0U)
    {
        g_waveform_cache.service_defer_passes--;
        if(trace_service != 0U)
        {
            waveform_cache_debug_mark(REC_LIVE_DEBUG_WAVECACHE_SERVICE_EXIT,
                                      g_waveform_cache.active.wav_path,
                                      (uint32_t)g_waveform_cache.diag.status);
        }
        return;
    }
    waveform_cache_service_tile_request(byte_budget);
    if(g_waveform_cache.active.state == WAVEFORM_CACHE_JOB_EMPTY)
    {
        (void)waveform_cache_start_next_job();
    }
    if(g_waveform_cache.active.state == WAVEFORM_CACHE_JOB_VALIDATE)
    {
        waveform_cache_service_validate();
    }
    else if(g_waveform_cache.active.state == WAVEFORM_CACHE_JOB_BUILDING)
    {
        waveform_cache_service_build(byte_budget);
    }
    if(trace_service != 0U)
    {
        waveform_cache_debug_mark(REC_LIVE_DEBUG_WAVECACHE_SERVICE_EXIT,
                                  g_waveform_cache.active.wav_path,
                                  (uint32_t)g_waveform_cache.diag.status);
    }
}

void waveform_cache_get_diag(waveform_cache_diag_t *out_diag)
{
    if(out_diag != 0)
    {
        *out_diag = g_waveform_cache.diag;
    }
}

uint8_t waveform_cache_level_frames_per_column(waveform_cache_level_id_t level_id,
                                               uint32_t *out_frames_per_column)
{
    if((level_id >= WAVEFORM_CACHE_LEVEL_COUNT) || (out_frames_per_column == 0))
    {
        return 0U;
    }
    *out_frames_per_column = g_waveform_cache_level_frames[level_id];
    return 1U;
}

uint8_t waveform_cache_choose_level(uint32_t frames_per_pixel,
                                    waveform_cache_level_id_t *out_level_id)
{
    if(out_level_id == 0)
    {
        return 0U;
    }
    if(frames_per_pixel >= 8192U)
    {
        *out_level_id = WAVEFORM_CACHE_LEVEL_L0_COARSE;
    }
    else if(frames_per_pixel >= 2048U)
    {
        *out_level_id = WAVEFORM_CACHE_LEVEL_L1_GLOBAL;
    }
    else if(frames_per_pixel >= 512U)
    {
        *out_level_id = WAVEFORM_CACHE_LEVEL_L2_MID;
    }
    else
    {
        *out_level_id = WAVEFORM_CACHE_LEVEL_L3_FINE;
    }
    return 1U;
}

uint8_t waveform_cache_open_for_wav(const char *path, waveform_cache_handle_t *out_handle)
{
    if((path == 0) || (out_handle == 0) || (waveform_cache_path_is_temporary(path) != 0U))
    {
        return 0U;
    }
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
    {
        return 0U;
    }

    waveform_cache_file_header_t identity;
    waveform_cache_file_header_t cached;
    waveform_cache_file_level_t table[WAVEFORM_CACHE_FORMAT_LEVEL_COUNT];
    uint8_t ok = 0U;
    uint8_t request_allowed = 1U;
    do
    {
        if(waveform_cache_build_identity(path, &identity) == 0U)
        {
            break;
        }
        if(waveform_cache_duration_is_persistable(identity.frame_count,
                                                  identity.sample_rate) == 0U)
        {
            request_allowed = 0U;
            break;
        }
        char cache_path[96U];
        waveform_cache_make_cache_path(identity.sample_id, cache_path, sizeof(cache_path));
        (void)cache_path;
        if(waveform_cache_read_ready_header(identity.sample_id, &cached, table) == 0U)
        {
            break;
        }
        if(waveform_cache_header_matches(&cached, &identity) == 0U)
        {
            break;
        }
        memset(out_handle, 0, sizeof(*out_handle));
        memcpy(out_handle->sample_id, identity.sample_id, WAVEFORM_CACHE_SAMPLE_ID_BYTES);
        out_handle->frame_count = identity.frame_count;
        out_handle->sample_rate = identity.sample_rate;
        out_handle->channels = identity.channels;
        out_handle->bits_per_sample = identity.bits_per_sample;
        ok = 1U;
    } while(0);

    sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
    if((ok == 0U) && (request_allowed != 0U))
    {
        (void)waveform_cache_request_for_wav(path, WAVEFORM_CACHE_REASON_EDITOR_VISIBLE);
    }
    return ok;
}

uint8_t waveform_cache_request_tiles(const waveform_cache_handle_t *handle,
                                     waveform_cache_level_id_t level_id,
                                     uint32_t tile_start,
                                     uint32_t tile_count,
                                     waveform_cache_reason_t reason)
{
    if((handle == 0) || (level_id >= WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT) || (tile_count == 0U))
    {
        return 0U;
    }
    uint8_t queued_any = 0U;
    for(uint32_t t = 0U; t < tile_count; ++t)
    {
        const uint32_t tile_index = tile_start + t;
        if(waveform_cache_find_ram_tile(handle, level_id, tile_index) >= 0)
        {
            continue;
        }
        if(waveform_cache_tile_request_exists(handle, level_id, tile_index) != 0U)
        {
            queued_any = 1U;
            continue;
        }
        for(uint8_t i = 0U; i < WAVEFORM_CACHE_TILE_QUEUE_CAPACITY; ++i)
        {
            if(g_waveform_cache.tile_queue[i].pending != 0U)
            {
                continue;
            }
            memset(&g_waveform_cache.tile_queue[i], 0, sizeof(g_waveform_cache.tile_queue[i]));
            g_waveform_cache.tile_queue[i].pending = 1U;
            g_waveform_cache.tile_queue[i].handle = *handle;
            g_waveform_cache.tile_queue[i].level_id = level_id;
            g_waveform_cache.tile_queue[i].tile_index = tile_index;
            g_waveform_cache.tile_queue[i].reason = reason;
            queued_any = 1U;
            break;
        }
    }
    return queued_any;
}

uint8_t waveform_cache_tiles_ready(const waveform_cache_handle_t *handle,
                                   waveform_cache_level_id_t level_id,
                                   uint32_t tile_start,
                                   uint32_t tile_count)
{
    if((handle == 0) || (level_id >= WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT) || (tile_count == 0U))
    {
        return 0U;
    }
    for(uint32_t t = 0U; t < tile_count; ++t)
    {
        if(waveform_cache_find_ram_tile(handle, level_id, tile_start + t) < 0)
        {
            return 0U;
        }
    }
    return 1U;
}

uint8_t waveform_cache_minmax_from_ram(const waveform_cache_handle_t *handle,
                                       waveform_cache_level_id_t level_id,
                                       uint32_t column_start,
                                       uint32_t column_count,
                                       int16_t *out_min,
                                       int16_t *out_max)
{
    if((handle == 0) || (out_min == 0) || (out_max == 0)
            || (level_id >= WAVEFORM_CACHE_ACTIVE_LEVEL_COUNT) || (column_count == 0U))
    {
        return 0U;
    }
    uint8_t seen = 0U;
    int16_t min_v = 0;
    int16_t max_v = 0;
    uint32_t cursor = column_start;
    uint32_t remaining = column_count;
    while(remaining != 0U)
    {
        const uint32_t tile_index = cursor / WAVEFORM_CACHE_TILE_COLUMNS;
        const uint32_t tile_col = cursor % WAVEFORM_CACHE_TILE_COLUMNS;
        const int16_t slot = waveform_cache_find_ram_tile(handle, level_id, tile_index);
        if(slot < 0)
        {
            return 0U;
        }
        waveform_cache_ram_tile_t *const tile = &g_waveform_cache_ram_tiles[(uint16_t)slot];
        tile->last_used = ++g_waveform_cache.tile_lru_tick;
        if(tile_col >= tile->column_count)
        {
            return 0U;
        }
        uint32_t span = tile->column_count - tile_col;
        if(span > remaining)
        {
            span = remaining;
        }
        for(uint32_t i = 0U; i < span; ++i)
        {
            const waveform_cache_minmax_t *const c = &tile->columns[tile_col + i];
            if(seen == 0U)
            {
                min_v = c->min;
                max_v = c->max;
                seen = 1U;
            }
            else
            {
                if(c->min < min_v) { min_v = c->min; }
                if(c->max > max_v) { max_v = c->max; }
            }
        }
        cursor += span;
        remaining -= span;
    }
    if(seen == 0U)
    {
        return 0U;
    }
    *out_min = min_v;
    *out_max = max_v;
    return 1U;
}
