#include "Storage/sampler_waveform_store.h"

#include <stddef.h>
#include <string.h>

#include "Platform/memory_layout.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "Storage/waveform_service.h"
#include "ff.h"
#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"

#define SAMPLER_WAVEFORM_PATH "0:/BRICK/SAMPLE.WFM"
#define SAMPLER_WAVEFORM_MAGIC 0x314D4657UL /* WFM1 */
#define SAMPLER_WAVEFORM_RECORD_MAGIC 0x31524657UL /* WFR1 */
#define SAMPLER_WAVEFORM_VERSION 1U
#define SAMPLER_WAVEFORM_BUCKET_COUNT 4096U
#define SAMPLER_WAVEFORM_EMPTY_OFFSET 0xFFFFFFFFUL
#define SAMPLER_WAVEFORM_HEADER_BYTES 64U
#define SAMPLER_WAVEFORM_TABLE_BYTES (SAMPLER_WAVEFORM_BUCKET_COUNT * 4U)
#define SAMPLER_WAVEFORM_RECORD_BASE \
    (SAMPLER_WAVEFORM_HEADER_BYTES + SAMPLER_WAVEFORM_TABLE_BYTES)
#define SAMPLER_WAVEFORM_READ_BYTES 4096U
#define SAMPLER_WAVEFORM_MAX_CHAIN 64U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint16_t record_size;
    uint16_t bucket_count;
    uint32_t record_count;
    uint32_t generation;
    uint32_t header_crc;
    uint32_t reserved[10];
} sampler_waveform_file_header_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint64_t identity_hash;
    uint64_t path_hash;
    uint32_t state;
    uint32_t file_size;
    uint32_t fat_date_time;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint32_t data_offset;
    uint32_t source_signature;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t next_offset;
    uint32_t record_crc;
    sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT];
} sampler_waveform_record_t;

typedef enum
{
    SAMPLER_WAVEFORM_SOURCE_EMPTY = 0,
    SAMPLER_WAVEFORM_SOURCE_LOOKUP,
    SAMPLER_WAVEFORM_SOURCE_ON_DISK,
    SAMPLER_WAVEFORM_SOURCE_LOADING,
    SAMPLER_WAVEFORM_SOURCE_MISSING,
    SAMPLER_WAVEFORM_SOURCE_BUILDING,
    SAMPLER_WAVEFORM_SOURCE_READY,
    SAMPLER_WAVEFORM_SOURCE_ERROR
} sampler_waveform_source_state_t;

typedef struct
{
    sampler_waveform_identity_t identity;
    uint32_t record_offset;
    uint32_t media_epoch;
    sampler_waveform_source_state_t state;
    uint8_t registered;
    uint8_t fresh_persistent;
} sampler_waveform_source_t;

typedef struct
{
    sampler_waveform_identity_t identity;
    uint32_t media_epoch;
    uint16_t sample_id;
    uint8_t active;
} sampler_waveform_request_t;

typedef enum
{
    SAMPLER_WAVEFORM_IO_OK = 0,
    SAMPLER_WAVEFORM_IO_NOT_FOUND,
    SAMPLER_WAVEFORM_IO_ERROR,
    SAMPLER_WAVEFORM_IO_CORRUPT
} sampler_waveform_io_result_t;

typedef struct
{
    sampler_waveform_identity_t identity;
    sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT];
    uint32_t age;
    uint8_t valid;
} sampler_waveform_ram_record_t;

typedef struct
{
    FIL file;
    sampler_waveform_identity_t identity;
    sampler_waveform_accumulator_t accumulator;
    char path[SAMPLE_GLOBAL_POOL_PATH_MAX];
    uint32_t next_frame;
    uint32_t media_epoch;
    uint16_t sample_id;
    uint8_t file_open;
    uint8_t active;
    uint8_t cancel_requested;
} sampler_waveform_build_t;

_Static_assert(sizeof(sampler_waveform_file_header_t) == SAMPLER_WAVEFORM_HEADER_BYTES,
               "sampler waveform header ABI");
_Static_assert(sizeof(sampler_waveform_record_t) == SAMPLER_WAVEFORM_RECORD_BYTES,
               "sampler waveform record ABI");

STORAGE_STATE_SDRAM static sampler_waveform_source_t
    g_sampler_waveform_sources[SAMPLE_CLASSIC_CAPACITY];
STORAGE_STATE_SDRAM static sampler_waveform_ram_record_t
    g_sampler_waveform_ram[SAMPLER_WAVEFORM_RAM_RECORD_COUNT];
STORAGE_STATE_SDRAM static sampler_waveform_build_t g_sampler_waveform_build;
STORAGE_STATE_SDRAM static sampler_waveform_request_t g_sampler_waveform_request;
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sampler_waveform_read[SAMPLER_WAVEFORM_READ_BYTES];
static uint32_t g_sampler_waveform_age;

