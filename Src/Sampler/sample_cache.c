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
#define SAMPLE_CACHE_REFILL_LOW_WATER_FRAMES (2048U)

SDRAM_SAMPLES static sample_cache_desc_t g_sample_cache[SAMPLE_POOL_SIZE];
SDRAM_SAMPLES static float g_sample_cache_data[SAMPLE_CACHE_SLOT_COUNT][SAMPLE_CACHE_SLOT_FRAMES * 2U];
static CTRL_STATE int16_t g_cache_slot_by_sample[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_cache_slot_in_use[SAMPLE_CACHE_SLOT_COUNT];
static CTRL_STATE sample_cache_voice_t g_sample_cache_voice[SAMPLE_CACHE_MAX_VOICES];
static DMA_BUFFER uint8_t g_sample_cache_io[SAMPLE_CACHE_IO_BYTES];
static FIL g_sample_cache_file[SAMPLE_POOL_SIZE];
static CTRL_STATE uint8_t g_sample_cache_file_open[SAMPLE_POOL_SIZE];

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

        UINT br = 0U;
        if ((f_read(fp, g_sample_cache_io, request, &br) != FR_OK) || (br == 0U))
        {
            return 0U;
        }

        const uint32_t valid_bytes = br - (br % desc->info.block_align);
        uint32_t pos = 0U;
        while ((pos + desc->info.block_align <= valid_bytes)
               && (remaining_frames != 0U)
               && (desc->cache_valid_frames < desc->cache_capacity_frames))
        {
            float left = 0.0f;
            float right = 0.0f;
            wav_audio_codec_decode_stereo_frame(&g_sample_cache_io[pos],
                                                desc->info.channels,
                                                desc->info.bits_per_sample,
                                                &left,
                                                &right);
            const uint32_t cache_pos = desc->cache_valid_frames;
            desc->cache[cache_pos * 2U] = left;
            desc->cache[cache_pos * 2U + 1U] = right;
            desc->cache_valid_frames++;
            desc->source_read_frame++;
            loaded_frames++;
            remaining_frames--;
            pos += desc->info.block_align;
        }

        if (valid_bytes == 0U)
        {
            break;
        }
    }

    if (out_loaded_frames != 0)
    {
        *out_loaded_frames = loaded_frames;
    }
    return (loaded_frames != 0U) ? 1U : 0U;
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
    if (f_open(&g_sample_cache_file[sample_id], desc->path, FA_READ) != FR_OK)
    {
        return 0U;
    }

    g_sample_cache_file_open[sample_id] = 1U;
    const FSIZE_t offset = (FSIZE_t)desc->data_offset
                         + ((FSIZE_t)desc->source_read_frame * (FSIZE_t)desc->info.block_align);
    if (f_lseek(&g_sample_cache_file[sample_id], offset) != FR_OK)
    {
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

    if (min_frame >= (desc->cache_window_start_frame + desc->cache_valid_frames))
    {
        desc->cache_window_start_frame = min_frame;
        desc->cache_valid_frames = 0U;
        return 1U;
    }

    const uint32_t drop_frames = min_frame - desc->cache_window_start_frame;
    const uint32_t keep_frames = desc->cache_valid_frames - drop_frames;
    memmove(desc->cache,
            &desc->cache[drop_frames * 2U],
            keep_frames * 2U * sizeof(float));
    desc->cache_window_start_frame = min_frame;
    desc->cache_valid_frames = keep_frames;
    return 1U;
}

static uint8_t sample_cache_refill_sample(uint16_t sample_id, uint32_t byte_budget)
{
    if ((sample_id >= SAMPLE_POOL_SIZE) || (byte_budget < g_sample_cache[sample_id].info.block_align))
    {
        return 0U;
    }

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    if ((desc->state != SAMPLE_CACHE_PLAYING)
        || (desc->mode != SAMPLE_CACHE_MODE_STREAM)
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

    return ((frame_index >= desc->cache_window_start_frame)
            && (frame_index < (desc->cache_window_start_frame + desc->cache_valid_frames)))
               ? 1U
               : 0U;
}

static uint8_t sample_cache_start_frame_available(uint16_t sample_id)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    return sample_cache_frame_available(&g_sample_cache[sample_id], 0U);
}

