#include "Sampler/multi_sample_index.h"

#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"

#include "ff.h"

#define MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V1 (40U)
#define MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V2 (52U)
#define MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE    MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V2
#define MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE   (8U)

typedef struct
{
    char magic[MULTI_SAMPLE_INDEX_MAGIC_SIZE];
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint16_t sample_count;
    uint16_t zone_count;
    uint32_t string_bytes;
    uint32_t samples_offset;
    uint32_t zones_offset;
    uint32_t strings_offset;
    uint32_t file_size;
    uint32_t crc32;
    char instrument_name[MULTI_SAMPLE_POOL_NAME_MAX];
    uint8_t reserved[20];
} multi_sample_index_header_t;

SDRAM_MULTI_LOAD static multi_sample_index_sample_t
    g_index_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];
SDRAM_MULTI_LOAD static multi_sample_index_zone_t
    g_index_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
SDRAM_MULTI_LOAD static char g_index_strings[MULTI_SAMPLE_INDEX_STRING_MAX_BYTES];
SDRAM_MULTI_LOAD static uint8_t g_index_io[MULTI_SAMPLE_INDEX_HEADER_SIZE];
static CTRL_STATE uint16_t g_apply_sample_map[MULTI_SAMPLE_POOL_MAX_SAMPLES];

static uint16_t multi_index_get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t multi_index_get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
           | ((uint32_t)p[3] << 24);
}

