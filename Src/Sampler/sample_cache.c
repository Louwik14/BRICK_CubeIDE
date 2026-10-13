#include "Sampler/sample_cache.h"

#include <ctype.h>
#include <string.h>

#include "Platform/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sample_stream_manager.h"
#include "Sampler/sample_classic_audio_projection_control.h"
#include "Storage/sampler_waveform_store.h"
#include "ff.h"

#define SAMPLE_CACHE_MAX_VOICES (16U)
SDRAM_CLASSIC_POOL static sample_cache_desc_t g_sample_cache[SAMPLE_CLASSIC_CAPACITY];
static CTRL_STATE FRESULT g_sample_cache_last_fresult[SAMPLE_CLASSIC_CAPACITY];
static uint8_t g_sample_cache_stream_gate_held;
static uint8_t g_sample_cache_projection_published[SAMPLE_CLASSIC_CAPACITY];

typedef enum
{
    SAMPLE_CACHE_COOP_RIFF = 0,
    SAMPLE_CACHE_COOP_CHUNK,
    SAMPLE_CACHE_COOP_FMT,
    SAMPLE_CACHE_COOP_FACT_FIRST,
    SAMPLE_CACHE_COOP_FACT_LAST,
    SAMPLE_CACHE_COOP_INSTALL
} sample_cache_coop_phase_t;

typedef struct
{
    char path[SAMPLE_CLASSIC_PATH_MAX];
    wav_info_t info;
    uint8_t io[40U];
    uint32_t parse_limit;
    uint32_t cursor;
    uint32_t chunk_size;
    uint32_t chunk_payload;
    uint32_t source_signature;
    uint16_t sample_id;
    uint8_t active;
    uint8_t waiting;
    uint8_t terminal;
    uint8_t success;
    uint8_t have_fmt;
    uint8_t have_fact;
    uint8_t have_data;
    uint8_t edge_ready;
    sample_cache_coop_phase_t phase;
} sample_cache_coop_load_t;

SDRAM_CLASSIC_POOL ALIGN32 static sample_stream_coop_file_t g_sample_cache_coop_file;
SDRAM_CLASSIC_POOL static sample_cache_coop_load_t g_sample_cache_coop;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(SAMPLE_CLASSIC_CAPACITY <= SAMPLE_PAGE_CACHE_ID_CAPACITY,
               "hot sample cache ids must fit in the page-cache id space");
#endif

static uint8_t sample_cache_prepare_via_page_cache(uint16_t sample_id,
                                                   sample_cache_desc_t *desc,
                                                   FIL *map_file,
                                                   const char *path);
static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc);

static void sample_cache_sync_projections(void)
{
    for (uint16_t sample_id = 0U; sample_id < SAMPLE_CLASSIC_CAPACITY; ++sample_id)
    {
        const uint8_t ready = sample_cache_is_ready(sample_id);
        if ((ready != 0U) && (g_sample_cache_projection_published[sample_id] == 0U))
        {
            if (sample_classic_audio_projection_publish(sample_id) != 0U)
                g_sample_cache_projection_published[sample_id] = 1U;
        }
        else if ((ready == 0U) && (g_sample_cache_projection_published[sample_id] != 0U))
        {
            sample_classic_audio_projection_withdraw(sample_id);
            g_sample_cache_projection_published[sample_id] = 0U;
        }
    }
}

uint32_t sample_cache_product_cost_bytes(uint32_t frames,
                                          sample_audio_format_t format)
{
    const uint32_t prep_frames = (frames < SAMPLE_PREP_MIN_READY_FRAMES)
                                     ? frames
                                     : SAMPLE_PREP_MIN_READY_FRAMES;
    return sample_audio_format_required_page_count(
               sample_audio_format_or_stereo(format), prep_frames)
           * SAMPLE_PAGE_BYTES;
}

uint32_t sample_cache_get_product_cost_bytes(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY) return 0U;
    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if (desc->total_frames == 0U) return 0U;
    return sample_cache_product_cost_bytes(desc->total_frames, desc->format);
}
static const char *sample_cache_path(uint16_t sample_id)
{
    uint16_t global = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (sample_global_pool_find_by_backend(
            SAMPLE_GLOBAL_KIND_CLASSIC, sample_id, &global) == 0U) return 0;
    const sample_global_slot_t *const slot = sample_global_pool_get_slot(global);
    return (slot != 0) ? slot->path : 0;
}

