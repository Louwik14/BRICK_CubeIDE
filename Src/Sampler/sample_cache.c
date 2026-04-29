#include "Sampler/sample_cache.h"

#include <ctype.h>
#include <string.h>

#include "Storage/memory_layout.h"
#include "Storage/sd_access_gate.h"
#include "Storage/wav_audio_codec.h"
#include "ff.h"

#define SAMPLE_CACHE_SLOT_COUNT (4U)
#define SAMPLE_CACHE_SLOT_FRAMES (65536U)
#define SAMPLE_CACHE_MAX_VOICES (16U)
#define SAMPLE_CACHE_IO_BYTES (4096U)
#define SAMPLE_CACHE_REFILL_LOW_WATER_FRAMES (16384U)

SDRAM_SAMPLES static sample_cache_desc_t g_sample_cache[SAMPLE_POOL_SIZE];
SDRAM_SAMPLES static float g_sample_cache_data[SAMPLE_CACHE_SLOT_COUNT][SAMPLE_CACHE_SLOT_FRAMES * 2U];
static CTRL_STATE int16_t g_cache_slot_by_sample[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_cache_slot_in_use[SAMPLE_CACHE_SLOT_COUNT];
static CTRL_STATE sample_cache_voice_t g_sample_cache_voice[SAMPLE_CACHE_MAX_VOICES];
static AUDIO_WARM uint8_t g_sample_cache_io_storage[SAMPLE_CACHE_IO_BYTES + 1U];
static FIL g_sample_cache_file[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_sample_cache_file_open[SAMPLE_POOL_SIZE];
static CTRL_STATE FRESULT g_sample_cache_last_fresult[SAMPLE_POOL_SIZE];

static uint8_t sample_cache_open_source(uint16_t sample_id);
static void sample_cache_close_stream_file(uint16_t sample_id);

static uint8_t *sample_cache_io_buffer(void)
{
    /*
     * Keep FatFs on the diskio scratch-buffer path. The preview path works
     * through a non-direct destination; this avoids the direct multi-block DMA
     * path for sample import while still decoding into the SDRAM cache after IO.
     */
    return &g_sample_cache_io_storage[1U];
}

static void sample_cache_clear_desc(sample_cache_desc_t *desc)
{
    if (desc == 0)
    {
        return;
    }

    memset(desc, 0, sizeof(*desc));
    desc->state = SAMPLE_CACHE_EMPTY;
    desc->mode = SAMPLE_CACHE_MODE_FULL;
}

static void sample_cache_release_slot(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return;
    }

    const int16_t slot = g_cache_slot_by_sample[sample_id];
    if ((slot >= 0) && ((uint32_t)slot < SAMPLE_CACHE_SLOT_COUNT))
    {
        g_cache_slot_in_use[(uint32_t)slot] = 0U;
    }
    g_cache_slot_by_sample[sample_id] = -1;

    if (g_sample_cache_file_open[sample_id] != 0U)
    {
        (void)f_close(&g_sample_cache_file[sample_id]);
        g_sample_cache_file_open[sample_id] = 0U;
    }
}

static int16_t sample_cache_alloc_slot(void)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_SLOT_COUNT; ++i)
    {
        if (g_cache_slot_in_use[i] == 0U)
        {
            g_cache_slot_in_use[i] = 1U;
            return (int16_t)i;
        }
    }

    return -1;
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

static uint8_t sample_cache_format_supported(const wav_info_t *info)
{
    if (info == 0)
    {
        return 0U;
    }

    return (((info->audio_format == 1U) || (info->audio_format == 65534U))
            && ((info->channels == 1U) || (info->channels == 2U))
            && ((info->bits_per_sample == 16U) || (info->bits_per_sample == 24U))
            && (info->sample_rate == 48000U)
            && (info->block_align != 0U)) ? 1U : 0U;
}