static void multi_index_put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void multi_index_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static uint32_t multi_index_crc32_update(uint32_t crc, const uint8_t *data, uint32_t size)
{
    crc = ~crc;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static uint8_t multi_index_copy_text(char *dst, uint32_t dst_size, const char *src)
{
    if ((dst == 0) || (dst_size == 0U))
    {
        return 0U;
    }

    dst[0] = '\0';
    if (src == 0)
    {
        return 1U;
    }

    uint32_t i = 0U;
    while ((i + 1U) < dst_size)
    {
        dst[i] = src[i];
        if (src[i] == '\0')
        {
            return 1U;
        }
        i++;
    }

    dst[i] = '\0';
    return (src[i] == '\0') ? 1U : 0U;
}

static uint8_t multi_index_path_len(const char *path, uint16_t *out_len)
{
    if ((path == 0) || (out_len == 0))
    {
        return 0U;
    }

    if ((path[0] == '/') || (path[0] == '\\'))
    {
        return 0U;
    }

    uint32_t len = 0U;
    while ((len < MULTI_SAMPLE_POOL_PATH_MAX) && (path[len] != '\0'))
    {
        if (path[len] == ':')
        {
            return 0U;
        }
        len++;
    }

    if ((len == 0U) || (len >= MULTI_SAMPLE_POOL_PATH_MAX) || (path[len] != '\0'))
    {
        return 0U;
    }

    *out_len = (uint16_t)len;
    return 1U;
}

static uint8_t multi_index_path_bytes_valid(const char *path, uint16_t len)
{
    if ((path == 0) || (len == 0U) || (len >= MULTI_SAMPLE_POOL_PATH_MAX)
        || (path[0] == '/') || (path[0] == '\\'))
    {
        return 0U;
    }

    for (uint16_t i = 0U; i < len; ++i)
    {
        if ((path[i] == '\0') || (path[i] == ':'))
        {
            return 0U;
        }
    }

    return 1U;
}

static uint8_t multi_index_source_to_static(const multi_sample_index_source_t *src,
                                            multi_sample_index_t *out)
{
    if ((src == 0) || (out == 0)
        || (src->sample_count > MULTI_SAMPLE_POOL_MAX_SAMPLES)
        || (src->zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
        || ((src->sample_count != 0U) && (src->samples == 0))
        || ((src->zone_count != 0U) && (src->zones == 0)))
    {
        return 0U;
    }

    memset(g_index_samples, 0, sizeof(g_index_samples));
    memset(g_index_zones, 0, sizeof(g_index_zones));
    memset(g_index_strings, 0, sizeof(g_index_strings));
    multi_sample_index_reset(out);

    if (multi_index_copy_text(out->instrument_name,
                              sizeof(out->instrument_name),
                              src->instrument_name) == 0U)
    {
        return 0U;
    }

    out->flags = src->flags;
    out->sample_count = src->sample_count;
    out->zone_count = src->zone_count;

    uint32_t string_bytes = 0U;
    for (uint16_t i = 0U; i < src->sample_count; ++i)
    {
        uint16_t path_len = 0U;
        if (multi_index_path_len(src->samples[i].relative_path, &path_len) == 0U)
        {
            return 0U;
        }

        if ((string_bytes + path_len) > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES)
        {
            return 0U;
        }

        g_index_samples[i].path_offset = string_bytes;
        g_index_samples[i].path_len = path_len;
        g_index_samples[i].total_frames = src->samples[i].total_frames;
        g_index_samples[i].sample_rate = src->samples[i].sample_rate;
        g_index_samples[i].channels = src->samples[i].channels;
        g_index_samples[i].bits_per_sample = src->samples[i].bits_per_sample;
        g_index_samples[i].data_offset = src->samples[i].data_offset;
        g_index_samples[i].data_size = src->samples[i].data_size;
        g_index_samples[i].loop_begin = src->samples[i].loop_begin;
        g_index_samples[i].loop_end = src->samples[i].loop_end;
        g_index_samples[i].root_note = src->samples[i].root_note;
        g_index_samples[i].vel_low = src->samples[i].vel_low;
        g_index_samples[i].vel_high = src->samples[i].vel_high;
        g_index_samples[i].has_loop = src->samples[i].has_loop;
        g_index_samples[i].metadata_flags = src->samples[i].metadata_flags;
        g_index_samples[i].wav_size = src->samples[i].wav_size;
        g_index_samples[i].wav_mtime = src->samples[i].wav_mtime;
        memcpy(&g_index_strings[string_bytes], src->samples[i].relative_path, path_len);
        string_bytes += path_len;
    }

    for (uint16_t i = 0U; i < src->zone_count; ++i)
    {
        g_index_zones[i] = src->zones[i];
    }

    out->string_bytes = string_bytes;
    out->samples = g_index_samples;
    out->zones = g_index_zones;
    out->strings = g_index_strings;
    return multi_sample_index_validate(out);
}

static void multi_index_encode_header(const multi_sample_index_header_t *header, uint8_t *out)
{
    memset(out, 0, MULTI_SAMPLE_INDEX_HEADER_SIZE);
    memcpy(&out[0], header->magic, MULTI_SAMPLE_INDEX_MAGIC_SIZE);
    multi_index_put_le16(&out[8], header->version);
    multi_index_put_le16(&out[10], header->header_size);
    multi_index_put_le32(&out[12], header->flags);
    multi_index_put_le16(&out[16], header->sample_count);
    multi_index_put_le16(&out[18], header->zone_count);
    multi_index_put_le32(&out[20], header->string_bytes);
    multi_index_put_le32(&out[24], header->samples_offset);
    multi_index_put_le32(&out[28], header->zones_offset);
    multi_index_put_le32(&out[32], header->strings_offset);
    multi_index_put_le32(&out[36], header->file_size);
    multi_index_put_le32(&out[40], header->crc32);
    memcpy(&out[44], header->instrument_name, MULTI_SAMPLE_POOL_NAME_MAX);
}

static void multi_index_decode_header(const uint8_t *in, multi_sample_index_header_t *header)
{
    memset(header, 0, sizeof(*header));
    memcpy(header->magic, &in[0], MULTI_SAMPLE_INDEX_MAGIC_SIZE);
    header->version = multi_index_get_le16(&in[8]);
    header->header_size = multi_index_get_le16(&in[10]);
    header->flags = multi_index_get_le32(&in[12]);
    header->sample_count = multi_index_get_le16(&in[16]);
    header->zone_count = multi_index_get_le16(&in[18]);
    header->string_bytes = multi_index_get_le32(&in[20]);
    header->samples_offset = multi_index_get_le32(&in[24]);
    header->zones_offset = multi_index_get_le32(&in[28]);
    header->strings_offset = multi_index_get_le32(&in[32]);
    header->file_size = multi_index_get_le32(&in[36]);
    header->crc32 = multi_index_get_le32(&in[40]);
    memcpy(header->instrument_name, &in[44], MULTI_SAMPLE_POOL_NAME_MAX);
}

static void multi_index_encode_sample(const multi_sample_index_sample_t *sample, uint8_t *out)
{
    memset(out, 0, MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE);
    multi_index_put_le32(&out[0], sample->path_offset);
    multi_index_put_le16(&out[4], sample->path_len);
    multi_index_put_le16(&out[6], 0U);
    multi_index_put_le32(&out[8], sample->total_frames);
    multi_index_put_le32(&out[12], sample->sample_rate);
    multi_index_put_le16(&out[16], sample->channels);
    multi_index_put_le16(&out[18], sample->bits_per_sample);
    multi_index_put_le32(&out[20], sample->data_offset);
    multi_index_put_le32(&out[24], sample->data_size);
    multi_index_put_le32(&out[28], sample->loop_begin);
    multi_index_put_le32(&out[32], sample->loop_end);
    out[36] = sample->root_note;
    out[37] = sample->vel_low;
    out[38] = sample->vel_high;
    out[39] = sample->has_loop;
    out[40] = sample->metadata_flags;
    multi_index_put_le32(&out[44], sample->wav_size);
    multi_index_put_le32(&out[48], sample->wav_mtime);
}

static void multi_index_encode_sample_v1(const multi_sample_index_sample_t *sample, uint8_t *out)
{
    memset(out, 0, MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V1);
    multi_index_put_le32(&out[0], sample->path_offset);
    multi_index_put_le16(&out[4], sample->path_len);
    multi_index_put_le16(&out[6], 0U);
    multi_index_put_le32(&out[8], sample->total_frames);
    multi_index_put_le32(&out[12], sample->sample_rate);
    multi_index_put_le16(&out[16], sample->channels);
    multi_index_put_le16(&out[18], sample->bits_per_sample);
    multi_index_put_le32(&out[20], sample->data_offset);
    multi_index_put_le32(&out[24], sample->data_size);
    out[28] = sample->root_note;
    out[29] = sample->vel_low;
    out[30] = sample->vel_high;
    out[31] = sample->metadata_flags;
    multi_index_put_le32(&out[32], sample->wav_size);
    multi_index_put_le32(&out[36], sample->wav_mtime);
}

static void multi_index_decode_sample(const uint8_t *in,
                                      uint16_t version,
                                      multi_sample_index_sample_t *sample)
{
    memset(sample, 0, sizeof(*sample));
    sample->path_offset = multi_index_get_le32(&in[0]);
    sample->path_len = multi_index_get_le16(&in[4]);
    sample->total_frames = multi_index_get_le32(&in[8]);
    sample->sample_rate = multi_index_get_le32(&in[12]);
    sample->channels = multi_index_get_le16(&in[16]);
    sample->bits_per_sample = multi_index_get_le16(&in[18]);
    sample->data_offset = multi_index_get_le32(&in[20]);
    sample->data_size = multi_index_get_le32(&in[24]);
    if (version >= 2U)
    {
        sample->loop_begin = multi_index_get_le32(&in[28]);
        sample->loop_end = multi_index_get_le32(&in[32]);
        sample->root_note = in[36];
        sample->vel_low = in[37];
        sample->vel_high = in[38];
        sample->has_loop = in[39];
        sample->metadata_flags = in[40];
        sample->wav_size = multi_index_get_le32(&in[44]);
        sample->wav_mtime = multi_index_get_le32(&in[48]);
    }
    else
    {
        sample->root_note = in[28];
        sample->vel_low = in[29];
        sample->vel_high = in[30];
        sample->metadata_flags = in[31];
        sample->wav_size = multi_index_get_le32(&in[32]);
        sample->wav_mtime = multi_index_get_le32(&in[36]);
    }
}

static void multi_index_encode_zone(const multi_sample_index_zone_t *zone, uint8_t *out)
{
    memset(out, 0, MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE);
    out[0] = zone->note_low;
    out[1] = zone->note_high;
    out[2] = zone->vel_low;
    out[3] = zone->vel_high;
    out[4] = zone->root_note;
    out[5] = 0U;
    multi_index_put_le16(&out[6], zone->multi_sample_id);
}

static void multi_index_decode_zone(const uint8_t *in, multi_sample_index_zone_t *zone)
{
    memset(zone, 0, sizeof(*zone));
    zone->note_low = in[0];
    zone->note_high = in[1];
    zone->vel_low = in[2];
    zone->vel_high = in[3];
    zone->root_note = in[4];
    zone->multi_sample_id = multi_index_get_le16(&in[6]);
}

static uint32_t multi_index_crc_header_tables(const multi_sample_index_t *idx,
                                              const multi_sample_index_header_t *header)
{
    multi_sample_index_header_t crc_header = *header;
    crc_header.crc32 = 0U;
    multi_index_encode_header(&crc_header, g_index_io);

    uint32_t crc = 0U;
    crc = multi_index_crc32_update(crc, g_index_io, MULTI_SAMPLE_INDEX_HEADER_SIZE);

    for (uint16_t i = 0U; i < idx->sample_count; ++i)
    {
        if (header->version >= 2U)
        {
            multi_index_encode_sample(&idx->samples[i], g_index_io);
            crc = multi_index_crc32_update(crc, g_index_io, MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V2);
        }
        else
        {
            multi_index_encode_sample_v1(&idx->samples[i], g_index_io);
            crc = multi_index_crc32_update(crc, g_index_io, MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V1);
        }
    }

    for (uint16_t i = 0U; i < idx->zone_count; ++i)
    {
        multi_index_encode_zone(&idx->zones[i], g_index_io);
        crc = multi_index_crc32_update(crc, g_index_io, MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE);
    }

    crc = multi_index_crc32_update(crc, (const uint8_t *)idx->strings, idx->string_bytes);
    return crc;
}

static uint8_t multi_index_make_header(const multi_sample_index_t *idx,
                                       multi_sample_index_header_t *header)
{
    if ((idx == 0) || (header == 0) || (multi_sample_index_validate(idx) == 0U))
    {
        return 0U;
    }

    memset(header, 0, sizeof(*header));
    memcpy(header->magic, "BRKMULTI", MULTI_SAMPLE_INDEX_MAGIC_SIZE);
    header->version = MULTI_SAMPLE_INDEX_VERSION;
    header->header_size = MULTI_SAMPLE_INDEX_HEADER_SIZE;
    header->flags = idx->flags;
    header->sample_count = idx->sample_count;
    header->zone_count = idx->zone_count;
    header->string_bytes = idx->string_bytes;
    header->samples_offset = MULTI_SAMPLE_INDEX_HEADER_SIZE;
    header->zones_offset = header->samples_offset
                           + ((uint32_t)idx->sample_count
                              * MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE);
    header->strings_offset = header->zones_offset
                             + ((uint32_t)idx->zone_count
                                * MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE);
    header->file_size = header->strings_offset + idx->string_bytes;
    memcpy(header->instrument_name, idx->instrument_name, MULTI_SAMPLE_POOL_NAME_MAX);
    header->crc32 = multi_index_crc_header_tables(idx, header);
    return 1U;
}

static uint8_t multi_index_write_exact(FIL *fp, const void *data, uint32_t size)
{
    UINT bw = 0U;
    return ((f_write(fp, data, size, &bw) == FR_OK) && (bw == size)) ? 1U : 0U;
}

static uint8_t multi_index_read_exact(FIL *fp, void *data, uint32_t size)
{
    UINT br = 0U;
    return ((f_read(fp, data, size, &br) == FR_OK) && (br == size)) ? 1U : 0U;
}

static uint8_t multi_index_header_basic_valid(const multi_sample_index_header_t *header)
{
    if ((header == 0)
        || (memcmp(header->magic, "BRKMULTI", MULTI_SAMPLE_INDEX_MAGIC_SIZE) != 0)
        || (header->version < MULTI_SAMPLE_INDEX_MIN_VERSION)
        || (header->version > MULTI_SAMPLE_INDEX_VERSION)
        || (header->header_size != MULTI_SAMPLE_INDEX_HEADER_SIZE)
        || (header->sample_count > MULTI_SAMPLE_POOL_MAX_SAMPLES)
        || (header->zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
        || (header->string_bytes > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES))
    {
        return 0U;
    }

    const uint32_t sample_record_size = (header->version >= 2U)
        ? MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V2
        : MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V1;
    const uint32_t samples_size =
        (uint32_t)header->sample_count * sample_record_size;
    const uint32_t zones_size =
        (uint32_t)header->zone_count * MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE;
    return ((header->samples_offset == MULTI_SAMPLE_INDEX_HEADER_SIZE)
            && (header->zones_offset == (header->samples_offset + samples_size))
            && (header->strings_offset == (header->zones_offset + zones_size))
            && (header->file_size == (header->strings_offset + header->string_bytes)))
        ? 1U
        : 0U;
}

multi_sample_index_result_t multi_sample_index_write(
    const char *path,
    const multi_sample_index_source_t *src)
{
    if ((path == 0) || (path[0] == '\0') || (src == 0))
    {
        return MULTI_SAMPLE_INDEX_INVALID_ARG;
    }

    multi_sample_index_t idx;
    if (multi_index_source_to_static(src, &idx) == 0U)
    {
        return MULTI_SAMPLE_INDEX_BAD_FORMAT;
    }

    multi_sample_index_header_t header;
    if (multi_index_make_header(&idx, &header) == 0U)
    {
        return MULTI_SAMPLE_INDEX_BAD_FORMAT;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return MULTI_SAMPLE_INDEX_SD_BUSY;
    }

    multi_sample_index_result_t result = MULTI_SAMPLE_INDEX_OK;
    FIL fp;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_SD_MOUNT_FAIL;
    }

    FRESULT fr = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_OPEN_FAIL;
    }

    multi_index_encode_header(&header, g_index_io);
    if (multi_index_write_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_HEADER_SIZE) == 0U)
    {
        result = MULTI_SAMPLE_INDEX_WRITE_FAIL;
    }

    for (uint16_t i = 0U; (result == MULTI_SAMPLE_INDEX_OK) && (i < idx.sample_count); ++i)
    {
        multi_index_encode_sample(&idx.samples[i], g_index_io);
        if (multi_index_write_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE)
            == 0U)
        {
            result = MULTI_SAMPLE_INDEX_WRITE_FAIL;
        }
    }

    for (uint16_t i = 0U; (result == MULTI_SAMPLE_INDEX_OK) && (i < idx.zone_count); ++i)
    {
        multi_index_encode_zone(&idx.zones[i], g_index_io);
        if (multi_index_write_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE)
            == 0U)
        {
            result = MULTI_SAMPLE_INDEX_WRITE_FAIL;
        }
    }

    if ((result == MULTI_SAMPLE_INDEX_OK)
        && (multi_index_write_exact(&fp, idx.strings, idx.string_bytes) == 0U))
    {
        result = MULTI_SAMPLE_INDEX_WRITE_FAIL;
    }

    if ((f_close(&fp) != FR_OK) && (result == MULTI_SAMPLE_INDEX_OK))
    {
        result = MULTI_SAMPLE_INDEX_WRITE_FAIL;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return result;
}