static void sample_cache_clear_desc(sample_cache_desc_t *desc)
{
    if (desc == 0)
    {
        return;
    }

    memset(desc, 0, sizeof(*desc));
    desc->state = SAMPLE_CACHE_EMPTY;
}

static void sample_cache_release_slot(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return;
    }

    sample_stream_manager_release_sample(sample_id);
    sample_page_cache_port_clear(sample_audio_key_classic(sample_id));
    sampler_waveform_store_unregister_classic(sample_id);
}

static uint8_t sample_cache_prepare_via_page_cache(uint16_t sample_id,
                                                   sample_cache_desc_t *desc,
                                                   FIL *map_file,
                                                   const char *path)
{
    if (desc == 0)
    {
        return 0U;
    }

    sample_page_stream_info_t stream_info;
    uint16_t prefill_pages = 0U;
    if (sample_page_cache_port_prepare_stream(
            sample_audio_key_classic(sample_id), path, &desc->info,
            desc->total_frames, desc->info.data_offset, map_file,
            &stream_info, &prefill_pages) == 0U)
    {
        desc->last_error = 12U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        return 0U;
    }
    if (prefill_pages == 0U)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        return 0U;
    }
    desc->format = stream_info.format;
    desc->stride_floats = stream_info.stride_floats;
    desc->frames_per_page = stream_info.frames_per_page;
    desc->registration_epoch = stream_info.registration_epoch;

    desc->state = SAMPLE_CACHE_READY;
    desc->last_error = 0U;
    return 1U;
}

static uint32_t sample_cache_trim_path_copy(char *dst, uint32_t dst_size, const char *src)
{
    uint32_t start = 0U;
    uint32_t end;

    if ((dst == 0) || (dst_size == 0U) || (src == 0))
    {
        return 0U;
    }

    end = (uint32_t)strlen(src);
    while ((start < end) && (isspace((unsigned char)src[start]) != 0))
    {
        start++;
    }
    while ((end > start) && (isspace((unsigned char)src[end - 1U]) != 0))
    {
        end--;
    }

    const uint32_t len = end - start;
    if ((len == 0U) || (len >= dst_size))
    {
        return 0U;
    }

    memcpy(dst, &src[start], len);
    dst[len] = '\0';
    return len;
}

uint8_t sample_cache_wav_format_supported(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return wav_parser_is_canonical_brick_float(info);
}

static uint32_t sample_cache_stream_last_page_index(const sample_cache_desc_t *desc)
{
    if ((desc == 0) || (desc->total_frames == 0U))
    {
        return 0U;
    }

    return sample_audio_format_page_index_from_frame(desc->format, desc->total_frames - 1U);
}

static uint8_t sample_cache_stream_start_base_failed(uint16_t sample_id,
                                                     const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (desc == 0)
        || (desc->total_frames == 0U))
    {
        return 0U;
    }
    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    uint32_t required_pages = sample_audio_format_presocle_pages(desc->format);
    if (required_pages > (last_page + 1U))
    {
        required_pages = last_page + 1U;
    }
    for (uint32_t page = 0U; page < required_pages; ++page)
    {
        if (sample_page_cache_get_page_state(sample_id, page) == SAMPLE_PAGE_FAILED)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t sample_cache_stream_start_base_ready(uint16_t sample_id,
                                                    const sample_cache_desc_t *desc)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (desc == 0)
        || (desc->total_frames == 0U))
    {
        return 0U;
    }
    const uint32_t last_page = sample_cache_stream_last_page_index(desc);
    uint32_t required_pages = sample_audio_format_presocle_pages(desc->format);
    if (required_pages > (last_page + 1U))
    {
        required_pages = last_page + 1U;
    }
    for (uint32_t page = 0U; page < required_pages; ++page)
    {
        if (sample_page_cache_get_page_state(sample_id, page) != SAMPLE_PAGE_READY)
        {
            return 0U;
        }
    }
    return 1U;
}