static uint8_t sample_cache_read_frames(FIL *fp,
                                        sample_cache_desc_t *desc,
                                        uint32_t max_frames,
                                        uint32_t *out_loaded_frames)
{
    if (out_loaded_frames != 0)
    {
        *out_loaded_frames = 0U;
    }

    if ((fp == 0) || (desc == 0) || (desc->cache == 0) || (desc->info.block_align == 0U)
        || (max_frames == 0U))
    {
        return 0U;
    }

    uint32_t loaded_frames = 0U;
    uint32_t remaining_frames = desc->total_frames - desc->source_read_frame;
    if (remaining_frames > max_frames)
    {
        remaining_frames = max_frames;
    }

    while ((remaining_frames != 0U) && (desc->cache_valid_frames < desc->cache_capacity_frames))
    {
        uint32_t request_frames = remaining_frames;
        const uint32_t free_frames = desc->cache_capacity_frames - desc->cache_valid_frames;
        if (request_frames > free_frames)
        {
            request_frames = free_frames;
        }

        uint32_t request = request_frames * desc->info.block_align;
        if (request > SAMPLE_CACHE_IO_BYTES)
        {
            request = SAMPLE_CACHE_IO_BYTES;
        }
        request -= (request % desc->info.block_align);
        if (request == 0U)
        {
            break;
        }

        uint8_t *const io_buf = sample_cache_io_buffer();
        UINT br = 0U;
        const FRESULT fr = f_read(fp, io_buf, request, &br);
        if (fr != FR_OK)
        {
            desc->last_error = 12U;
            g_sample_cache_last_fresult[desc->sample_id] = fr;
            return 0U;
        }
        if (br == 0U)
        {
            desc->last_error = 13U;
            return 0U;
        }

        const uint32_t valid_bytes = br - (br % desc->info.block_align);
        if (valid_bytes == 0U)
        {
            desc->last_error = 14U;
            return 0U;
        }
        uint32_t pos = 0U;
        while ((pos + desc->info.block_align <= valid_bytes)
               && (remaining_frames != 0U)
               && (desc->cache_valid_frames < desc->cache_capacity_frames))
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(&io_buf[pos],
                                                desc->info.channels,
                                                desc->info.bits_per_sample,
                                                &left,
                                                &right);
            const uint32_t cache_pos = desc->source_read_frame % desc->cache_capacity_frames;
            desc->cache[cache_pos * 2U] = left;
            desc->cache[cache_pos * 2U + 1U] = right;
            __asm volatile ("" ::: "memory");
            desc->cache_valid_frames++;
            desc->source_read_frame++;
            loaded_frames++;
            remaining_frames--;
            pos += desc->info.block_align;
        }
    }

    if (out_loaded_frames != 0)
    {
        *out_loaded_frames = loaded_frames;
    }
    return (loaded_frames != 0U) ? 1U : 0U;
}

static uint8_t sample_cache_seek_source(uint16_t sample_id, uint32_t frame_index)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if (sample_cache_open_source(sample_id) == 0U)
    {
        return 0U;
    }

    const FSIZE_t offset = (FSIZE_t)desc->data_offset
                         + ((FSIZE_t)frame_index * (FSIZE_t)desc->info.block_align);
    const FRESULT fr = f_lseek(&g_sample_cache_file[sample_id], offset);
    if (fr != FR_OK)
    {
        g_sample_cache_last_fresult[sample_id] = fr;
        sample_cache_close_stream_file(sample_id);
        return 0U;
    }

    desc->source_read_frame = frame_index;
    return 1U;
}

static uint8_t sample_cache_open_source(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    if (g_sample_cache_file_open[sample_id] != 0U)
    {
        return 1U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    FRESULT fr = f_open(&g_sample_cache_file[sample_id], desc->path, FA_READ);
    if (fr != FR_OK)
    {
        g_sample_cache_last_fresult[sample_id] = fr;
        return 0U;
    }

    g_sample_cache_file_open[sample_id] = 1U;
    const FSIZE_t offset = (FSIZE_t)desc->data_offset
                         + ((FSIZE_t)desc->source_read_frame * (FSIZE_t)desc->info.block_align);
    fr = f_lseek(&g_sample_cache_file[sample_id], offset);
    if (fr != FR_OK)
    {
        g_sample_cache_last_fresult[sample_id] = fr;
        (void)f_close(&g_sample_cache_file[sample_id]);
        g_sample_cache_file_open[sample_id] = 0U;
        return 0U;
    }

    return 1U;
}

static uint32_t sample_cache_min_play_frame(uint16_t sample_id)
{
    uint32_t min_frame = UINT32_MAX;

    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        const sample_cache_voice_t *const voice = &g_sample_cache_voice[i];
        if ((voice->active == 0U) || (voice->sample_id != sample_id))
        {
            continue;
        }

        if (voice->frame_pos < min_frame)
        {
            min_frame = voice->frame_pos;
        }
    }

    return min_frame;
}