multi_sample_index_result_t multi_sample_index_load(const char *path,
                                                    multi_sample_index_t *out)
{
    if ((path == 0) || (path[0] == '\0') || (out == 0))
    {
        return MULTI_SAMPLE_INDEX_INVALID_ARG;
    }

    multi_sample_index_reset(out);
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return MULTI_SAMPLE_INDEX_SD_BUSY;
    }

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_SD_MOUNT_FAIL;
    }

    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_OPEN_FAIL;
    }

    multi_sample_index_result_t result = MULTI_SAMPLE_INDEX_OK;
    multi_sample_index_header_t header;
    if (multi_index_read_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_HEADER_SIZE) == 0U)
    {
        result = MULTI_SAMPLE_INDEX_READ_FAIL;
    }
    else
    {
        multi_index_decode_header(g_index_io, &header);
        if ((header.version < MULTI_SAMPLE_INDEX_MIN_VERSION)
            || (header.version > MULTI_SAMPLE_INDEX_VERSION))
        {
            result = MULTI_SAMPLE_INDEX_UNSUPPORTED_VERSION;
        }
        else if ((header.sample_count > MULTI_SAMPLE_MAX_SAMPLES)
                 || (header.zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
                 || (header.string_bytes > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES))
        {
            result = MULTI_SAMPLE_INDEX_LIMIT;
        }
        else if ((multi_index_header_basic_valid(&header) == 0U)
                 || (f_size(&fp) != header.file_size))
        {
            result = MULTI_SAMPLE_INDEX_BAD_FORMAT;
        }
    }

    if (result == MULTI_SAMPLE_INDEX_OK)
    {
        memcpy(out->instrument_name, header.instrument_name, sizeof(out->instrument_name));
        out->flags = header.flags;
        out->sample_count = header.sample_count;
        out->zone_count = header.zone_count;
        out->string_bytes = header.string_bytes;
        out->samples = g_index_samples;
        out->zones = g_index_zones;
        out->strings = g_index_strings;

        for (uint16_t i = 0U; (result == MULTI_SAMPLE_INDEX_OK) && (i < out->sample_count);
             ++i)
        {
            const uint32_t sample_record_size = (header.version >= 2U)
                ? MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V2
                : MULTI_SAMPLE_INDEX_SAMPLE_RECORD_SIZE_V1;
            if (multi_index_read_exact(&fp, g_index_io, sample_record_size)
                == 0U)
            {
                result = MULTI_SAMPLE_INDEX_READ_FAIL;
            }
            else
            {
                multi_index_decode_sample(g_index_io, header.version, &g_index_samples[i]);
            }
        }

        for (uint16_t i = 0U; (result == MULTI_SAMPLE_INDEX_OK) && (i < out->zone_count);
             ++i)
        {
            if (multi_index_read_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_ZONE_RECORD_SIZE)
                == 0U)
            {
                result = MULTI_SAMPLE_INDEX_READ_FAIL;
            }
            else
            {
                multi_index_decode_zone(g_index_io, &g_index_zones[i]);
            }
        }

        if ((result == MULTI_SAMPLE_INDEX_OK)
            && (multi_index_read_exact(&fp, g_index_strings, out->string_bytes) == 0U))
        {
            result = MULTI_SAMPLE_INDEX_READ_FAIL;
        }

        if ((result == MULTI_SAMPLE_INDEX_OK) && (multi_sample_index_validate(out) == 0U))
        {
            result = MULTI_SAMPLE_INDEX_BAD_FORMAT;
        }
        else if ((result == MULTI_SAMPLE_INDEX_OK)
                 && (multi_index_crc_header_tables(out, &header) != header.crc32))
        {
            result = MULTI_SAMPLE_INDEX_CRC_FAIL;
        }
    }

    (void)f_close(&fp);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    if (result != MULTI_SAMPLE_INDEX_OK)
    {
        multi_sample_index_reset(out);
    }
    return result;
}