void sample_cache_init(void)
{
    sample_classic_audio_projection_init();
    sample_page_cache_reset();
    sample_stream_manager_init();
    if ((g_sample_cache_stream_gate_held != 0U)
        && (sd_access_gate_current_owner() == SD_ACCESS_CLIENT_SAMPLE_STREAM))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
    }
    g_sample_cache_stream_gate_held = 0U;

    for (uint32_t i = 0U; i < SAMPLE_CLASSIC_CAPACITY; ++i)
    {
        sample_cache_clear_desc(&g_sample_cache[i]);
        g_sample_cache_last_fresult[i] = FR_OK;
        g_sample_cache_projection_published[i] = 0U;
    }

    sample_stream_coop_file_init(&g_sample_cache_coop_file);
    memset(&g_sample_cache_coop, 0, sizeof(g_sample_cache_coop));

}

static uint16_t sample_cache_coop_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8U);
}

static uint32_t sample_cache_coop_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8U)
        | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
}

static uint32_t sample_cache_coop_crc32(const uint8_t *data, uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0U; i < size; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return ~crc;
}

static void sample_cache_coop_finish(uint8_t success, FRESULT result)
{
    const uint16_t id = g_sample_cache_coop.sample_id;
    if ((success == 0U) && (id < SAMPLE_CLASSIC_CAPACITY))
    {
        sample_cache_release_slot(id);
        g_sample_cache[id].state = SAMPLE_CACHE_ERROR;
        if (g_sample_cache[id].last_error == 0U) g_sample_cache[id].last_error = 5U;
        g_sample_cache_last_fresult[id] = result;
    }
    sample_stream_coop_file_close(&g_sample_cache_coop_file);
    g_sample_cache_coop.active = 0U;
    g_sample_cache_coop.terminal = 1U;
    g_sample_cache_coop.success = success;
}

uint8_t sample_cache_prepare_cooperative_begin(uint16_t sample_id,
                                               const char *path)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (path == NULL)
        || (path[0] == '\0') || (strlen(path) >= SAMPLE_CLASSIC_PATH_MAX)
        || (g_sample_cache_coop.active != 0U)
        || (g_sample_cache_coop.terminal != 0U)) return 0U;
    sample_cache_clear(sample_id);
    memset(&g_sample_cache_coop, 0, sizeof(g_sample_cache_coop));
    memcpy(g_sample_cache_coop.path, path, strlen(path) + 1U);
    g_sample_cache_coop.sample_id = sample_id;
    g_sample_cache_coop.phase = SAMPLE_CACHE_COOP_RIFF;
    g_sample_cache[sample_id].state = SAMPLE_CACHE_PREPARING;
    if (sample_stream_coop_file_begin(&g_sample_cache_coop_file, path) == 0U)
    {
        g_sample_cache[sample_id].state = SAMPLE_CACHE_ERROR;
        return 0U;
    }
    g_sample_cache_coop.active = 1U;
    return 1U;
}

static uint8_t sample_cache_coop_read(uint32_t offset, uint32_t bytes)
{
    if (g_sample_cache_coop.waiting != 0U)
    {
        if (sample_stream_coop_file_read_done(&g_sample_cache_coop_file) == 0U)
            return 0U;
        g_sample_cache_coop.waiting = 0U;
        return 1U;
    }
    if (bytes > sizeof(g_sample_cache_coop.io)
        || sample_stream_coop_file_read_begin(&g_sample_cache_coop_file,
                offset, g_sample_cache_coop.io, bytes) == 0U)
        return 0U;
    g_sample_cache_coop.waiting = 1U;
    return 0U;
}