static uint8_t sample_cache_compact_for_playback(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    const uint32_t min_frame = sample_cache_min_play_frame(sample_id);
    if ((min_frame == UINT32_MAX) || (min_frame <= desc->cache_window_start_frame))
    {
        return 1U;
    }

    const uint32_t cache_end = desc->cache_window_start_frame + desc->cache_valid_frames;
    if (min_frame >= cache_end)
    {
        desc->cache_window_start_frame = min_frame;
        desc->cache_valid_frames = 0U;
        return 1U;
    }

    const uint32_t drop_frames = min_frame - desc->cache_window_start_frame;
    desc->cache_window_start_frame = min_frame;
    desc->cache_valid_frames -= drop_frames;
    return 1U;
}

static uint8_t sample_cache_refill_sample(uint16_t sample_id, uint32_t byte_budget)
{
    if ((sample_id >= SAMPLE_POOL_SIZE) || (byte_budget < g_sample_cache[sample_id].info.block_align))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM)
        || ((desc->state != SAMPLE_CACHE_PLAYING) && (desc->state != SAMPLE_CACHE_PREFILLING))
        || (desc->source_read_frame >= desc->total_frames))
    {
        return 0U;
    }

    if (sample_cache_compact_for_playback(sample_id) == 0U)
    {
        return 0U;
    }

    if (desc->cache_valid_frames >= desc->cache_capacity_frames)
    {
        return 0U;
    }

    if (sample_cache_open_source(sample_id) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 10U;
        return 0U;
    }

    const uint32_t max_frames = byte_budget / desc->info.block_align;
    uint32_t loaded_frames = 0U;
    if (sample_cache_read_frames(&g_sample_cache_file[sample_id], desc, max_frames, &loaded_frames) == 0U)
    {
        if (desc->source_read_frame >= desc->total_frames)
        {
            if (g_sample_cache_file_open[sample_id] != 0U)
            {
                (void)f_close(&g_sample_cache_file[sample_id]);
                g_sample_cache_file_open[sample_id] = 0U;
            }
            return 0U;
        }
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 11U;
        return 0U;
    }

    if (desc->source_read_frame >= desc->total_frames)
    {
        if (g_sample_cache_file_open[sample_id] != 0U)
        {
            (void)f_close(&g_sample_cache_file[sample_id]);
            g_sample_cache_file_open[sample_id] = 0U;
        }
    }

    return (loaded_frames != 0U) ? 1U : 0U;
}

static uint8_t sample_cache_frame_available(const sample_cache_desc_t *desc, uint32_t frame_index)
{
    if ((desc == 0) || (desc->cache == 0) || (desc->cache_valid_frames == 0U)
        || (frame_index >= desc->total_frames))
    {
        return 0U;
    }

    if (frame_index < desc->cache_window_start_frame)
    {
        return 0U;
    }

    const uint32_t relative = frame_index - desc->cache_window_start_frame;
    return ((relative < desc->cache_valid_frames) && (relative < desc->cache_capacity_frames)) ? 1U : 0U;
}

static uint32_t sample_cache_frame_offset(const sample_cache_desc_t *desc, uint32_t frame_index)
{
    return (desc->cache_capacity_frames == 0U) ? 0U : (frame_index % desc->cache_capacity_frames);
}