multi_sample_index_result_t multi_sample_index_peek_counts(const char *path,
                                                           uint16_t *out_sample_count,
                                                           uint16_t *out_zone_count)
{
    if ((path == 0) || (path[0] == '\0') || (out_sample_count == 0)
        || (out_zone_count == 0))
    {
        return MULTI_SAMPLE_INDEX_INVALID_ARG;
    }

    *out_sample_count = 0U;
    *out_zone_count = 0U;

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
    {
        return MULTI_SAMPLE_INDEX_SD_BUSY;
    }

    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_SD_MOUNT_FAIL;
    }

    FIL fp;
    FRESULT fr = f_open(&fp, path, FA_READ);
    if (fr != FR_OK)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return MULTI_SAMPLE_INDEX_OPEN_FAIL;
    }

    multi_sample_index_result_t result = MULTI_SAMPLE_INDEX_OK;
    multi_sample_index_header_t header;
    if (multi_index_read_exact(&fp, g_index_io, MULTI_SAMPLE_INDEX_HEADER_SIZE) == 0U)
    {
        result = MULTI_SAMPLE_INDEX_READ_FAIL;
    }
    else
    {
        multi_index_decode_header(g_index_io, &header);
        if ((header.version < MULTI_SAMPLE_INDEX_MIN_VERSION)
            || (header.version > MULTI_SAMPLE_INDEX_VERSION))
        {
            result = MULTI_SAMPLE_INDEX_UNSUPPORTED_VERSION;
        }
        else if ((header.sample_count > MULTI_SAMPLE_MAX_SAMPLES)
                 || (header.zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
                 || (header.string_bytes > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES))
        {
            result = MULTI_SAMPLE_INDEX_LIMIT;
        }
        else if ((multi_index_header_basic_valid(&header) == 0U)
                 || (f_size(&fp) != header.file_size))
        {
            result = MULTI_SAMPLE_INDEX_BAD_FORMAT;
        }
        else
        {
            *out_sample_count = header.sample_count;
            *out_zone_count = header.zone_count;
        }
    }

    (void)f_close(&fp);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    return result;
}