static void sample_cache_coop_parse_service(void)
{
    sample_cache_coop_load_t *const load = &g_sample_cache_coop;
    if (load->phase == SAMPLE_CACHE_COOP_RIFF)
    {
        if (sample_cache_coop_read(0U, 12U) == 0U) return;
        if (memcmp(load->io, "RIFF", 4U) != 0
            || memcmp(&load->io[8], "WAVE", 4U) != 0)
        { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
        const uint64_t limit = (uint64_t)sample_cache_coop_le32(&load->io[4]) + 8ULL;
        if ((limit < 12ULL) || (limit > g_sample_cache_coop_file.file_size))
        { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
        load->parse_limit = (uint32_t)limit;
        load->cursor = 12U;
        load->phase = SAMPLE_CACHE_COOP_CHUNK;
        return;
    }
    if (load->phase == SAMPLE_CACHE_COOP_CHUNK)
    {
        if ((load->cursor + 8U) > load->parse_limit)
        { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
        if (sample_cache_coop_read(load->cursor, 8U) == 0U) return;
        load->chunk_size = sample_cache_coop_le32(&load->io[4]);
        load->chunk_payload = load->cursor + 8U;
        const uint64_t end = (uint64_t)load->chunk_payload + load->chunk_size;
        if (end > load->parse_limit)
        { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
        load->cursor = (uint32_t)end + (load->chunk_size & 1U);
        if (memcmp(load->io, "fmt ", 4U) == 0)
        {
            if (load->chunk_size < 16U)
            { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
            load->phase = SAMPLE_CACHE_COOP_FMT;
        }
        else if (memcmp(load->io, "fact", 4U) == 0)
        {
            if (load->chunk_size < 4U)
            { sample_cache_coop_finish(0U, FR_INVALID_OBJECT); return; }
            load->phase = SAMPLE_CACHE_COOP_FACT_FIRST;
        }
        else if (memcmp(load->io, "data", 4U) == 0)
        {
            load->info.data_offset = load->chunk_payload;
            load->info.data_size = load->chunk_size;
            load->have_data = 1U;
            load->phase = (load->have_fmt != 0U && load->have_fact != 0U)
                ? SAMPLE_CACHE_COOP_FACT_LAST : SAMPLE_CACHE_COOP_CHUNK;
        }
        return;
    }
    if (load->phase == SAMPLE_CACHE_COOP_FMT)
    {
        const uint32_t bytes = (load->chunk_size > 40U) ? 40U : load->chunk_size;
        if (sample_cache_coop_read(load->chunk_payload, bytes) == 0U) return;
        load->info.audio_format = sample_cache_coop_le16(&load->io[0]);
        load->info.encoding = (load->info.audio_format == 3U)
            ? WAV_SAMPLE_ENCODING_IEEE_FLOAT : WAV_SAMPLE_ENCODING_INVALID;
        load->info.channels = sample_cache_coop_le16(&load->io[2]);
        load->info.sample_rate = sample_cache_coop_le32(&load->io[4]);
        load->info.byte_rate = sample_cache_coop_le32(&load->io[8]);
        load->info.block_align = sample_cache_coop_le16(&load->io[12]);
        load->info.bits_per_sample = sample_cache_coop_le16(&load->io[14]);
        load->info.valid_bits_per_sample = load->info.bits_per_sample;
        load->info.fmt_chunk_size = load->chunk_size;
        load->have_fmt = 1U;
        load->phase = (load->have_data != 0U && load->have_fact != 0U)
            ? SAMPLE_CACHE_COOP_FACT_LAST : SAMPLE_CACHE_COOP_CHUNK;
        return;
    }
    if (load->phase == SAMPLE_CACHE_COOP_FACT_FIRST)
    {
        if (sample_cache_coop_read(load->chunk_payload, 4U) == 0U) return;
        load->info.fact_sample_length = sample_cache_coop_le32(load->io);
        load->info.has_fact = 1U;
        load->have_fact = 1U;
        load->phase = (load->have_data != 0U && load->have_fmt != 0U)
            ? SAMPLE_CACHE_COOP_FACT_LAST : SAMPLE_CACHE_COOP_CHUNK;
        return;
    }
    if (load->phase == SAMPLE_CACHE_COOP_FACT_LAST)
    {
        if (wav_parser_is_canonical_brick_float(&load->info) == 0U)
        { sample_cache_coop_finish(0U, FR_INVALID_PARAMETER); return; }
        if (sample_cache_coop_read(load->info.data_offset, 8U) == 0U) return;
        memcpy(&load->io[16], load->io, 8U);
        load->phase = SAMPLE_CACHE_COOP_INSTALL;
        return;
    }
    if (load->phase == SAMPLE_CACHE_COOP_INSTALL)
    {
        if (load->edge_ready == 0U)
        {
            const uint32_t last = load->info.data_offset + load->info.data_size
                - load->info.block_align;
            if (sample_cache_coop_read(last, 8U) == 0U) return;
            memcpy(&load->io[24], load->io, 8U);
            load->source_signature = sample_cache_coop_crc32(&load->io[16], 16U);
            load->edge_ready = 1U;
            return;
        }
        sample_stream_physical_map_t map;
        memset(&map, 0, sizeof(map));
        uint32_t file_size = 0U;
        if (sample_stream_coop_file_take_map(
                &g_sample_cache_coop_file, &map, &file_size) == 0U)
        { sample_cache_coop_finish(0U, FR_INT_ERR); return; }
        sample_cache_desc_t *const desc = &g_sample_cache[load->sample_id];
        desc->info = load->info;
        desc->total_frames = desc->info.data_size / desc->info.block_align;
        desc->format = sample_audio_format_from_channels(desc->info.channels);
        desc->stride_floats = (uint16_t)sample_audio_format_stride_floats(desc->format);
        desc->frames_per_page = sample_audio_format_frames_per_page(desc->format);
        desc->state = SAMPLE_CACHE_PREFILLING;
        sample_page_stream_info_t stream;
        uint16_t prefill = 0U;
        if (sample_page_cache_port_prepare_stream_mapped(
                sample_audio_key_classic(load->sample_id), load->path,
                &desc->info, desc->total_frames, desc->info.data_offset,
                &map, file_size, &stream, &prefill) == 0U)
        {
            sample_stream_physical_map_release(&map);
            sample_cache_coop_finish(0U, FR_NOT_ENOUGH_CORE);
            return;
        }
        desc->registration_epoch = stream.registration_epoch;
        if (sampler_waveform_store_register_classic_prepared(
                load->sample_id, load->path, &desc->info, file_size,
                load->source_signature) == 0U)
        {
            sample_page_cache_port_clear(sample_audio_key_classic(load->sample_id));
            sample_cache_coop_finish(0U, FR_INVALID_OBJECT);
            return;
        }
        desc->state = SAMPLE_CACHE_READY;
        desc->last_error = 0U;
        g_sample_cache_last_fresult[load->sample_id] = FR_OK;
        sample_cache_coop_finish(1U, FR_OK);
    }
}

uint8_t sample_cache_prepare_cooperative_active(void)
{
    return g_sample_cache_coop.active;
}

uint8_t sample_cache_prepare_cooperative_take_result(uint16_t *out_sample_id,
                                                     uint8_t *out_success)
{
    if (g_sample_cache_coop.terminal == 0U) return 0U;
    if (out_sample_id != NULL) *out_sample_id = g_sample_cache_coop.sample_id;
    if (out_success != NULL) *out_success = g_sample_cache_coop.success;
    g_sample_cache_coop.terminal = 0U;
    return 1U;
}

void sample_cache_clear(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return;
    }

    sample_classic_audio_projection_withdraw(sample_id);
    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;
    g_sample_cache_projection_published[sample_id] = 0U;
}

static uint8_t sample_cache_prepare_internal(uint16_t sample_id,
                                              const char *path,
                                              const wav_info_t *prepared_info,
                                              uint32_t prepared_file_size,
                                              uint32_t prepared_crc32,
                                              uint32_t prepared_cost_bytes)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return 0U;
    }

    g_sample_cache_last_fresult[sample_id] = FR_OK;

    char prepared_path[SAMPLE_CLASSIC_PATH_MAX];
    if (sample_cache_trim_path_copy(prepared_path, sizeof(prepared_path), path) == 0U)
    {
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_NAME;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        g_sample_cache_last_fresult[sample_id] = FR_TIMEOUT;
        return 0U;
    }

    sample_classic_audio_projection_withdraw(sample_id);
    g_sample_cache_projection_published[sample_id] = 0U;
    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    sample_global_pool_clear_slot(sample_id);

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->state = SAMPLE_CACHE_PREPARING;

    uint8_t ok = 0U;
    uint8_t fp_open = 0U;
    FIL fp;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        desc->last_error = 3U;
        g_sample_cache_last_fresult[sample_id] = FR_DISK_ERR;
        goto done;
    }

    const FRESULT open_fr = f_open(&fp, prepared_path, FA_READ);
    if (open_fr != FR_OK)
    {
        desc->last_error = 4U;
        g_sample_cache_last_fresult[sample_id] = open_fr;
        goto done;
    }
    fp_open = 1U;

    uint32_t actual_crc32 = 0U;
    if ((prepared_info != NULL)
        && (((uint32_t)f_size(&fp) != prepared_file_size)
            || (wav_parser_crc32_file(&fp, &actual_crc32) == 0U)
            || (actual_crc32 != prepared_crc32)))
    {
        goto done;
    }

    if (prepared_info != NULL)
    {
        desc->info = *prepared_info;
    }
    else
    {
        if (wav_parser_parse_info(&fp, &desc->info) == 0U)
        {
            desc->last_error = 5U;
            g_sample_cache_last_fresult[sample_id] = FR_INVALID_OBJECT;
            goto done;
        }

        if (desc->info.sample_rate != 48000U)
        {
            desc->last_error = 15U;
            g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
            goto done;
        }

        if (sample_cache_wav_format_supported(&desc->info) == 0U)
        {
            desc->last_error = 6U;
            g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
            goto done;
        }
    }

    desc->total_frames = desc->info.data_size / desc->info.block_align;
    desc->format = sample_audio_format_from_channels(desc->info.channels);
    desc->stride_floats = (uint16_t)sample_audio_format_stride_floats(desc->format);
    desc->frames_per_page = sample_audio_format_frames_per_page(desc->format);
    if (sample_audio_format_is_valid(desc->format) == 0U)
    {
        desc->last_error = 6U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }
    if (desc->total_frames == 0U)
    {
        desc->last_error = 7U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    desc->state = SAMPLE_CACHE_PREFILLING;
    if (sample_cache_prepare_via_page_cache(sample_id, desc, &fp,
                                            prepared_path) == 0U)
    {
        goto done;
    }

    if (sampler_waveform_store_register_classic(sample_id, prepared_path,
                                                 &desc->info, &fp) == 0U)
    {
        desc->last_error = 16U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_OBJECT;
        goto done;
    }

    ok = 1U;

done:
    if (fp_open != 0U)
    {
        (void)f_close(&fp);
    }
    if (ok != 0U)
    {
        const uint32_t product_cost = (prepared_info != NULL)
            ? prepared_cost_bytes
            : sample_cache_product_cost_bytes(desc->total_frames, desc->format);
        if ((product_cost == 0U)
            || (sample_global_pool_register_classic_at(sample_id, prepared_path,
                                                       product_cost) == 0U))
        {
            ok = 0U;
            desc->last_error = 8U;
            g_sample_cache_last_fresult[sample_id] = FR_DENIED;
        }
    }
    if (ok == 0U)
    {
        sample_cache_release_slot(sample_id);
        desc->state = SAMPLE_CACHE_ERROR;
    }
    else if (sample_cache_is_ready(sample_id) != 0U)
    {
        if (sample_classic_audio_projection_publish(sample_id) != 0U)
            g_sample_cache_projection_published[sample_id] = 1U;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    return ok;
}

uint8_t sample_cache_prepare(uint16_t sample_id, const char *path)
{
    return sample_cache_prepare_internal(sample_id, path, NULL, 0U, 0U, 0U);
}

uint8_t sample_cache_prepare_prevalidated(uint16_t sample_id,
                                          const char *path,
                                          const wav_info_t *info,
                                          uint32_t source_file_size,
                                          uint32_t source_crc32,
                                          uint32_t prepared_cost_bytes)
{
    if (info == NULL) return 0U;
    return sample_cache_prepare_internal(sample_id, path, info,
                                         source_file_size, source_crc32,
                                         prepared_cost_bytes);
}

void sample_cache_service(uint32_t byte_budget)
{
    sample_cache_sync_projections();
    if (g_sample_cache_coop.active != 0U)
    {
        sample_stream_coop_file_service(&g_sample_cache_coop_file, 16U);
        if (g_sample_cache_coop_file.state == SAMPLE_STREAM_COOP_ERROR)
            sample_cache_coop_finish(0U, g_sample_cache_coop_file.result);
        else if ((g_sample_cache_coop_file.state == SAMPLE_STREAM_COOP_READY)
                && (g_sample_cache_coop.active != 0U))
            sample_cache_coop_parse_service();
    }
    if (byte_budget == 0U)
    {
        return;
    }

    if (sample_stream_manager_has_pending_sd_work() == 0U) return;
    if ((g_sample_cache_stream_gate_held == 0U)
        && (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_STREAM) == 0U))
    {
        return;
    }
    g_sample_cache_stream_gate_held = 1U;

    sample_stream_manager_service(byte_budget);
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_STREAM);
    g_sample_cache_stream_gate_held = 0U;
    sample_cache_sync_projections();
}

uint8_t sample_cache_has_pending_sd_work(void)
{
    return sample_stream_manager_has_pending_sd_work();
}

uint8_t sample_cache_is_ready(uint16_t sample_id)
{
    return (sample_cache_get_slot_readiness(sample_id) == SAMPLE_CACHE_SLOT_PLAYABLE) ? 1U : 0U;
}

sample_cache_slot_readiness_t sample_cache_get_slot_readiness(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return SAMPLE_CACHE_SLOT_ERROR;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    switch (desc->state)
    {
        case SAMPLE_CACHE_EMPTY:
            return SAMPLE_CACHE_SLOT_EMPTY;

        case SAMPLE_CACHE_PREPARING:
        case SAMPLE_CACHE_PREFILLING:
            return SAMPLE_CACHE_SLOT_PREPARING;

        case SAMPLE_CACHE_READY:
            if (sample_cache_stream_start_base_failed(sample_id, desc) != 0U)
            {
                return SAMPLE_CACHE_SLOT_ERROR;
            }
            if (sample_cache_stream_start_base_ready(sample_id, desc) != 0U)
            {
                return SAMPLE_CACHE_SLOT_PLAYABLE;
            }
            return SAMPLE_CACHE_SLOT_START_PENDING;

        case SAMPLE_CACHE_ERROR:
        default:
            return SAMPLE_CACHE_SLOT_ERROR;
    }
}

sample_cache_state_t sample_cache_get_state(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return SAMPLE_CACHE_ERROR;
    }

    return g_sample_cache[sample_id].state;
}