static sample_cache_block_status_t sample_cache_inspect_voice_block(uint8_t voice_id,
                                                                    uint32_t max_frames,
                                                                    sample_cache_block_t *out_block)
{
    if (out_block == 0)
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    out_block->l = 0;
    out_block->r = 0;
    out_block->frames = 0U;
    out_block->frame_stride = 0U;
    out_block->is_mono = 0U;
    out_block->status = SAMPLE_CACHE_BLOCK_NOT_READY;

    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (max_frames == 0U))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    const sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if ((desc->cache == 0) || (desc->cache_valid_frames == 0U)
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        return SAMPLE_CACHE_BLOCK_NOT_READY;
    }

    if (voice->frame_pos >= desc->total_frames)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_DONE;
        return SAMPLE_CACHE_BLOCK_DONE;
    }

    if (sample_cache_frame_available(desc, voice->frame_pos) == 0U)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_UNDERRUN;
        return SAMPLE_CACHE_BLOCK_UNDERRUN;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, voice->frame_pos);
    uint32_t frames = desc->total_frames - voice->frame_pos;
    const uint32_t window_frames =
        (desc->cache_window_start_frame + desc->cache_valid_frames) - voice->frame_pos;
    const uint32_t wrap_frames = desc->cache_capacity_frames - cache_index;

    if (frames > max_frames)
    {
        frames = max_frames;
    }
    if (frames > window_frames)
    {
        frames = window_frames;
    }
    if (frames > wrap_frames)
    {
        frames = wrap_frames;
    }

    if (frames == 0U)
    {
        out_block->status = SAMPLE_CACHE_BLOCK_UNDERRUN;
        return SAMPLE_CACHE_BLOCK_UNDERRUN;
    }

    out_block->l = &desc->cache[cache_index * 2U];
    out_block->r = (desc->info.channels == 1U) ? 0 : (&desc->cache[cache_index * 2U + 1U]);
    out_block->frames = frames;
    out_block->frame_stride = 2U;
    out_block->is_mono = (desc->info.channels == 1U) ? 1U : 0U;
    out_block->status = SAMPLE_CACHE_BLOCK_OK;
    return SAMPLE_CACHE_BLOCK_OK;
}

static uint8_t sample_cache_start_frame_available(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    return sample_cache_frame_available(&g_sample_cache[sample_id], 0U);
}

static uint8_t sample_cache_any_voice_active(uint16_t sample_id)
{
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        if ((g_sample_cache_voice[i].active != 0U)
            && (g_sample_cache_voice[i].sample_id == sample_id))
        {
            return 1U;
        }
    }

    return 0U;
}

static void sample_cache_close_stream_file(uint16_t sample_id)
{
    if ((sample_id < SAMPLE_POOL_SIZE) && (g_sample_cache_file_open[sample_id] != 0U))
    {
        (void)f_close(&g_sample_cache_file[sample_id]);
        g_sample_cache_file_open[sample_id] = 0U;
    }
}

static void sample_cache_finish_playback(sample_cache_desc_t *desc,
                                         sample_cache_voice_t *voice,
                                         sample_cache_state_t state)
{
    if ((desc == 0) || (voice == 0))
    {
        return;
    }

    const uint16_t sample_id = voice->sample_id;
    voice->active = 0U;
    desc->stream_active = sample_cache_any_voice_active(sample_id);

    if (desc->fully_cached != 0U)
    {
        desc->state = SAMPLE_CACHE_READY_FULL;
    }
    else if (desc->mode == SAMPLE_CACHE_MODE_STREAM)
    {
        sample_cache_close_stream_file(sample_id);
        desc->state = (state == SAMPLE_CACHE_UNDERRUN) ? SAMPLE_CACHE_UNDERRUN : SAMPLE_CACHE_NEEDS_REPREPARE;
        desc->reprepare_start_frame = 0U;
    }
    else
    {
        desc->state = SAMPLE_CACHE_DONE;
    }
}