uint8_t multi_sample_index_validate(const multi_sample_index_t *idx)
{
    if ((idx == 0)
        || (idx->sample_count > MULTI_SAMPLE_POOL_MAX_SAMPLES)
        || (idx->zone_count > MULTI_SAMPLE_POOL_MAX_ZONES)
        || (idx->string_bytes > MULTI_SAMPLE_INDEX_STRING_MAX_BYTES)
        || ((idx->sample_count != 0U) && (idx->samples == 0))
        || ((idx->zone_count != 0U) && (idx->zones == 0))
        || ((idx->string_bytes != 0U) && (idx->strings == 0)))
    {
        return 0U;
    }

    for (uint16_t i = 0U; i < idx->sample_count; ++i)
    {
        const multi_sample_index_sample_t *const sample = &idx->samples[i];
        if ((sample->path_len == 0U)
            || (sample->path_len >= MULTI_SAMPLE_POOL_PATH_MAX)
            || ((sample->path_offset + sample->path_len) > idx->string_bytes)
            || (multi_index_path_bytes_valid(&idx->strings[sample->path_offset],
                                             sample->path_len) == 0U)
            || (sample->total_frames == 0U)
            || (sample->sample_rate != 48000U)
            || !((sample->channels == 1U) || (sample->channels == 2U))
            || !((sample->bits_per_sample == 16U) || (sample->bits_per_sample == 24U)
                 || (sample->bits_per_sample == 32U))
            || ((sample->has_loop != 0U)
                && ((sample->loop_end <= sample->loop_begin)
                    || (sample->loop_end > sample->total_frames)))
            || (sample->root_note > 127U)
            || (sample->vel_low > sample->vel_high)
            || (sample->vel_high > 127U))
        {
            return 0U;
        }
    }

    for (uint16_t i = 0U; i < idx->zone_count; ++i)
    {
        const multi_sample_index_zone_t *const zone = &idx->zones[i];
        if ((zone->note_low > zone->note_high)
            || (zone->note_high > 127U)
            || (zone->vel_low > zone->vel_high)
            || (zone->vel_high > 127U)
            || (zone->root_note > 127U)
            || (zone->multi_sample_id >= idx->sample_count))
        {
            return 0U;
        }

        const multi_sample_index_sample_t *const sample = &idx->samples[zone->multi_sample_id];
        if ((sample->vel_low > zone->vel_low) || (sample->vel_high < zone->vel_high))
        {
            return 0U;
        }
    }

    return 1U;
}