uint8_t sample_cache_get_last_error(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return 1U;
    }

    return g_sample_cache[sample_id].last_error;
}

uint8_t sample_cache_get_last_fresult(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_CLASSIC_CAPACITY)
    {
        return (uint8_t)FR_INVALID_PARAMETER;
    }

    return (uint8_t)g_sample_cache_last_fresult[sample_id];
}

uint8_t sample_cache_resolve_classic_source(uint16_t sample_id,
                                            sample_resolved_source_t *out_source)
{
    if (out_source != 0)
    {
        sample_resolved_source_init(out_source);
    }
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (out_source == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->total_frames == 0U)
        || (desc->state == SAMPLE_CACHE_EMPTY)
        || (desc->state == SAMPLE_CACHE_ERROR))
    {
        return 0U;
    }

    out_source->key = sample_audio_key_classic(sample_id);
    out_source->path = sample_cache_path(sample_id);
    out_source->total_frames = desc->total_frames;
    out_source->format = desc->format;
    out_source->stride_floats = desc->stride_floats;
    out_source->frames_per_page = desc->frames_per_page;
    out_source->registration_epoch = desc->registration_epoch;
    out_source->root_note = 60U;
    out_source->fine_tune_cents = 0;
    out_source->region_begin = 0U;
    out_source->region_end = desc->total_frames;
    out_source->loop_begin = 0U;
    out_source->loop_end = desc->total_frames;
    out_source->loop_mode = SAMPLE_PLAY_LOOP_NONE;
    out_source->rate = 1.0f;
    out_source->gain = 1.0f;
    out_source->owner_track_id = UINT8_MAX;
    out_source->note = 60U;
    out_source->velocity = 127U;
    out_source->source_kind = 0U;
    out_source->instrument_id = UINT16_MAX;
    out_source->zone_id = UINT16_MAX;
    return sample_resolved_source_is_valid(out_source);
}

uint8_t sample_cache_peek_frame(uint16_t sample_id, uint32_t frame_index, float *out_l, float *out_r)
{
    if ((sample_id >= SAMPLE_CLASSIC_CAPACITY) || (out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if (desc->state != SAMPLE_CACHE_READY)
    {
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        return 0U;
    }

    sample_page_span_t page;
    const uint32_t page_index = sample_audio_format_page_index_from_frame(
        desc->format, frame_index);
    if (sample_page_cache_control_resolve_page(
            sample_id, page_index, &page) == 0U)
    {
        return 0U;
    }
    const uint32_t page_offset = frame_index - page.start_frame;
    const float *const frame =
        &page.frames_interleaved[page_offset * page.stride_floats];
    *out_l = frame[0];
    *out_r = (page.format == SAMPLE_AUDIO_FORMAT_FLOAT32_MONO)
                 ? frame[0] : frame[1U];
    return 1U;
}