static uint16_t sample_cache_pick_refill_candidate(void)
{
    uint16_t best_sample = SAMPLE_POOL_SIZE;
    uint32_t best_available = UINT32_MAX;

    for (uint16_t sample_id = 0U; sample_id < SAMPLE_POOL_SIZE; ++sample_id)
    {
        const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
        if (desc->mode != SAMPLE_CACHE_MODE_STREAM)
        {
            continue;
        }

        if ((desc->state == SAMPLE_CACHE_NEEDS_REPREPARE)
            || (desc->state == SAMPLE_CACHE_DONE)
            || (desc->state == SAMPLE_CACHE_UNDERRUN))
        {
            return sample_id;
        }

        if ((desc->state == SAMPLE_CACHE_PLAYING) && (desc->source_read_frame < desc->total_frames))
        {
            const uint32_t min_frame = sample_cache_min_play_frame(sample_id);
            if (min_frame == UINT32_MAX)
            {
                continue;
            }

            uint32_t available = 0U;
            const uint32_t cache_end = desc->cache_window_start_frame + desc->cache_valid_frames;
            if (cache_end > min_frame)
            {
                available = cache_end - min_frame;
            }

            if ((available < best_available) && (available <= SAMPLE_CACHE_REFILL_LOW_WATER_FRAMES))
            {
                best_available = available;
                best_sample = sample_id;
            }
        }
    }

    return best_sample;
}

static uint8_t sample_cache_reprepare_window(uint16_t sample_id, uint32_t byte_budget)
{
    if ((sample_id >= SAMPLE_POOL_SIZE) || (byte_budget < g_sample_cache[sample_id].info.block_align))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->mode != SAMPLE_CACHE_MODE_STREAM)
        || ((desc->state != SAMPLE_CACHE_NEEDS_REPREPARE)
            && (desc->state != SAMPLE_CACHE_DONE)
            && (desc->state != SAMPLE_CACHE_UNDERRUN)))
    {
        return 0U;
    }

    if (sample_cache_any_voice_active(sample_id) != 0U)
    {
        return 0U;
    }

    uint32_t start_frame = desc->reprepare_start_frame;
    if (start_frame >= desc->total_frames)
    {
        start_frame = 0U;
    }

    desc->state = SAMPLE_CACHE_PREFILLING;
    desc->cache_window_start_frame = start_frame;
    desc->cache_valid_frames = 0U;
    desc->fully_cached = 0U;
    desc->stream_active = 0U;
    desc->last_error = 0U;

    if (sample_cache_seek_source(sample_id, start_frame) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 10U;
        return 0U;
    }

    const uint32_t max_frames = byte_budget / desc->info.block_align;
    uint32_t loaded_frames = 0U;
    if (sample_cache_read_frames(&g_sample_cache_file[sample_id], desc, max_frames, &loaded_frames) == 0U)
    {
        sample_cache_close_stream_file(sample_id);
        desc->state = SAMPLE_CACHE_ERROR;
        if (desc->last_error == 0U)
        {
            desc->last_error = 11U;
        }
        return 0U;
    }

    sample_cache_close_stream_file(sample_id);
    desc->fully_cached = (desc->source_read_frame >= desc->total_frames) ? 1U : 0U;
    desc->state = (desc->fully_cached != 0U) ? SAMPLE_CACHE_READY_FULL : SAMPLE_CACHE_READY_PARTIAL;
    return 1U;
}

void sample_cache_init(void)
{
    for (uint32_t i = 0U; i < SAMPLE_POOL_SIZE; ++i)
    {
        if (g_sample_cache_file_open[i] != 0U)
        {
            (void)f_close(&g_sample_cache_file[i]);
        }
        sample_cache_clear_desc(&g_sample_cache[i]);
        g_cache_slot_by_sample[i] = -1;
        g_sample_cache_file_open[i] = 0U;
        g_sample_cache_last_fresult[i] = FR_OK;
    }

    memset(g_cache_slot_in_use, 0, sizeof(g_cache_slot_in_use));
    memset(g_sample_cache_voice, 0, sizeof(g_sample_cache_voice));
    for (uint32_t i = 0U; i < SAMPLE_CACHE_MAX_VOICES; ++i)
    {
        g_sample_cache_voice[i].voice_id = (uint8_t)i;
        g_sample_cache_voice[i].stop_on_underrun = 1U;
    }
}

void sample_cache_clear(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;
}