static uint32_t sampler_waveform_crc_update(uint32_t crc,
                                            const uint8_t *data,
                                            uint32_t size)
{
    while(size-- != 0U)
    {
        crc ^= *data++;
        for(uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ ((0U - (crc & 1U)) & 0xEDB88320UL);
    }
    return crc;
}

static uint32_t sampler_waveform_crc(const void *data, uint32_t size)
{
    return ~sampler_waveform_crc_update(0xFFFFFFFFUL,
        (const uint8_t *)data, size);
}

static uint64_t sampler_waveform_hash_update(uint64_t hash,
                                             const uint8_t *data,
                                             uint32_t size)
{
    while(size-- != 0U)
    {
        hash ^= *data++;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint8_t sampler_waveform_path_char(uint8_t c)
{
    if(c == '\\') c = '/';
    if(c >= 'a' && c <= 'z') c = (uint8_t)(c - ('a' - 'A'));
    return c;
}

static uint64_t sampler_waveform_path_hash(const char *path)
{
    uint64_t hash = 1469598103934665603ULL;
    if(path == 0) return hash;
    while(*path != '\0')
    {
        const uint8_t c = sampler_waveform_path_char((uint8_t)*path++);
        hash = sampler_waveform_hash_update(hash, &c, 1U);
    }
    return hash;
}

static uint64_t sampler_waveform_identity_hash(
    const sampler_waveform_identity_t *identity)
{
    uint64_t hash = 1469598103934665603ULL;
    hash = sampler_waveform_hash_update(hash,
        (const uint8_t *)&identity->path_hash, sizeof(identity->path_hash));
    hash = sampler_waveform_hash_update(hash,
        (const uint8_t *)&identity->file_size,
        (uint32_t)(sizeof(*identity) - offsetof(sampler_waveform_identity_t, file_size)));
    return hash;
}

static uint8_t sampler_waveform_identity_equal(
    const sampler_waveform_identity_t *a,
    const sampler_waveform_identity_t *b)
{
    return (uint8_t)(a->identity_hash == b->identity_hash
        && a->path_hash == b->path_hash
        && a->file_size == b->file_size
        && a->fat_date_time == b->fat_date_time
        && a->frame_count == b->frame_count
        && a->sample_rate == b->sample_rate
        && a->data_offset == b->data_offset
        && a->source_signature == b->source_signature
        && a->channels == b->channels
        && a->bits_per_sample == b->bits_per_sample);
}

static int16_t sampler_waveform_float_to_i16(float value)
{
    if(value != value) value = 0.0f;
    if(value >= 1.0f) return 32767;
    if(value <= -1.0f) return -32768;
    return (int16_t)(value * 32768.0f);
}

void sampler_waveform_accumulator_init(sampler_waveform_accumulator_t *acc,
                                       uint32_t frame_count)
{
    if(acc == 0) return;
    memset(acc, 0, sizeof(*acc));
    acc->frame_count = frame_count;
}

static void sampler_waveform_accumulator_push_i16(
    sampler_waveform_accumulator_t *acc, int16_t left, int16_t right)
{
    if(acc == 0 || acc->frame_count == 0U
            || acc->next_frame >= acc->frame_count) return;
    uint32_t bin = (uint32_t)(((uint64_t)acc->next_frame
        * SAMPLER_WAVEFORM_BIN_COUNT) / acc->frame_count);
    if(bin >= SAMPLER_WAVEFORM_BIN_COUNT)
        bin = SAMPLER_WAVEFORM_BIN_COUNT - 1U;
    const int16_t frame_min = (left < right) ? left : right;
    const int16_t frame_max = (left > right) ? left : right;
    const uint8_t mask = (uint8_t)(1U << (bin & 7U));
    if((acc->seen[bin >> 3U] & mask) == 0U)
    {
        acc->bins[bin].min = frame_min;
        acc->bins[bin].max = frame_max;
        acc->seen[bin >> 3U] |= mask;
    }
    else
    {
        if(frame_min < acc->bins[bin].min) acc->bins[bin].min = frame_min;
        if(frame_max > acc->bins[bin].max) acc->bins[bin].max = frame_max;
    }
    acc->next_frame++;
}

void sampler_waveform_accumulator_push_float(sampler_waveform_accumulator_t *acc,
                                             const float *stereo,
                                             uint32_t frame_count)
{
    if(acc == 0 || stereo == 0) return;
    for(uint32_t frame = 0U; frame < frame_count; ++frame)
    {
        sampler_waveform_accumulator_push_i16(acc,
            sampler_waveform_float_to_i16(stereo[frame * 2U]),
            sampler_waveform_float_to_i16(stereo[frame * 2U + 1U]));
    }
}

uint8_t sampler_waveform_accumulator_finish(sampler_waveform_accumulator_t *acc,
                                            sampler_waveform_bin_t out_bins[SAMPLER_WAVEFORM_BIN_COUNT])
{
    if(acc == 0 || out_bins == 0 || acc->frame_count == 0U
            || acc->next_frame != acc->frame_count) return 0U;
    int32_t first = -1;
    for(uint32_t bin = 0U; bin < SAMPLER_WAVEFORM_BIN_COUNT; ++bin)
    {
        if((acc->seen[bin >> 3U] & (1U << (bin & 7U))) != 0U)
        {
            first = (int32_t)bin;
            break;
        }
    }
    if(first < 0) return 0U;
    for(int32_t bin = first - 1; bin >= 0; --bin)
        acc->bins[(uint32_t)bin] = acc->bins[(uint32_t)first];
    uint32_t previous = (uint32_t)first;
    for(uint32_t bin = (uint32_t)first + 1U;
            bin < SAMPLER_WAVEFORM_BIN_COUNT; ++bin)
    {
        if((acc->seen[bin >> 3U] & (1U << (bin & 7U))) != 0U)
            previous = bin;
        else
            acc->bins[bin] = acc->bins[previous];
    }
    if(out_bins != acc->bins)
        memcpy(out_bins, acc->bins, sizeof(acc->bins));
    return 1U;
}

static uint8_t sampler_waveform_edge_signature(FIL *file,
                                               const wav_info_t *info,
                                               uint32_t *out_signature)
{
    if(file == 0 || info == 0 || info->block_align == 0U
            || info->data_size < info->block_align
            || out_signature == 0) return 0U;
    uint8_t edge[16] = {0};
    const uint32_t bytes = (info->block_align < 8U) ? info->block_align : 8U;
    UINT read = 0U;
    if(f_lseek(file, info->data_offset) != FR_OK
            || f_read(file, edge, bytes, &read) != FR_OK || read != bytes)
        return 0U;
    read = 0U;
    if(f_lseek(file, info->data_offset + info->data_size - info->block_align) != FR_OK
            || f_read(file, &edge[8], bytes, &read) != FR_OK || read != bytes)
        return 0U;
    *out_signature = sampler_waveform_crc(edge, sizeof(edge));
    return 1U;
}

static uint8_t sampler_waveform_make_identity_open(
    const char *path, const wav_info_t *info, FIL *file,
    sampler_waveform_identity_t *out)
{
    FILINFO stat;
    if(path == 0 || info == 0 || file == 0 || out == 0
            || f_stat(path, &stat) != FR_OK || info->block_align == 0U)
        return 0U;
    memset(out, 0, sizeof(*out));
    out->path_hash = sampler_waveform_path_hash(path);
    out->file_size = (uint32_t)stat.fsize;
    out->fat_date_time = ((uint32_t)stat.fdate << 16U) | stat.ftime;
    out->frame_count = info->data_size / info->block_align;
    out->sample_rate = info->sample_rate;
    out->data_offset = info->data_offset;
    if(sampler_waveform_edge_signature(file, info,
            &out->source_signature) == 0U) return 0U;
    out->channels = info->channels;
    out->bits_per_sample = info->bits_per_sample;
    if(out->frame_count == 0U) return 0U;
    out->identity_hash = sampler_waveform_identity_hash(out);
    return 1U;
}

static uint32_t sampler_waveform_header_crc(
    const sampler_waveform_file_header_t *header)
{
    sampler_waveform_file_header_t copy = *header;
    copy.header_crc = 0U;
    return sampler_waveform_crc(&copy, sizeof(copy));
}

static uint32_t sampler_waveform_record_crc(
    const sampler_waveform_record_t *record)
{
    sampler_waveform_record_t copy = *record;
    copy.record_crc = 0U;
    return sampler_waveform_crc(&copy, sizeof(copy));
}

static uint8_t sampler_waveform_header_valid(
    const sampler_waveform_file_header_t *header)
{
    return (uint8_t)(header->magic == SAMPLER_WAVEFORM_MAGIC
        && header->version == SAMPLER_WAVEFORM_VERSION
        && header->header_size == SAMPLER_WAVEFORM_HEADER_BYTES
        && header->record_size == SAMPLER_WAVEFORM_RECORD_BYTES
        && header->bucket_count == SAMPLER_WAVEFORM_BUCKET_COUNT
        && header->header_crc == sampler_waveform_header_crc(header));
}

static uint8_t sampler_waveform_create_file(FIL *file)
{
    if(f_open(file, SAMPLER_WAVEFORM_PATH,
            FA_CREATE_ALWAYS | FA_READ | FA_WRITE) != FR_OK) return 0U;
    sampler_waveform_file_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = SAMPLER_WAVEFORM_MAGIC;
    header.version = SAMPLER_WAVEFORM_VERSION;
    header.header_size = SAMPLER_WAVEFORM_HEADER_BYTES;
    header.record_size = SAMPLER_WAVEFORM_RECORD_BYTES;
    header.bucket_count = SAMPLER_WAVEFORM_BUCKET_COUNT;
    header.generation = 1U;
    header.header_crc = sampler_waveform_header_crc(&header);
    UINT written = 0U;
    if(f_write(file, &header, sizeof(header), &written) != FR_OK
            || written != sizeof(header))
    {
        (void)f_close(file);
        return 0U;
    }
    uint32_t empty[64];
    for(uint32_t i = 0U; i < 64U; ++i) empty[i] = SAMPLER_WAVEFORM_EMPTY_OFFSET;
    for(uint32_t done = 0U; done < SAMPLER_WAVEFORM_BUCKET_COUNT; done += 64U)
    {
        if(f_write(file, empty, sizeof(empty), &written) != FR_OK
                || written != sizeof(empty))
        {
            (void)f_close(file);
            return 0U;
        }
    }
    if(f_sync(file) != FR_OK)
    {
        (void)f_close(file);
        return 0U;
    }
    return 1U;
}

static sampler_waveform_io_result_t sampler_waveform_open_store_read(
    FIL *file, sampler_waveform_file_header_t *header)
{
    const FRESULT opened = f_open(file, SAMPLER_WAVEFORM_PATH,
                                  FA_READ | FA_OPEN_EXISTING);
    if(opened == FR_NO_FILE || opened == FR_NO_PATH)
        return SAMPLER_WAVEFORM_IO_NOT_FOUND;
    if(opened != FR_OK) return SAMPLER_WAVEFORM_IO_ERROR;
    UINT read = 0U;
    const FRESULT seek = f_lseek(file, 0U);
    const FRESULT result = (seek == FR_OK)
        ? f_read(file, header, sizeof(*header), &read) : seek;
    if(result != FR_OK)
    {
        (void)f_close(file);
        return SAMPLER_WAVEFORM_IO_ERROR;
    }
    if(read != sizeof(*header) || sampler_waveform_header_valid(header) == 0U)
    {
        (void)f_close(file);
        return SAMPLER_WAVEFORM_IO_CORRUPT;
    }
    return SAMPLER_WAVEFORM_IO_OK;
}

static uint8_t sampler_waveform_open_store_write(
    FIL *file, sampler_waveform_file_header_t *header)
{
    sampler_waveform_io_result_t result =
        sampler_waveform_open_store_read(file, header);
    if(result == SAMPLER_WAVEFORM_IO_OK)
    {
        (void)f_close(file);
        if(f_open(file, SAMPLER_WAVEFORM_PATH,
                FA_READ | FA_WRITE | FA_OPEN_EXISTING) != FR_OK) return 0U;
        return 1U;
    }
    if(result == SAMPLER_WAVEFORM_IO_ERROR) return 0U;
    if(sampler_waveform_create_file(file) == 0U) return 0U;
    *header = (sampler_waveform_file_header_t){0};
    UINT read = 0U;
    const uint8_t valid = (uint8_t)(f_lseek(file, 0U) == FR_OK
        && f_read(file, header, sizeof(*header), &read) == FR_OK
        && read == sizeof(*header)
        && sampler_waveform_header_valid(header) != 0U);
    if(valid == 0U) (void)f_close(file);
    return valid;
}

static uint8_t sampler_waveform_record_valid(
    const sampler_waveform_record_t *record)
{
    return (uint8_t)(record->magic == SAMPLER_WAVEFORM_RECORD_MAGIC
        && record->version == SAMPLER_WAVEFORM_VERSION
        && record->record_size == SAMPLER_WAVEFORM_RECORD_BYTES
        && record->state == 1U
        && record->record_crc == sampler_waveform_record_crc(record));
}

static void sampler_waveform_record_identity(
    const sampler_waveform_record_t *record,
    sampler_waveform_identity_t *identity)
{
    *identity = (sampler_waveform_identity_t){
        .identity_hash = record->identity_hash,
        .path_hash = record->path_hash,
        .file_size = record->file_size,
        .fat_date_time = record->fat_date_time,
        .frame_count = record->frame_count,
        .sample_rate = record->sample_rate,
        .data_offset = record->data_offset,
        .source_signature = record->source_signature,
        .channels = record->channels,
        .bits_per_sample = record->bits_per_sample,
    };
}

static sampler_waveform_io_result_t sampler_waveform_read_record(
    FIL *file, uint32_t offset, sampler_waveform_record_t *record)
{
    if(offset < SAMPLER_WAVEFORM_RECORD_BASE)
        return SAMPLER_WAVEFORM_IO_CORRUPT;
    UINT read = 0U;
    if(f_lseek(file, offset) != FR_OK
            || f_read(file, record, sizeof(*record), &read) != FR_OK)
        return SAMPLER_WAVEFORM_IO_ERROR;
    if(read != sizeof(*record) || sampler_waveform_record_valid(record) == 0U)
        return SAMPLER_WAVEFORM_IO_CORRUPT;
    return SAMPLER_WAVEFORM_IO_OK;
}

static sampler_waveform_io_result_t sampler_waveform_lookup_open(
    FIL *file, const sampler_waveform_identity_t *identity,
    uint32_t *out_offset, sampler_waveform_record_t *out_record)
{
    const uint32_t bucket = (uint32_t)identity->identity_hash
        & (SAMPLER_WAVEFORM_BUCKET_COUNT - 1U);
    uint32_t offset = SAMPLER_WAVEFORM_EMPTY_OFFSET;
    UINT read = 0U;
    if(f_lseek(file, SAMPLER_WAVEFORM_HEADER_BYTES + bucket * 4U) != FR_OK
            || f_read(file, &offset, sizeof(offset), &read) != FR_OK
            || read != sizeof(offset)) return SAMPLER_WAVEFORM_IO_ERROR;
    for(uint32_t chain = 0U; offset != SAMPLER_WAVEFORM_EMPTY_OFFSET
            && chain < SAMPLER_WAVEFORM_MAX_CHAIN; ++chain)
    {
        sampler_waveform_record_t record;
        const sampler_waveform_io_result_t record_result =
            sampler_waveform_read_record(file, offset, &record);
        if(record_result != SAMPLER_WAVEFORM_IO_OK) return record_result;
        sampler_waveform_identity_t candidate;
        sampler_waveform_record_identity(&record, &candidate);
        if(sampler_waveform_identity_equal(&candidate, identity) != 0U)
        {
            if(out_offset != 0) *out_offset = offset;
            if(out_record != 0) *out_record = record;
            return SAMPLER_WAVEFORM_IO_OK;
        }
        offset = record.next_offset;
    }
    return (offset == SAMPLER_WAVEFORM_EMPTY_OFFSET)
        ? SAMPLER_WAVEFORM_IO_NOT_FOUND : SAMPLER_WAVEFORM_IO_CORRUPT;
}

static uint8_t sampler_waveform_commit_locked(
    const sampler_waveform_identity_t *identity,
    const sampler_waveform_bin_t *bins,
    uint32_t *out_offset)
{
    FIL file;
    sampler_waveform_file_header_t header;
    if(sampler_waveform_open_store_write(&file, &header) == 0U) return 0U;
    uint32_t existing = 0U;
    sampler_waveform_record_t old_record;
    const sampler_waveform_io_result_t lookup =
        sampler_waveform_lookup_open(&file, identity, &existing, &old_record);
    if(lookup == SAMPLER_WAVEFORM_IO_ERROR)
    {
        (void)f_close(&file);
        return 0U;
    }
    if(lookup == SAMPLER_WAVEFORM_IO_OK)
    {
        if(memcmp(old_record.bins, bins, sizeof(old_record.bins)) == 0)
        {
            if(out_offset != 0) *out_offset = existing;
            (void)f_close(&file);
            return 1U;
        }
    }
    const uint32_t bucket = (uint32_t)identity->identity_hash
        & (SAMPLER_WAVEFORM_BUCKET_COUNT - 1U);
    uint32_t head = SAMPLER_WAVEFORM_EMPTY_OFFSET;
    UINT transferred = 0U;
    if(f_lseek(&file, SAMPLER_WAVEFORM_HEADER_BYTES + bucket * 4U) != FR_OK
            || f_read(&file, &head, sizeof(head), &transferred) != FR_OK
            || transferred != sizeof(head))
    {
        (void)f_close(&file);
        return 0U;
    }
    uint32_t offset = (uint32_t)f_size(&file);
    if(offset < SAMPLER_WAVEFORM_RECORD_BASE) offset = SAMPLER_WAVEFORM_RECORD_BASE;
    sampler_waveform_record_t record;
    memset(&record, 0, sizeof(record));
    record.magic = SAMPLER_WAVEFORM_RECORD_MAGIC;
    record.version = SAMPLER_WAVEFORM_VERSION;
    record.record_size = SAMPLER_WAVEFORM_RECORD_BYTES;
    record.state = 1U;
    record.identity_hash = identity->identity_hash;
    record.path_hash = identity->path_hash;
    record.file_size = identity->file_size;
    record.fat_date_time = identity->fat_date_time;
    record.frame_count = identity->frame_count;
    record.sample_rate = identity->sample_rate;
    record.data_offset = identity->data_offset;
    record.source_signature = identity->source_signature;
    record.channels = identity->channels;
    record.bits_per_sample = identity->bits_per_sample;
    record.next_offset = head;
    memcpy(record.bins, bins, sizeof(record.bins));
    record.record_crc = sampler_waveform_record_crc(&record);
    if(f_lseek(&file, offset) != FR_OK
            || f_write(&file, &record, sizeof(record), &transferred) != FR_OK
            || transferred != sizeof(record) || f_sync(&file) != FR_OK
            || f_lseek(&file, SAMPLER_WAVEFORM_HEADER_BYTES + bucket * 4U) != FR_OK
            || f_write(&file, &offset, sizeof(offset), &transferred) != FR_OK
            || transferred != sizeof(offset) || f_sync(&file) != FR_OK)
    {
        (void)f_close(&file);
        return 0U;
    }
    (void)f_close(&file);
    if(out_offset != 0) *out_offset = offset;
    return 1U;
}

static int8_t sampler_waveform_ram_find(
    const sampler_waveform_identity_t *identity)
{
    for(uint8_t i = 0U; i < SAMPLER_WAVEFORM_RAM_RECORD_COUNT; ++i)
    {
        if(g_sampler_waveform_ram[i].valid != 0U
                && sampler_waveform_identity_equal(
                    &g_sampler_waveform_ram[i].identity, identity) != 0U)
        {
            g_sampler_waveform_ram[i].age = ++g_sampler_waveform_age;
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t sampler_waveform_ram_put(
    const sampler_waveform_identity_t *identity,
    const sampler_waveform_bin_t *bins)
{
    for(uint8_t i = 0U; i < SAMPLER_WAVEFORM_RAM_RECORD_COUNT; ++i)
    {
        if(g_sampler_waveform_ram[i].valid != 0U
                && sampler_waveform_identity_equal(
                    &g_sampler_waveform_ram[i].identity, identity) != 0U)
        {
            memcpy(g_sampler_waveform_ram[i].bins, bins,
                   sizeof(g_sampler_waveform_ram[i].bins));
            g_sampler_waveform_ram[i].age = ++g_sampler_waveform_age;
            return i;
        }
    }
    uint8_t slot = 0U;
    uint32_t oldest = UINT32_MAX;
    for(uint8_t i = 0U; i < SAMPLER_WAVEFORM_RAM_RECORD_COUNT; ++i)
    {
        if(g_sampler_waveform_ram[i].valid == 0U) { slot = i; oldest = 0U; break; }
        if(g_sampler_waveform_ram[i].age < oldest)
        {
            oldest = g_sampler_waveform_ram[i].age;
            slot = i;
        }
    }
    if(g_sampler_waveform_ram[slot].valid != 0U)
    {
        const sampler_waveform_identity_t evicted =
            g_sampler_waveform_ram[slot].identity;
        for(uint16_t i = 0U; i < SAMPLE_CLASSIC_CAPACITY; ++i)
        {
            sampler_waveform_source_t *const source =
                &g_sampler_waveform_sources[i];
            if(source->registered != 0U
                    && source->state == SAMPLER_WAVEFORM_SOURCE_READY
                    && sampler_waveform_identity_equal(
                        &source->identity, &evicted) != 0U)
            {
                source->state = SAMPLER_WAVEFORM_SOURCE_ON_DISK;
            }
        }
    }
    g_sampler_waveform_ram[slot].identity = *identity;
    memcpy(g_sampler_waveform_ram[slot].bins, bins,
           sizeof(g_sampler_waveform_ram[slot].bins));
    g_sampler_waveform_ram[slot].age = ++g_sampler_waveform_age;
    g_sampler_waveform_ram[slot].valid = 1U;
    return slot;
}

void sampler_waveform_store_init(void)
{
    memset(g_sampler_waveform_sources, 0, sizeof(g_sampler_waveform_sources));
    memset(g_sampler_waveform_ram, 0, sizeof(g_sampler_waveform_ram));
    memset(&g_sampler_waveform_build, 0, sizeof(g_sampler_waveform_build));
    memset(&g_sampler_waveform_request, 0, sizeof(g_sampler_waveform_request));
    g_sampler_waveform_age = 0U;
}

static uint8_t sampler_waveform_request_matches(
    uint16_t sample_id, const sampler_waveform_identity_t *identity,
    uint32_t media_epoch)
{
    return (uint8_t)(g_sampler_waveform_request.active != 0U
        && g_sampler_waveform_request.sample_id == sample_id
        && g_sampler_waveform_request.media_epoch == media_epoch
        && sampler_waveform_identity_equal(
            &g_sampler_waveform_request.identity, identity) != 0U);
}

void sampler_waveform_store_release_request(void)
{
    memset(&g_sampler_waveform_request, 0, sizeof(g_sampler_waveform_request));
    if(g_sampler_waveform_build.active != 0U)
        g_sampler_waveform_build.cancel_requested = 1U;
}

void sampler_waveform_store_unregister_classic(uint16_t sample_id)
{
    if(sample_id >= SAMPLE_CLASSIC_CAPACITY) return;
    if(g_sampler_waveform_request.active != 0U
            && g_sampler_waveform_request.sample_id == sample_id)
        sampler_waveform_store_release_request();
    memset(&g_sampler_waveform_sources[sample_id], 0,
           sizeof(g_sampler_waveform_sources[sample_id]));
    if(g_sampler_waveform_build.active != 0U
            && g_sampler_waveform_build.sample_id == sample_id)
    {
        g_sampler_waveform_build.cancel_requested = 1U;
    }
}

uint8_t sampler_waveform_store_register_classic(uint16_t sample_id,
                                                 const char *path,
                                                 const wav_info_t *info,
                                                 FIL *open_file)
{
    if(sample_id >= SAMPLE_CLASSIC_CAPACITY) return 0U;
    sampler_waveform_identity_t identity;
    if(sampler_waveform_make_identity_open(path, info, open_file, &identity) == 0U)
        return 0U;
    if(g_sampler_waveform_request.active != 0U
            && g_sampler_waveform_request.sample_id == sample_id)
        sampler_waveform_store_release_request();
    if(g_sampler_waveform_build.active != 0U
            && g_sampler_waveform_build.sample_id == sample_id)
        g_sampler_waveform_build.cancel_requested = 1U;
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample_id];
    memset(source, 0, sizeof(*source));
    source->identity = identity;
    source->media_epoch = sd_access_media_epoch();
    source->state = SAMPLER_WAVEFORM_SOURCE_LOOKUP;
    source->registered = 1U;
    return 1U;
}

sampler_waveform_result_t sampler_waveform_store_request_classic(
    uint16_t sample_id,
    sampler_waveform_bin_t out_bins[SAMPLER_WAVEFORM_BIN_COUNT])
{
    if(sample_id >= SAMPLE_CLASSIC_CAPACITY || out_bins == 0)
        return SAMPLER_WAVEFORM_RESULT_INVALID;
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample_id];
    if(source->registered == 0U
            || source->media_epoch != sd_access_media_epoch())
        return SAMPLER_WAVEFORM_RESULT_INVALID;
    if(sampler_waveform_request_matches(sample_id, &source->identity,
            source->media_epoch) == 0U)
    {
        if(g_sampler_waveform_build.active != 0U)
            g_sampler_waveform_build.cancel_requested = 1U;
        g_sampler_waveform_request.identity = source->identity;
        g_sampler_waveform_request.media_epoch = source->media_epoch;
        g_sampler_waveform_request.sample_id = sample_id;
        g_sampler_waveform_request.active = 1U;
    }
    const int8_t slot = sampler_waveform_ram_find(&source->identity);
    if(slot < 0)
    {
        /* READY describes the resident LRU copy.  If it disappeared, keep the
         * indexed SAMPLE.WFM record authoritative and queue a cold reload. */
        if(source->state == SAMPLER_WAVEFORM_SOURCE_READY)
            source->state = SAMPLER_WAVEFORM_SOURCE_ON_DISK;
        if(source->state == SAMPLER_WAVEFORM_SOURCE_ERROR)
            return SAMPLER_WAVEFORM_RESULT_INVALID;
        return SAMPLER_WAVEFORM_RESULT_PENDING;
    }
    if(source->state == SAMPLER_WAVEFORM_SOURCE_ON_DISK
            && source->record_offset >= SAMPLER_WAVEFORM_RECORD_BASE)
        source->state = SAMPLER_WAVEFORM_SOURCE_READY;
    memcpy(out_bins, g_sampler_waveform_ram[(uint8_t)slot].bins,
           sizeof(g_sampler_waveform_ram[(uint8_t)slot].bins));
    if(source->fresh_persistent != 0U)
    {
        source->fresh_persistent = 0U;
        return SAMPLER_WAVEFORM_RESULT_READY_PERSISTENT;
    }
    return SAMPLER_WAVEFORM_RESULT_READY_RAM;
}

static void sampler_waveform_lookup_service(uint16_t sample_id)
{
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample_id];
    const sd_scheduler_background_request_t admission = {
        SAMPLER_WAVEFORM_RECORD_BYTES, source->media_epoch,
        SD_SCHEDULER_BACKGROUND_DATA };
    if(sd_scheduler_runtime_background_try_begin(&admission)
            != SD_SCHEDULER_BACKGROUND_GO) return;
    FIL file;
    memset(&file, 0, sizeof(file));
    sampler_waveform_file_header_t header;
    sampler_waveform_record_t record;
    uint32_t offset = 0U;
    const sampler_waveform_io_result_t opened =
        sampler_waveform_open_store_read(&file, &header);
    sampler_waveform_io_result_t lookup = opened;
    if(opened == SAMPLER_WAVEFORM_IO_OK)
    {
        lookup = sampler_waveform_lookup_open(&file, &source->identity,
                                               &offset, &record);
        (void)f_close(&file);
    }
    source->record_offset = offset;
    if(lookup == SAMPLER_WAVEFORM_IO_OK)
    {
        (void)sampler_waveform_ram_put(&source->identity, record.bins);
        source->state = SAMPLER_WAVEFORM_SOURCE_READY;
        source->fresh_persistent = 1U;
        g_waveform_diag.persistent_read_calls += 3U;
        g_waveform_diag.persistent_read_bytes +=
            sizeof(header) + sizeof(uint32_t) + sizeof(record);
    }
    else if(lookup == SAMPLER_WAVEFORM_IO_ERROR)
    {
        source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
    }
    else
    {
        /* A true miss or structurally invalid persisted data gets one
         * source rebuild. I/O failures above stay terminal until lifecycle
         * re-registration; they are never converted into retry demand. */
        source->state = SAMPLER_WAVEFORM_SOURCE_MISSING;
    }
    sd_scheduler_runtime_background_end();
}

static void sampler_waveform_load_service(uint16_t sample_id)
{
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample_id];
    const sd_scheduler_background_request_t admission = {
        SAMPLER_WAVEFORM_RECORD_BYTES, source->media_epoch,
        SD_SCHEDULER_BACKGROUND_DATA };
    if(sd_scheduler_runtime_background_try_begin(&admission)
            != SD_SCHEDULER_BACKGROUND_GO) return;
    FIL file;
    sampler_waveform_record_t record;
    const uint32_t io_start = DWT->CYCCNT;
    const FRESULT opened = f_open(&file, SAMPLER_WAVEFORM_PATH,
                                  FA_READ | FA_OPEN_EXISTING);
    uint8_t ok = (opened == FR_OK) ? 1U : 0U;
    if(opened != FR_OK && opened != FR_NO_FILE && opened != FR_NO_PATH)
        source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
    if(ok != 0U)
    {
        const sampler_waveform_io_result_t read_result =
            sampler_waveform_read_record(&file, source->record_offset, &record);
        ok = (read_result == SAMPLER_WAVEFORM_IO_OK) ? 1U : 0U;
        (void)f_close(&file);
        if(read_result == SAMPLER_WAVEFORM_IO_ERROR)
            source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
    }
    g_waveform_diag.io_cycles += (uint32_t)(DWT->CYCCNT - io_start);
    g_waveform_diag.persistent_read_calls++;
    if(ok != 0U) g_waveform_diag.persistent_read_bytes += sizeof(record);
    if(ok != 0U)
    {
        sampler_waveform_identity_t identity;
        sampler_waveform_record_identity(&record, &identity);
        if(sampler_waveform_identity_equal(&identity, &source->identity) != 0U)
        {
            (void)sampler_waveform_ram_put(&identity, record.bins);
            source->state = SAMPLER_WAVEFORM_SOURCE_READY;
            source->fresh_persistent = 1U;
        }
        else ok = 0U;
    }
    if(ok == 0U && source->state != SAMPLER_WAVEFORM_SOURCE_ERROR)
        source->state = SAMPLER_WAVEFORM_SOURCE_MISSING;
    sd_scheduler_runtime_background_end();
}

static uint8_t sampler_waveform_start_build(uint16_t sample_id)
{
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample_id];
    if(sampler_waveform_request_matches(sample_id, &source->identity,
            source->media_epoch) == 0U) return 0U;
    const sample_global_slot_t *const slot = sample_global_pool_get_slot(sample_id);
    if(slot == 0 || slot->kind != SAMPLE_GLOBAL_KIND_CLASSIC
            || slot->state != SAMPLE_GLOBAL_STATE_READY)
    {
        source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
        return 0U;
    }
    sampler_waveform_build_t *const build = &g_sampler_waveform_build;
    memset(build, 0, sizeof(*build));
    if(strlen(slot->path) >= sizeof(build->path))
    {
        source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
        return 0U;
    }
    strcpy(build->path, slot->path);
    build->identity = source->identity;
    build->sample_id = sample_id;
    build->media_epoch = source->media_epoch;
    sampler_waveform_accumulator_init(&build->accumulator,
                                      source->identity.frame_count);
    build->active = 1U;
    source->state = SAMPLER_WAVEFORM_SOURCE_BUILDING;
    g_waveform_diag.build_starts++;
    return 1U;
}

static void sampler_waveform_build_fail(void)
{
    sampler_waveform_build_t *const build = &g_sampler_waveform_build;
    if(build->file_open != 0U) (void)f_close(&build->file);
    if(build->sample_id < SAMPLE_CLASSIC_CAPACITY)
    {
        sampler_waveform_source_t *const source =
            &g_sampler_waveform_sources[build->sample_id];
        if(source->registered != 0U
                && source->media_epoch == build->media_epoch
                && sampler_waveform_identity_equal(
                    &source->identity, &build->identity) != 0U)
            source->state = SAMPLER_WAVEFORM_SOURCE_ERROR;
    }
    g_waveform_diag.build_aborts++;
    memset(build, 0, sizeof(*build));
}

static void sampler_waveform_build_service(void)
{
    sampler_waveform_build_t *const build = &g_sampler_waveform_build;
    if(build->active == 0U) return;
    if(build->media_epoch != sd_access_media_epoch())
    {
        memset(build, 0, sizeof(*build));
        return;
    }
    if(sampler_waveform_request_matches(build->sample_id,
                &build->identity, build->media_epoch) == 0U)
        build->cancel_requested = 1U;
    const sd_scheduler_background_request_t admission = {
        SAMPLER_WAVEFORM_READ_BYTES, build->media_epoch,
        SD_SCHEDULER_BACKGROUND_DATA };
    if(sd_scheduler_runtime_background_try_begin(&admission)
            != SD_SCHEDULER_BACKGROUND_GO) return;
    if(build->cancel_requested != 0U)
    {
        if(build->file_open != 0U) (void)f_close(&build->file);
        if(build->sample_id < SAMPLE_CLASSIC_CAPACITY)
        {
            sampler_waveform_source_t *const source =
                &g_sampler_waveform_sources[build->sample_id];
            if(source->registered != 0U
                    && source->state == SAMPLER_WAVEFORM_SOURCE_BUILDING
                    && source->media_epoch == build->media_epoch
                    && sampler_waveform_identity_equal(
                        &source->identity, &build->identity) != 0U)
                source->state = SAMPLER_WAVEFORM_SOURCE_MISSING;
        }
        memset(build, 0, sizeof(*build));
        sd_scheduler_runtime_background_end();
        return;
    }
    if(build->file_open == 0U)
    {
        const uint32_t io_start = DWT->CYCCNT;
        if(f_open(&build->file, build->path, FA_READ) != FR_OK)
        {
            g_waveform_diag.io_cycles += (uint32_t)(DWT->CYCCNT - io_start);
            sampler_waveform_build_fail();
            sd_scheduler_runtime_background_end();
            return;
        }
        build->file_open = 1U;
        wav_info_t info;
        if(wav_parser_parse_info(&build->file, &info) == 0U
                || wav_parser_is_canonical_brick_float(&info) == 0U
                || info.data_size / info.block_align != build->identity.frame_count
                || info.sample_rate != build->identity.sample_rate
                || info.data_offset != build->identity.data_offset
                || info.channels != build->identity.channels
                || info.bits_per_sample != build->identity.bits_per_sample
                || f_lseek(&build->file, info.data_offset) != FR_OK)
        {
            g_waveform_diag.io_cycles += (uint32_t)(DWT->CYCCNT - io_start);
            sampler_waveform_build_fail();
            sd_scheduler_runtime_background_end();
            return;
        }
        g_waveform_diag.file_opens++;
        g_waveform_diag.format_parses++;
        g_waveform_diag.io_cycles += (uint32_t)(DWT->CYCCNT - io_start);
    }
    uint32_t frames = build->identity.frame_count - build->next_frame;
    const uint32_t max_frames = sizeof(g_sampler_waveform_read) / 8U;
    if(frames > max_frames) frames = max_frames;
    const uint32_t bytes = frames * 8U;
    UINT read = 0U;
    uint32_t start = DWT->CYCCNT;
    const FRESULT result = f_read(&build->file, g_sampler_waveform_read,
                                  bytes, &read);
    g_waveform_diag.io_cycles += (uint32_t)(DWT->CYCCNT - start);
    g_waveform_diag.read_calls++;
    g_waveform_diag.read_bytes += read;
    if(read > g_waveform_diag.read_max_bytes) g_waveform_diag.read_max_bytes = read;
    if(result != FR_OK || read != bytes)
    {
        sampler_waveform_build_fail();
        sd_scheduler_runtime_background_end();
        return;
    }
    start = DWT->CYCCNT;
    sampler_waveform_accumulator_push_float(&build->accumulator,
        (const float *)(const void *)g_sampler_waveform_read, frames);
    g_waveform_diag.reduction_cycles += (uint32_t)(DWT->CYCCNT - start);
    build->next_frame += frames;
    if(build->next_frame < build->identity.frame_count)
    {
        sd_scheduler_runtime_background_end();
        return;
    }
    sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT];
    const uint8_t finished = sampler_waveform_accumulator_finish(
        &build->accumulator, bins);
    (void)f_close(&build->file);
    build->file_open = 0U;
    uint32_t record_offset = 0U;
    const uint8_t committed = (uint8_t)(finished != 0U
        && sampler_waveform_commit_locked(&build->identity, bins,
                                          &record_offset) != 0U);
    if(committed != 0U)
    {
        sampler_waveform_source_t *const source =
            &g_sampler_waveform_sources[build->sample_id];
        if(source->registered != 0U
                && source->media_epoch == build->media_epoch
                && sampler_waveform_identity_equal(
                    &source->identity, &build->identity) != 0U
                && sampler_waveform_request_matches(build->sample_id,
                    &build->identity, build->media_epoch) != 0U)
        {
            source->record_offset = record_offset;
            source->state = SAMPLER_WAVEFORM_SOURCE_READY;
            source->fresh_persistent = 1U;
            (void)sampler_waveform_ram_put(&build->identity, bins);
            g_waveform_diag.build_completions++;
        }
    }
    sd_scheduler_runtime_background_end();
    if(committed == 0U) sampler_waveform_build_fail();
    else memset(build, 0, sizeof(*build));
}