multi_sample_index_result_t multi_sample_index_apply_to_pool(
    const multi_sample_index_t *idx,
    uint16_t instrument_id)
{
    if ((idx == 0) || (multi_sample_index_validate(idx) == 0U)
        || (instrument_id >= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))
    {
        return MULTI_SAMPLE_INDEX_INVALID_ARG;
    }

    const char *const name =
        (idx->instrument_name[0] != '\0') ? idx->instrument_name : "MULTI";
    if (multi_sample_pool_debug_define_instrument(instrument_id,
                                                  name,
                                                  MULTI_SAMPLE_INSTRUMENT_INDEXED)
        == 0U)
    {
        return MULTI_SAMPLE_INDEX_POOL_FAIL;
    }

    for (uint16_t i = 0U; i < idx->sample_count; ++i)
    {
        const multi_sample_index_sample_t *const sample = &idx->samples[i];
        char path[MULTI_SAMPLE_POOL_PATH_MAX];
        memcpy(path, &idx->strings[sample->path_offset], sample->path_len);
        path[sample->path_len] = '\0';

        uint16_t pool_sample_id = MULTI_SAMPLE_POOL_INVALID_ID;
        if (multi_sample_pool_debug_add_sample(instrument_id,
                                               path,
                                               sample->total_frames,
                                               sample->root_note,
                                               sample->vel_low,
                                               sample->vel_high,
                                               (uint16_t)sample->metadata_flags,
                                               &pool_sample_id)
            == 0U)
        {
            return MULTI_SAMPLE_INDEX_POOL_FAIL;
        }
        if (multi_sample_pool_set_sample_format(pool_sample_id,
                                                sample->data_offset,
                                                sample->data_size,
                                                sample->sample_rate,
                                                sample->channels,
                                                sample->bits_per_sample)
            == 0U)
        {
            return MULTI_SAMPLE_INDEX_POOL_FAIL;
        }
        if (multi_sample_pool_set_sample_loop(pool_sample_id,
                                              sample->has_loop,
                                              sample->loop_begin,
                                              sample->loop_end)
            == 0U)
        {
            return MULTI_SAMPLE_INDEX_POOL_FAIL;
        }
        g_apply_sample_map[i] = pool_sample_id;
    }

    for (uint16_t i = 0U; i < idx->zone_count; ++i)
    {
        multi_sample_zone_t zone;
        zone.note_low = idx->zones[i].note_low;
        zone.note_high = idx->zones[i].note_high;
        zone.vel_low = idx->zones[i].vel_low;
        zone.vel_high = idx->zones[i].vel_high;
        zone.root_note = idx->zones[i].root_note;
        zone.multi_sample_id = g_apply_sample_map[idx->zones[i].multi_sample_id];
        if (multi_sample_pool_debug_add_zone(instrument_id, &zone) == 0U)
        {
            return MULTI_SAMPLE_INDEX_POOL_FAIL;
        }
    }

    return MULTI_SAMPLE_INDEX_OK;
}

void multi_sample_index_reset(multi_sample_index_t *idx)
{
    if (idx != 0)
    {
        memset(idx, 0, sizeof(*idx));
    }
}

multi_sample_index_result_t multi_sample_index_debug_make(
    const multi_sample_index_source_t *src,
    multi_sample_index_t *out)
{
    return (multi_index_source_to_static(src, out) != 0U)
        ? MULTI_SAMPLE_INDEX_OK
        : MULTI_SAMPLE_INDEX_BAD_FORMAT;
}