uint8_t sample_cache_prepare(uint16_t sample_id, const char *path)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);
    g_sample_cache_last_fresult[sample_id] = FR_OK;

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->sample_id = sample_id;
    desc->state = SAMPLE_CACHE_PREPARING;

    if (sample_cache_trim_path_copy(desc->path, sizeof(desc->path), path) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 1U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_NAME;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 2U;
        g_sample_cache_last_fresult[sample_id] = FR_TIMEOUT;
        return 0U;
    }

    uint8_t ok = 0U;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        desc->last_error = 3U;
        g_sample_cache_last_fresult[sample_id] = FR_DISK_ERR;
        goto done;
    }

    if (sample_cache_open_source(sample_id) == 0U)
    {
        desc->last_error = 4U;
        goto done;
    }

    if (wav_parser_parse_info(&g_sample_cache_file[sample_id], &desc->info) == 0U)
    {
        desc->last_error = 5U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_OBJECT;
        goto done;
    }

    if (sample_cache_format_supported(&desc->info) == 0U)
    {
        desc->last_error = 6U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    desc->total_frames = desc->info.data_size / desc->info.block_align;
    desc->data_offset = desc->info.data_offset;
    desc->cache_capacity_frames = SAMPLE_CACHE_SLOT_FRAMES;
    if (desc->total_frames == 0U)
    {
        desc->last_error = 7U;
        g_sample_cache_last_fresult[sample_id] = FR_INVALID_PARAMETER;
        goto done;
    }

    const int16_t slot = sample_cache_alloc_slot();
    if (slot < 0)
    {
        desc->last_error = 8U;
        g_sample_cache_last_fresult[sample_id] = FR_NOT_ENOUGH_CORE;
        goto done;
    }

    g_cache_slot_by_sample[sample_id] = slot;
    desc->cache = &g_sample_cache_data[(uint32_t)slot][0];
    desc->cache_window_start_frame = 0U;
    desc->state = SAMPLE_CACHE_PREFILLING;
    desc->mode = (desc->total_frames <= desc->cache_capacity_frames)
                     ? SAMPLE_CACHE_MODE_FULL
                     : SAMPLE_CACHE_MODE_STREAM;
    desc->source_read_frame = 0U;

    FRESULT seek_fr = f_lseek(&g_sample_cache_file[sample_id], desc->data_offset);
    if (seek_fr != FR_OK)
    {
        sample_cache_release_slot(sample_id);
        desc->cache = 0;
        desc->last_error = 9U;
        g_sample_cache_last_fresult[sample_id] = seek_fr;
        goto done;
    }

    uint32_t loaded_frames = 0U;
    if (sample_cache_read_frames(&g_sample_cache_file[sample_id],
                                 desc,
                                 desc->cache_capacity_frames,
                                 &loaded_frames) == 0U)
    {
        sample_cache_release_slot(sample_id);
        desc->cache = 0;
        if (desc->last_error == 0U)
        {
            desc->last_error = 12U;
        }
        goto done;
    }

    desc->fully_cached = (desc->source_read_frame >= desc->total_frames) ? 1U : 0U;
    desc->stream_active = 0U;
    desc->state = (desc->fully_cached != 0U) ? SAMPLE_CACHE_READY_FULL : SAMPLE_CACHE_READY_PARTIAL;
    desc->last_error = 0U;
    ok = 1U;
    if (g_sample_cache_file_open[sample_id] != 0U)
    {
        (void)f_close(&g_sample_cache_file[sample_id]);
        g_sample_cache_file_open[sample_id] = 0U;
    }

done:
    if (ok == 0U)
    {
        sample_cache_release_slot(sample_id);
        desc->state = SAMPLE_CACHE_ERROR;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
    return ok;
}

void sample_cache_service(uint32_t byte_budget)
{
    if (byte_budget == 0U)
    {
        return;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        return;
    }

    uint32_t remaining = byte_budget;
    while (remaining != 0U)
    {
        const uint16_t sample_id = sample_cache_pick_refill_candidate();
        if (sample_id >= SAMPLE_POOL_SIZE)
        {
            break;
        }

        if ((g_sample_cache[sample_id].state == SAMPLE_CACHE_NEEDS_REPREPARE)
            || (g_sample_cache[sample_id].state == SAMPLE_CACHE_DONE)
            || (g_sample_cache[sample_id].state == SAMPLE_CACHE_UNDERRUN))
        {
            if (sample_cache_reprepare_window(sample_id, remaining) == 0U)
            {
                break;
            }

            const uint32_t loaded_frames = g_sample_cache[sample_id].cache_valid_frames;
            const uint32_t consumed = loaded_frames * g_sample_cache[sample_id].info.block_align;
            if ((loaded_frames == 0U) || (consumed >= remaining))
            {
                break;
            }
            remaining -= consumed;
            continue;
        }

        const uint32_t before = g_sample_cache[sample_id].source_read_frame;
        if (sample_cache_refill_sample(sample_id, remaining) == 0U)
        {
            break;
        }

        const uint32_t loaded_frames = g_sample_cache[sample_id].source_read_frame - before;
        const uint32_t consumed = loaded_frames * g_sample_cache[sample_id].info.block_align;
        if ((loaded_frames == 0U) || (consumed >= remaining))
        {
            break;
        }
        remaining -= consumed;
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_SAMPLE_CACHE);
}

uint8_t sample_cache_is_ready(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    const sample_cache_state_t state = g_sample_cache[sample_id].state;
    if (state == SAMPLE_CACHE_READY_FULL)
    {
        return 1U;
    }

    if ((state == SAMPLE_CACHE_READY_PARTIAL) || (state == SAMPLE_CACHE_PLAYING))
    {
        return sample_cache_start_frame_available(sample_id);
    }

    return 0U;
}

sample_cache_state_t sample_cache_get_state(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return SAMPLE_CACHE_ERROR;
    }

    return g_sample_cache[sample_id].state;
}