void sampler_waveform_store_service(void)
{
    if(g_sampler_waveform_build.active != 0U)
    {
        sampler_waveform_build_service();
        return;
    }
    if(g_sampler_waveform_request.active == 0U
            || g_sampler_waveform_request.sample_id >= SAMPLE_CLASSIC_CAPACITY)
        return;
    const uint16_t sample = g_sampler_waveform_request.sample_id;
    sampler_waveform_source_t *const source = &g_sampler_waveform_sources[sample];
    if(source->registered == 0U
            || source->media_epoch != sd_access_media_epoch()
            || sampler_waveform_request_matches(sample, &source->identity,
                source->media_epoch) == 0U)
    {
        sampler_waveform_store_release_request();
        return;
    }
    if(source->state == SAMPLER_WAVEFORM_SOURCE_ON_DISK)
        sampler_waveform_load_service(sample);
    else if(source->state == SAMPLER_WAVEFORM_SOURCE_LOOKUP)
        sampler_waveform_lookup_service(sample);
    else if(source->state == SAMPLER_WAVEFORM_SOURCE_MISSING)
        (void)sampler_waveform_start_build(sample);
}

uint8_t sampler_waveform_store_commit_converted(
    const char *path, const wav_info_t *info,
    const sampler_waveform_bin_t bins[SAMPLER_WAVEFORM_BIN_COUNT])
{
    if(path == 0 || info == 0 || bins == 0) return 0U;
    const uint8_t convert_owned = (uint8_t)(sd_access_gate_current_owner()
        == SD_ACCESS_CLIENT_WAV_CONVERT);
    if(convert_owned == 0U
            && sd_access_gate_try_acquire(SD_ACCESS_CLIENT_WAVEFORM_CACHE) == 0U)
        return 0U;
    FIL file;
    uint8_t ok = 0U;
    if(f_open(&file, path, FA_READ) == FR_OK)
    {
        sampler_waveform_identity_t identity;
        if(sampler_waveform_make_identity_open(path, info, &file, &identity) != 0U)
            ok = sampler_waveform_commit_locked(&identity, bins, 0);
        (void)f_close(&file);
    }
    if(convert_owned == 0U)
        sd_access_gate_release(SD_ACCESS_CLIENT_WAVEFORM_CACHE);
    return ok;
}