static uint16_t sample_cache_pick_refill_candidate(void)
{
    uint16_t best_sample = SAMPLE_POOL_SIZE;
    uint32_t best_available = UINT32_MAX;

    for (uint16_t sample_id = 0U; sample_id < SAMPLE_POOL_SIZE; ++sample_id)
    {
        const sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
        if ((desc->state != SAMPLE_CACHE_PLAYING)
            || (desc->mode != SAMPLE_CACHE_MODE_STREAM)
            || (desc->source_read_frame >= desc->total_frames))
        {
            continue;
        }

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

    return best_sample;
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
}

uint8_t sample_cache_prepare(uint16_t sample_id, const char *path)
{
    if (sample_id >= SAMPLE_POOL_SIZE)
    {
        return 0U;
    }

    sample_cache_release_slot(sample_id);
    sample_cache_clear_desc(&g_sample_cache[sample_id]);

    sample_cache_desc_t *const desc = &g_sample_cache[sample_id];
    desc->sample_id = sample_id;
    desc->state = SAMPLE_CACHE_PREPARING;

    if (sample_cache_trim_path_copy(desc->path, sizeof(desc->path), path) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 1U;
        return 0U;
    }

    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_SAMPLE_CACHE) == 0U)
    {
        desc->state = SAMPLE_CACHE_ERROR;
        desc->last_error = 2U;
        return 0U;
    }

    uint8_t ok = 0U;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        desc->last_error = 3U;
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
        goto done;
    }

    if (sample_cache_format_supported(&desc->info) == 0U)
    {
        desc->last_error = 6U;
        goto done;
    }

    desc->total_frames = desc->info.data_size / desc->info.block_align;
    desc->data_offset = desc->info.data_offset;
    desc->cache_capacity_frames = SAMPLE_CACHE_SLOT_FRAMES;
    if (desc->total_frames == 0U)
    {
        desc->last_error = 7U;
        goto done;
    }

    const int16_t slot = sample_cache_alloc_slot();
    if (slot < 0)
    {
        desc->last_error = 8U;
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

    if (f_lseek(&g_sample_cache_file[sample_id], desc->data_offset) != FR_OK)
    {
        sample_cache_release_slot(sample_id);
        desc->cache = 0;
        desc->last_error = 9U;
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
        desc->last_error = 9U;
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

uint8_t sample_cache_start_voice(uint16_t sample_id, uint8_t voice_id)
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

    if (sample_cache_start_frame_available(sample_id) == 0U)
    {
        return 0U;
    }

    sample_cache_voice_t *const voice = &g_sample_cache_voice[voice_id];
    voice->voice_id = voice_id;
    voice->sample_id = sample_id;
    voice->frame_pos = 0U;
    voice->active = 1U;
    voice->stop_on_underrun = 1U;
    desc->stream_active = (desc->mode == SAMPLE_CACHE_MODE_STREAM) ? 1U : 0U;
    desc->state = SAMPLE_CACHE_PLAYING;
    return 1U;
}

uint32_t sample_cache_read_voice(uint8_t voice_id, float *out_l, float *out_r, uint32_t frames)
{
    if ((voice_id >= SAMPLE_CACHE_MAX_VOICES) || (out_l == 0) || (out_r == 0) || (frames == 0U))
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
        || ((desc->state != SAMPLE_CACHE_READY_FULL) && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        voice->active = 0U;
        desc->state = SAMPLE_CACHE_UNDERRUN;
        return 0U;
    }

    uint32_t produced = 0U;
    while (produced < frames)
    {
        const uint32_t pos = voice->frame_pos;
        if (pos >= desc->total_frames)
        {
            voice->active = 0U;
            desc->state = (desc->fully_cached != 0U) ? SAMPLE_CACHE_READY_FULL : SAMPLE_CACHE_DONE;
            break;
        }

        if (sample_cache_frame_available(desc, pos) == 0U)
        {
            voice->active = 0U;
            desc->state = SAMPLE_CACHE_UNDERRUN;
            break;
        }

        const uint32_t cache_index = pos - desc->cache_window_start_frame;
        out_l[produced] += desc->cache[cache_index * 2U];
        out_r[produced] += desc->cache[cache_index * 2U + 1U];
        voice->frame_pos++;
        produced++;

        if (voice->frame_pos >= desc->total_frames)
        {
            voice->active = 0U;
            desc->state = (desc->fully_cached != 0U) ? SAMPLE_CACHE_READY_FULL : SAMPLE_CACHE_DONE;
            break;
        }
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
        || ((desc->state != SAMPLE_CACHE_READY_FULL) && (desc->state != SAMPLE_CACHE_PLAYING)))
    {
        voice->active = 0U;
        desc->state = SAMPLE_CACHE_UNDERRUN;
        return 0U;
    }

    if (frame_index >= desc->total_frames)
    {
        voice->active = 0U;
        desc->state = (desc->fully_cached != 0U) ? SAMPLE_CACHE_READY_FULL : SAMPLE_CACHE_DONE;
        return 0U;
    }

    if (sample_cache_frame_available(desc, frame_index) == 0U)
    {
        voice->active = 0U;
        desc->state = SAMPLE_CACHE_UNDERRUN;
        return 0U;
    }

    const uint32_t cache_index = frame_index - desc->cache_window_start_frame;
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
        if ((desc->state == SAMPLE_CACHE_PLAYING) && (sample_cache_min_play_frame(sample_id) == UINT32_MAX))
        {
            desc->stream_active = 0U;
            if (desc->fully_cached != 0U)
            {
                desc->state = SAMPLE_CACHE_READY_FULL;
            }
            else
            {
                desc->state = sample_cache_start_frame_available(sample_id) != 0U
                                  ? SAMPLE_CACHE_READY_PARTIAL
                                  : SAMPLE_CACHE_DONE;
            }
        }
        return;
    }

    voice->active = 0U;
    voice->frame_pos = 0U;
}