uint8_t sample_cache_get_last_error(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 1U;
    }

    return g_sample_cache[sample_id].last_error;
}

uint8_t sample_cache_get_last_fresult(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return (uint8_t)FR_INVALID_PARAMETER;
    }

    return (uint8_t)g_sample_cache_last_fresult[sample_id];
}

uint8_t sample_cache_start_voice_at(uint16_t sample_id, uint8_t voice_id, uint32_t frame_index)
{
    if ((sample_id >= SAMPLE_POOL_SIZE) || (voice_id >= SAMPLE_CACHE_MAX_VOICES))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_READY_FULL)
        && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
        && (desc->state != SAMPLE_CACHE_PLAYING))
    {
        return 0U;
    }

    if ((desc->mode == SAMPLE_CACHE_MODE_STREAM)
        && (sample_cache_any_voice_active(sample_id) != 0U)
        && (g_sample_cache_voice[voice_id].active == 0U))
    {
        return 0U;
    }

    if (sample_cache_frame_available(desc, frame_index) == 0U)
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    voice->voice_id = voice_id;
    voice->sample_id = sample_id;
    voice->frame_pos = frame_index;
    voice->active = 1U;
    voice->stop_on_underrun = 1U;
    desc->stream_active = (desc->mode == SAMPLE_CACHE_MODE_STREAM) ? 1U : 0U;
    desc->state = SAMPLE_CACHE_PLAYING;
    return 1U;
}

uint8_t sample_cache_start_voice(uint16_t sample_id, uint8_t voice_id)
{
    return sample_cache_start_voice_at(sample_id, voice_id, 0U);
}

uint8_t sample_cache_begin_read_block(uint8_t voice_id,
                                      uint32_t max_frames,
                                      sample_cache_block_t *out_block)
{
    if (out_block == 0)
    {
        return 0U;
    }

    (void)sample_cache_inspect_voice_block(voice_id, max_frames, out_block);
    return (voice_id < SAMPLE_CACHE_MAX_VOICES) ? 1U : 0U;
}

void sample_cache_commit_read_block(uint8_t voice_id, uint32_t consumed_frames)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        return;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if ((desc->cache == 0)
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return;
    }

    if (consumed_frames != 0U)
    {
        sample_cache_block_t block;
        const sample_cache_block_status_t status =
            sample_cache_inspect_voice_block(voice_id, consumed_frames, &block);
        if (status != SAMPLE_CACHE_BLOCK_OK)
        {
            sample_cache_finish_playback(desc,
                                         voice,
                                         (status == SAMPLE_CACHE_BLOCK_DONE)
                                             ? SAMPLE_CACHE_DONE
                                             : SAMPLE_CACHE_UNDERRUN);
            return;
        }

        if (consumed_frames > block.frames)
        {
            consumed_frames = block.frames;
        }
        voice->frame_pos += consumed_frames;
    }

    if (voice->frame_pos >= desc->total_frames)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
        return;
    }

    if (sample_cache_frame_available(desc, voice->frame_pos) == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return;
    }

    desc->state = SAMPLE_CACHE_PLAYING;
}

uint32_t sample_cache_read_voice(uint8_t voice_id, float *out_l, float *out_r, uint32_t frames)
{
    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (out_l == 0) || (out_r == 0) || (frames == 0U))
    {
        return 0U;
    }

    uint32_t produced = 0U;
    while (produced < frames)
    {
        sample_cache_block_t block;
        (void)sample_cache_begin_read_block(voice_id, frames - produced, &block);
        if ((block.status != SAMPLE_CACHE_BLOCK_OK) || (block.frames == 0U))
        {
            sample_cache_commit_read_block(voice_id, 0U);
            break;
        }

        const float *src_l = block.l;
        const float *src_r = (block.is_mono != 0U) ? block.l : block.r;
        for (uint32_t i = 0U; i < block.frames; ++i)
        {
            const float sample_l = src_l[i * block.frame_stride];
            const float sample_r = src_r[i * block.frame_stride];
            out_l[produced + i] += sample_l;
            out_r[produced + i] += sample_r;
        }

        sample_cache_commit_read_block(voice_id, block.frames);
        produced += block.frames;
    }

    return produced;
}

uint8_t sample_cache_read_voice_frame(uint8_t voice_id, uint32_t frame_index, float *out_l, float *out_r)
{
    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (out_l == 0) || (out_r == 0))
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active == 0U) || (voice->sample_id >= SAMPLE_POOL_SIZE))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
    if ((desc->cache == 0) || (desc->cache_valid_frames == 0U)
        || ((desc->state != SAMPLE_CACHE_READY_FULL)
            && (desc->state != SAMPLE_CACHE_READY_PARTIAL)
            && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        voice->active = 0U;
        desc->state = SAMPLE_CACHE_UNDERRUN;
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_DONE);
        return 0U;
    }

    if (sample_cache_frame_available(desc, frame_index) == 0U)
    {
        sample_cache_finish_playback(desc, voice, SAMPLE_CACHE_UNDERRUN);
        return 0U;
    }

    const uint32_t cache_index = sample_cache_frame_offset(desc, frame_index);
    *out_l = desc->cache[cache_index * 2U];
    *out_r = desc->cache[cache_index * 2U + 1U];
    voice->frame_pos = frame_index + 1U;
    return 1U;
}

const float *sample_cache_get_legacy_data(uint16_t sample_id, uint32_t *out_frames)
{
    if (out_frames != 0)
    {
        *out_frames = 0U;
    }

    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0;
    }

    const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->fully_cached == 0U) || (desc->cache == 0) || (desc->cache_valid_frames < desc->total_frames))
    {
        return 0;
    }

    if (out_frames != 0)
    {
        *out_frames = desc->total_frames;
    }
    return desc->cache;
}

void sample_cache_stop_voice(uint8_t voice_id)
{
    if (voice_id >= SAMPLE_CACHE_MAX_VOICES)
    {
        return;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    if ((voice->active != 0U) && (voice->sample_id < SAMPLE_POOL_SIZE))
    {
        sample_cache_desc_t *const desc = &g_sample_cache[voice->sample_id];
        const uint16_t sample_id = voice->sample_id;
        voice->active = 0U;
        voice->frame_pos = 0U;
        desc->stream_active = sample_cache_any_voice_active(sample_id);
        if ((desc->state == SAMPLE_CACHE_PLAYING) && (sample_cache_min_play_frame(sample_id) == UINT32_MAX))
        {
            sample_cache_close_stream_file(sample_id);
            if (desc->fully_cached != 0U)
            {
                desc->state = SAMPLE_CACHE_READY_FULL;
            }
            else
            {
                desc->state = SAMPLE_CACHE_NEEDS_REPREPARE;
                desc->reprepare_start_frame = 0U;
            }
        }
        return;
    }

    voice->active = 0U;
    voice->frame_pos = 0U;
}
