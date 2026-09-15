#include "Storage/waveform_service.h"

#include "Storage/rec_source.h"
#include "Sampler/sample_page_cache.h"
#include "Storage/sample_capture.h"
#include "Storage/sd_access_gate.h"
#include "Storage/waveform_cache.h"
#include "SD/sd_scheduler_runtime.h"
#include "Platform/memory_layout.h"
#include "wav_parser.h"
#include "ff.h"

#include <string.h>

#define WAVEFORM_TILE_BINS 512U
#define WAVEFORM_TILE_COUNT 16U
#define WAVEFORM_READ_BYTES 4092U

typedef struct
{
    waveform_source_t source;
    uint32_t tile_index;
    uint32_t last_used;
    uint16_t first_bin;
    uint16_t ready_bins;
    uint8_t level;
    uint8_t valid;
    waveform_column_t bins[WAVEFORM_TILE_BINS];
} waveform_tile_t;

typedef struct
{
    waveform_source_t source;
    uint32_t tile_index;
    uint32_t next_frame;
    uint32_t data_offset;
    uint32_t media_epoch;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t channels;
    uint8_t level;
    uint8_t slot;
    uint8_t active;
    uint8_t format_ready;
} waveform_build_t;

STORAGE_STATE_SDRAM static waveform_tile_t g_waveform_tiles[WAVEFORM_TILE_COUNT];
STORAGE_STATE_SDRAM static waveform_build_t g_waveform_build;
RECORDER_SCRATCH_SDRAM static uint8_t g_waveform_read[WAVEFORM_READ_BYTES];
static uint32_t g_waveform_lru;
static waveform_source_t g_waveform_peak_source;
static uint16_t g_waveform_peak;
static uint8_t g_waveform_peak_valid;
/* GDB-visible request counters; classification is taken at the return path. */
typedef struct
{
    volatile uint32_t requests;
    volatile uint32_t invalid;
    volatile uint32_t pending_summary;
    volatile uint32_t ready_pcm;
    volatile uint32_t pcm_not_ready;
    volatile uint32_t ready_ideal_local;
    volatile uint32_t ready_ideal_sidecar;
    volatile uint32_t ready_fallback_local;
    volatile uint32_t ready_fallback_sidecar;
    volatile uint32_t ready_overview;
    volatile uint32_t ideal_local_missing;
    volatile uint32_t ideal_sidecar_missing;
    volatile uint32_t build_restarts;
    volatile uint32_t tile_evictions;
    volatile uint32_t last_frames_per_pixel;
    volatile uint32_t last_start_frame;
    volatile uint8_t last_ideal_level;
    volatile uint8_t last_display_level;
} waveform_latency_diag_t;
volatile waveform_latency_diag_t g_waveform_latency_diag;

static const uint32_t g_waveform_frames_per_bin[WAVEFORM_CACHE_LEVEL_COUNT] =
    { 16384U, 4096U, 1024U, 256U, 64U };

static uint8_t waveform_source_equal(const waveform_source_t *a,
                                     const waveform_source_t *b)
{
    return (uint8_t)(sample_audio_key_equal(&a->key, &b->key) != 0U
        && a->registration_epoch == b->registration_epoch
        && a->frame_count == b->frame_count);
}

static uint8_t waveform_source_current(const waveform_source_t *source,
                                       const char **out_path)
{
    rec_source_snapshot_t snapshot;
    const char *path;
    if(rec_source_current_path(&path, &snapshot) == 0U)
    {
        return 0U;
    }
    const waveform_source_t current =
        { snapshot.key, snapshot.registration_epoch, snapshot.frame_count };
    if(waveform_source_equal(source, &current) == 0U)
    {
        return 0U;
    }
    if(out_path != 0) { *out_path = path; }
    return 1U;
}

static waveform_tile_t *waveform_find_tile(const waveform_source_t *source,
                                           uint8_t level, uint32_t index)
{
    for(uint8_t i = 0U; i < WAVEFORM_TILE_COUNT; ++i)
    {
        waveform_tile_t *const tile = &g_waveform_tiles[i];
        if(tile->valid != 0U && tile->level == level
                && tile->tile_index == index
                && waveform_source_equal(&tile->source, source) != 0U)
        {
            tile->last_used = ++g_waveform_lru;
            return tile;
        }
    }
    return 0;
}

static uint8_t waveform_tile_range_ready(const waveform_source_t *source,
                                         uint8_t level, uint32_t frame0,
                                         uint32_t frame1)
{
    const uint32_t step = g_waveform_frames_per_bin[level];
    const uint32_t bin0 = frame0 / step;
    const uint32_t bin1 = (uint32_t)(((uint64_t)frame1 + step - 1U) / step);
    for(uint32_t bin = bin0; bin < bin1; )
    {
        waveform_tile_t *const tile = waveform_find_tile(source, level,
            bin / WAVEFORM_TILE_BINS);
        if(tile == 0 || bin % WAVEFORM_TILE_BINS < tile->first_bin
                || bin % WAVEFORM_TILE_BINS
                    >= (uint32_t)tile->first_bin + tile->ready_bins)
        {
            return 0U;
        }
        const uint32_t next = (bin / WAVEFORM_TILE_BINS) * WAVEFORM_TILE_BINS
            + tile->first_bin + tile->ready_bins;
        if(next <= bin) { return 0U; }
        bin = next;
    }
    return 1U;
}

static uint8_t waveform_tile_minmax(const waveform_source_t *source,
                                    uint8_t level, uint32_t frame0,
                                    uint32_t frame1, waveform_column_t *out)
{
    const uint32_t step = g_waveform_frames_per_bin[level];
    const uint32_t bin0 = frame0 / step;
    const uint32_t bin1 = (uint32_t)(((uint64_t)frame1 + step - 1U) / step);
    if(waveform_tile_range_ready(source, level, frame0, frame1) == 0U)
    {
        return 0U;
    }
    out->min = 32767;
    out->max = -32768;
    for(uint32_t bin = bin0; bin < bin1; ++bin)
    {
        waveform_tile_t *const tile = waveform_find_tile(source, level,
            bin / WAVEFORM_TILE_BINS);
        const waveform_column_t *const value =
            &tile->bins[bin % WAVEFORM_TILE_BINS];
        if(value->min < out->min) { out->min = value->min; }
        if(value->max > out->max) { out->max = value->max; }
    }
    return 1U;
}

static void waveform_start_tile(const waveform_source_t *source,
                                uint8_t level, uint32_t index,
                                uint32_t first_bin)
{
    if(g_waveform_build.active != 0U
            && g_waveform_build.level == level
            && g_waveform_build.tile_index == index
            && first_bin >= index * WAVEFORM_TILE_BINS
                + g_waveform_tiles[g_waveform_build.slot].first_bin
            && first_bin == g_waveform_build.next_frame
                / g_waveform_frames_per_bin[level]
            && waveform_source_equal(&g_waveform_build.source, source) != 0U)
    {
        return;
    }
    uint8_t slot = 0U;
    waveform_tile_t *const existing = waveform_find_tile(source, level, index);
    if(existing != 0)
    {
        slot = (uint8_t)(existing - g_waveform_tiles);
    }
    else
    {
    uint32_t oldest = 0xFFFFFFFFUL;
    for(uint8_t i = 0U; i < WAVEFORM_TILE_COUNT; ++i)
    {
        if(g_waveform_tiles[i].valid == 0U) { slot = i; break; }
        if(g_waveform_tiles[i].last_used < oldest)
        {
            oldest = g_waveform_tiles[i].last_used;
            slot = i;
        }
    }
    if(g_waveform_tiles[slot].valid != 0U) { ++g_waveform_latency_diag.tile_evictions; }
    }
    waveform_tile_t *const tile = &g_waveform_tiles[slot];
    const uint16_t first_offset = (uint16_t)(first_bin
        - index * WAVEFORM_TILE_BINS);
    const uint8_t resume = (uint8_t)(existing != 0
        && first_offset == (uint16_t)(tile->first_bin + tile->ready_bins));
    tile->valid = 1U;
    tile->source = *source;
    tile->level = level;
    tile->tile_index = index;
    if(resume == 0U)
    {
        tile->first_bin = first_offset;
        tile->ready_bins = 0U;
    }
    tile->last_used = ++g_waveform_lru;
    memset(&g_waveform_build, 0, sizeof(g_waveform_build));
    ++g_waveform_latency_diag.build_restarts;
    g_waveform_build.source = *source;
    g_waveform_build.level = level;
    g_waveform_build.tile_index = index;
    g_waveform_build.slot = slot;
    g_waveform_build.media_epoch = sd_access_media_epoch();
    g_waveform_build.next_frame = (uint32_t)((uint64_t)(resume != 0U
        ? index * WAVEFORM_TILE_BINS + tile->first_bin + tile->ready_bins
        : first_bin) * g_waveform_frames_per_bin[level]);
    g_waveform_build.active = 1U;
}

static int16_t waveform_decode_sample(const uint8_t *p, uint16_t bits)
{
    if(bits == 16U) { return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
    if(bits == 24U) { return (int16_t)((uint16_t)p[1] | ((uint16_t)p[2] << 8)); }
    return (int16_t)((uint16_t)p[2] | ((uint16_t)p[3] << 8));
}

void waveform_service_storage_service(void)
{
    waveform_build_t *const job = &g_waveform_build;
    const char *path;
    if(job->active == 0U) { return; }
    if(job->media_epoch != sd_access_media_epoch()
            || waveform_source_current(&job->source, &path) == 0U)
    {
        job->active = 0U;
        return;
    }
    const uint32_t step = g_waveform_frames_per_bin[job->level];
    const uint32_t tile_end = (uint32_t)((uint64_t)(job->tile_index + 1U)
        * WAVEFORM_TILE_BINS * step < job->source.frame_count
        ? (uint64_t)(job->tile_index + 1U) * WAVEFORM_TILE_BINS * step
        : job->source.frame_count);
    if(job->next_frame >= tile_end) { job->active = 0U; return; }
    const sd_scheduler_background_request_t admission =
        { WAVEFORM_READ_BYTES, job->media_epoch, SD_SCHEDULER_BACKGROUND_DATA };
    if(sd_scheduler_runtime_background_try_begin(&admission)
            != SD_SCHEDULER_BACKGROUND_GO) { return; }
    FIL fp;
    if(sd_access_fs_mount_if_needed() == 0U
            || f_open(&fp, path, FA_READ) != FR_OK)
    {
        sd_scheduler_runtime_background_end();
        return;
    }
    uint8_t ok = 1U;
    if(job->format_ready == 0U)
    {
        wav_info_t info;
        if(wav_parser_parse_info(&fp, &info) == 0
                || info.audio_format != 1U
                || (info.bits_per_sample != 16U
                    && info.bits_per_sample != 24U
                    && info.bits_per_sample != 32U)
                || (info.channels != 1U && info.channels != 2U)
                || info.block_align == 0U
                || info.block_align > 8U)
        {
            ok = 0U;
        }
        else
        {
            job->data_offset = info.data_offset;
            job->block_align = info.block_align;
            job->bits_per_sample = info.bits_per_sample;
            job->channels = info.channels;
            job->format_ready = 1U;
        }
    }
    if(ok != 0U)
    {
        uint32_t frames = tile_end - job->next_frame;
        const uint32_t max_frames = WAVEFORM_READ_BYTES / job->block_align;
        if(frames > max_frames) { frames = max_frames; }
        const uint32_t bytes = frames * job->block_align;
        UINT read = 0U;
        if(f_lseek(&fp, job->data_offset
                + job->next_frame * job->block_align) != FR_OK
                || f_read(&fp, g_waveform_read, bytes, &read) != FR_OK
                || read != bytes)
        {
            ok = 0U;
        }
        else
        {
            waveform_tile_t *const tile = &g_waveform_tiles[job->slot];
            const uint16_t sample_bytes = job->bits_per_sample / 8U;
            for(uint32_t f = 0U; f < frames; ++f)
            {
                const uint32_t absolute = job->next_frame + f;
                const uint32_t bin = absolute / step
                    - job->tile_index * WAVEFORM_TILE_BINS;
                const uint8_t *const pcm = &g_waveform_read[f * job->block_align];
                const int16_t left = waveform_decode_sample(pcm, job->bits_per_sample);
                const int16_t right = (job->channels == 2U)
                    ? waveform_decode_sample(pcm + sample_bytes, job->bits_per_sample)
                    : left;
                waveform_column_t *const value = &tile->bins[bin];
                if(absolute % step == 0U)
                {
                    value->min = (left < right) ? left : right;
                    value->max = (left > right) ? left : right;
                }
                else
                {
                    if(left < value->min) { value->min = left; }
                    if(right < value->min) { value->min = right; }
                    if(left > value->max) { value->max = left; }
                    if(right > value->max) { value->max = right; }
                }
            }
            job->next_frame += frames;
            tile->ready_bins = (uint16_t)(job->next_frame / step
                - job->tile_index * WAVEFORM_TILE_BINS
                - tile->first_bin);
            if(job->next_frame == job->source.frame_count
                    && (job->next_frame % step) != 0U)
            {
                tile->ready_bins++;
            }
            if(job->next_frame >= tile_end) { job->active = 0U; }
        }
    }
    (void)f_close(&fp);
    sd_scheduler_runtime_background_end();
    if(ok == 0U) { job->active = 0U; }
}

static const rec_source_waveform_summary_t *waveform_rec_summary(
    const waveform_source_t *source)
{
    rec_source_snapshot_t current;
    if((source == 0) || (rec_source_current_snapshot(&current) == 0U)
            || (sample_audio_key_equal(&source->key, &current.key) == 0U)
            || (source->registration_epoch != current.registration_epoch)
            || (source->frame_count != current.frame_count))
    {
        return 0;
    }
    const rec_source_waveform_summary_t *const summary =
        rec_source_current_waveform();
    if((summary == 0) || (summary->ready == 0U)
            || (summary->generation != source->key.generation)
            || (summary->frame_count != source->frame_count)
            || (summary->bin_count == 0U))
    {
        return 0;
    }
    return summary;
}

static uint8_t waveform_sidecar_range(const waveform_cache_handle_t *handle,
                                      uint8_t level, uint32_t start,
                                      uint32_t count, uint8_t request)
{
    const uint32_t step = g_waveform_frames_per_bin[level];
    const uint32_t first = start / step / WAVEFORM_TILE_BINS;
    const uint32_t last_bin = (uint32_t)(((uint64_t)start + count + step - 1U)
        / step);
    const uint32_t last = (last_bin + WAVEFORM_TILE_BINS - 1U)
        / WAVEFORM_TILE_BINS;
    if(request != 0U)
    {
        (void)waveform_cache_request_tiles(handle,
            (waveform_cache_level_id_t)level, first, last - first,
            WAVEFORM_CACHE_REASON_EDITOR_VISIBLE);
    }
    return waveform_cache_tiles_ready(handle,
        (waveform_cache_level_id_t)level, first, last - first);
}

static uint8_t waveform_local_range(const waveform_source_t *source,
                                    uint8_t level, uint32_t start,
                                    uint32_t count)
{
    return waveform_tile_range_ready(source, level, start, start + count);
}

static uint8_t waveform_render_level(const waveform_source_t *source,
                                     const waveform_cache_handle_t *sidecar,
                                     uint8_t use_sidecar, uint8_t level,
                                     uint32_t start, uint32_t count,
                                     uint8_t width, waveform_column_t *columns)
{
    uint64_t cursor = start;
    uint32_t remainder = 0U;
    const uint32_t frame_step = count / width;
    const uint32_t frame_remainder = count % width;
    const uint32_t step = g_waveform_frames_per_bin[level];
    for(uint8_t col = 0U; col < width; ++col)
    {
        const uint32_t frame0 = (uint32_t)cursor;
        cursor += frame_step;
        remainder += frame_remainder;
        if(remainder >= width) { remainder -= width; cursor++; }
        uint32_t frame1 = (uint32_t)cursor;
        if(frame1 <= frame0) { frame1 = frame0 + 1U; }
        if(frame1 > source->frame_count) { frame1 = source->frame_count; }
        if(use_sidecar != 0U)
        {
            const uint32_t bin0 = frame0 / step;
            const uint32_t bin1 = (uint32_t)(((uint64_t)frame1 + step - 1U)
                / step);
            if(waveform_cache_minmax_from_ram(sidecar,
                (waveform_cache_level_id_t)level, bin0, bin1 - bin0,
                &columns[col].min, &columns[col].max) == 0U)
            {
                return 0U;
            }
        }
        else if(waveform_tile_minmax(source, level, frame0,
                                    frame1, &columns[col]) == 0U)
        {
            return 0U;
        }
    }
    return 1U;
}

static int16_t waveform_pcm_to_i16(float value)
{
    if(value >= 1.0f) { return 32767; }
    if(value <= -1.0f) { return -32768; }
    return (int16_t)(value * 32768.0f);
}

static uint8_t waveform_pcm_render(const waveform_source_t *source,
                                   uint32_t start, uint32_t count,
                                   uint8_t width, waveform_column_t *columns)
{
    sample_page_span_t span;
    uint32_t loaded_page = UINT32_MAX;
    uint64_t cursor = start;
    uint32_t remainder = 0U;
    const uint32_t frame_step = count / width;
    const uint32_t frame_remainder = count % width;
    for(uint8_t col = 0U; col < width; ++col)
    {
        const uint32_t frame0 = (uint32_t)cursor;
        cursor += frame_step;
        remainder += frame_remainder;
        if(remainder >= width) { remainder -= width; cursor++; }
        uint32_t frame1 = (uint32_t)cursor;
        if(frame1 <= frame0) { frame1 = frame0 + 1U; }
        if(frame1 > source->frame_count) { frame1 = source->frame_count; }
        int16_t min = 32767;
        int16_t max = -32768;
        for(uint32_t frame = frame0; frame < frame1; ++frame)
        {
            const uint32_t page = frame / SAMPLE_PAGE_FRAMES;
            if(page != loaded_page)
            {
                if(sample_page_cache_control_resolve_page_key(source->key,
                    source->registration_epoch, page, &span) == 0U)
                {
                    return 0U;
                }
                loaded_page = page;
            }
            if(frame < span.start_frame
                    || frame - span.start_frame >= span.frame_count
                    || span.stride_floats != 2U)
            {
                return 0U;
            }
            const uint32_t offset = (frame - span.start_frame) * 2U;
            const int16_t left = waveform_pcm_to_i16(
                span.frames_interleaved[offset]);
            const int16_t right = waveform_pcm_to_i16(
                span.frames_interleaved[offset + 1U]);
            if(left < min) { min = left; }
            if(right < min) { min = right; }
            if(left > max) { max = left; }
            if(right > max) { max = right; }
        }
        columns[col].min = min;
        columns[col].max = max;
    }
    return 1U;
}

static uint8_t waveform_pcm_request(const waveform_source_t *source,
                                    uint32_t start, uint32_t count,
                                    uint8_t width, waveform_column_t *columns)
{
    sample_page_stream_info_t info;
    if(sample_page_cache_get_stream_info_key(source->key, &info) == 0U
            || info.registration_epoch != source->registration_epoch
            || info.total_frames < source->frame_count
            || info.format != SAMPLE_AUDIO_FORMAT_FLOAT32_STEREO_INTERLEAVED
            || info.frames_per_page != SAMPLE_PAGE_FRAMES
            || info.stride_floats != 2U)
    {
        return 0U;
    }
    const uint32_t first = start / SAMPLE_PAGE_FRAMES;
    const uint32_t last = (start + count - 1U) / SAMPLE_PAGE_FRAMES;
    for(uint32_t page = first; page <= last; ++page)
    {
        (void)sample_page_cache_reserve_page_key(source->key, page);
    }
    const uint8_t ready = waveform_pcm_render(source, start,
                                               count, width, columns);
    if(first > 0U)
    {
        (void)sample_page_cache_reserve_page_key(source->key, first - 1U);
    }
    if((uint64_t)(last + 1U) * SAMPLE_PAGE_FRAMES
            < source->frame_count)
    {
        (void)sample_page_cache_reserve_page_key(source->key, last + 1U);
    }
    return ready;
}

uint8_t waveform_rec_current_source(waveform_source_t *out_source)
{
    rec_source_snapshot_t current;
    if((out_source == 0) || (rec_source_current_snapshot(&current) == 0U)
            || (current.key.domain != SAMPLE_AUDIO_DOMAIN_REC)
            || (current.key.generation == 0U)
            || (current.registration_epoch == 0U)
            || (current.frame_count == 0U))
    {
        return 0U;
    }
    out_source->key = current.key;
    out_source->registration_epoch = current.registration_epoch;
    out_source->frame_count = current.frame_count;
    return (waveform_rec_summary(out_source) != 0) ? 1U : 0U;
}

uint16_t waveform_rec_peak(const waveform_source_t *source)
{
    const rec_source_waveform_summary_t *const summary = waveform_rec_summary(source);
    if(summary == 0) { return 0U; }
    if(g_waveform_peak_valid != 0U
            && waveform_source_equal(&g_waveform_peak_source, source) != 0U)
    {
        return g_waveform_peak;
    }
    uint16_t peak = 0U;
    for(uint16_t bin = 0U; bin < summary->bin_count; ++bin)
    {
        const int16_t min = summary->min[bin];
        const int16_t max = summary->max[bin];
        const uint16_t amin = (min == -32768) ? 32768U
            : (uint16_t)((min < 0) ? -min : min);
        const uint16_t amax = (max == -32768) ? 32768U
            : (uint16_t)((max < 0) ? -max : max);
        if(amin > peak) { peak = amin; }
        if(amax > peak) { peak = amax; }
    }
    g_waveform_peak_source = *source;
    g_waveform_peak = peak;
    g_waveform_peak_valid = 1U;
    return peak;
}

waveform_result_t waveform_request(const waveform_source_t *source,
                                   uint32_t start_frame,
                                   uint32_t frame_count,
                                   uint8_t pixel_width,
                                   waveform_column_t *columns)
{
    ++g_waveform_latency_diag.requests;
    if((source == 0) || (columns == 0) || (pixel_width == 0U)
            || (frame_count == 0U) || (start_frame >= source->frame_count)
            || (frame_count > (source->frame_count - start_frame)))
    {
        ++g_waveform_latency_diag.invalid;
        return WAVEFORM_RESULT_INVALID;
    }
    const rec_source_waveform_summary_t *const summary = waveform_rec_summary(source);
    if(summary == 0)
    {
        ++g_waveform_latency_diag.pending_summary;
        return WAVEFORM_RESULT_PENDING;
    }
    /* A visible window no larger than one PCM page can straddle two pages;
       the shared loader owns misses and the existing audio contracts. */
    const uint8_t pcm_scale = (uint8_t)(frame_count <= SAMPLE_PAGE_FRAMES);
    if(pcm_scale != 0U
            && waveform_pcm_request(source, start_frame, frame_count,
                pixel_width, columns) != 0U)
    {
        ++g_waveform_latency_diag.ready_pcm;
        return WAVEFORM_RESULT_READY;
    }
    if(pcm_scale != 0U) { ++g_waveform_latency_diag.pcm_not_ready; }

    const uint32_t frames_per_pixel = (uint32_t)(((uint64_t)frame_count
        + pixel_width - 1U) / pixel_width);
    uint8_t ideal = WAVEFORM_CACHE_LEVEL_L4_VERY_FINE;
    for(uint8_t level = WAVEFORM_CACHE_LEVEL_L0_COARSE;
            level < WAVEFORM_CACHE_LEVEL_COUNT; ++level)
    {
        if(g_waveform_frames_per_bin[level] <= frames_per_pixel)
        {
            ideal = level;
            break;
        }
    }
    g_waveform_latency_diag.last_frames_per_pixel = frames_per_pixel;
    g_waveform_latency_diag.last_start_frame = start_frame;
    g_waveform_latency_diag.last_ideal_level = ideal;
    g_waveform_latency_diag.last_display_level = 0xFFU;
    waveform_cache_handle_t sidecar;
    const uint8_t have_sidecar =
        sample_capture_model_waveform_cache_get_handle(&sidecar);
    if(waveform_local_range(source, ideal, start_frame, frame_count) != 0U
            && waveform_render_level(source, 0, 0U, ideal, start_frame,
                frame_count, pixel_width, columns) != 0U)
    {
        ++g_waveform_latency_diag.ready_ideal_local;
        g_waveform_latency_diag.last_display_level = ideal;
        return WAVEFORM_RESULT_READY;
    }
    ++g_waveform_latency_diag.ideal_local_missing;
    if(pcm_scale == 0U && have_sidecar != 0U
            && waveform_sidecar_range(&sidecar, ideal, start_frame,
                frame_count, 1U) != 0U
            && waveform_render_level(source, &sidecar, 1U, ideal,
                start_frame, frame_count, pixel_width, columns) != 0U)
    {
        ++g_waveform_latency_diag.ready_ideal_sidecar;
        g_waveform_latency_diag.last_display_level = ideal;
        return WAVEFORM_RESULT_READY;
    }
    if(pcm_scale == 0U && have_sidecar != 0U)
        ++g_waveform_latency_diag.ideal_sidecar_missing;
    /* One active build uses the existing BG admission. Published bins are
       reusable immediately; a new viewport abandons obsolete work. */
    const uint32_t step = g_waveform_frames_per_bin[ideal];
    const uint32_t first_bin = start_frame / step;
    const uint32_t last_bin = (uint32_t)(((uint64_t)start_frame
        + frame_count + step - 1U) / step);
    for(uint32_t bin = first_bin; pcm_scale == 0U && bin < last_bin; )
    {
        const uint32_t tile_index = bin / WAVEFORM_TILE_BINS;
        waveform_tile_t *const tile = waveform_find_tile(source, ideal, tile_index);
        if(tile == 0 || bin % WAVEFORM_TILE_BINS < tile->first_bin
                || bin % WAVEFORM_TILE_BINS
                    >= (uint32_t)tile->first_bin + tile->ready_bins)
        {
            waveform_start_tile(source, ideal, tile_index, bin);
            break;
        }
        const uint32_t next = tile_index * WAVEFORM_TILE_BINS
            + tile->first_bin + tile->ready_bins;
        if(next <= bin) { break; }
        bin = next;
    }
    for(int level = (int)ideal - 1; level >= 0; --level)
    {
        if(waveform_local_range(source, (uint8_t)level,
                start_frame, frame_count) != 0U
                && waveform_render_level(source, 0, 0U, (uint8_t)level,
                    start_frame, frame_count, pixel_width, columns) != 0U)
        {
            ++g_waveform_latency_diag.ready_fallback_local;
            g_waveform_latency_diag.last_display_level = (uint8_t)level;
            return WAVEFORM_RESULT_READY;
        }
        if(have_sidecar != 0U
                && waveform_sidecar_range(&sidecar, (uint8_t)level,
                    start_frame, frame_count, 0U) != 0U
                && waveform_render_level(source, &sidecar, 1U,
                    (uint8_t)level, start_frame, frame_count,
                    pixel_width, columns) != 0U)
        {
            ++g_waveform_latency_diag.ready_fallback_sidecar;
            g_waveform_latency_diag.last_display_level = (uint8_t)level;
            return WAVEFORM_RESULT_READY;
        }
    }

    const uint32_t frame_step = frame_count / pixel_width;
    const uint32_t frame_remainder = frame_count % pixel_width;
    uint64_t cursor = start_frame;
    uint32_t remainder = 0U;
    for(uint8_t col = 0U; col < pixel_width; ++col)
    {
        const uint32_t frame0 = (uint32_t)cursor;
        cursor += frame_step;
        remainder += frame_remainder;
        if(remainder >= pixel_width)
        {
            remainder -= pixel_width;
            cursor++;
        }
        uint32_t frame1 = (uint32_t)cursor;
        if(frame1 <= frame0) { frame1 = frame0 + 1U; }
        if(frame1 > source->frame_count) { frame1 = source->frame_count; }

        uint32_t bin0;
        uint32_t bin1;
        if(summary->frames_per_bin != 0U)
        {
            bin0 = frame0 / summary->frames_per_bin;
            bin1 = (uint32_t)(((uint64_t)frame1
                + summary->frames_per_bin - 1ULL) / summary->frames_per_bin);
        }
        else
        {
            const uint32_t domain = (summary->bin_domain_frames != 0U)
                ? summary->bin_domain_frames : summary->frame_count;
            bin0 = (uint32_t)(((uint64_t)frame0 * summary->bin_count) / domain);
            bin1 = (uint32_t)((((uint64_t)frame1 * summary->bin_count)
                + domain - 1ULL) / domain);
        }
        if(bin0 >= summary->bin_count) { bin0 = summary->bin_count - 1U; }
        if(bin1 <= bin0) { bin1 = bin0 + 1U; }
        if(bin1 > summary->bin_count) { bin1 = summary->bin_count; }

        int16_t min = summary->min[bin0];
        int16_t max = summary->max[bin0];
        for(uint32_t bin = bin0 + 1U; bin < bin1; ++bin)
        {
            if(summary->min[bin] < min) { min = summary->min[bin]; }
            if(summary->max[bin] > max) { max = summary->max[bin]; }
        }
        columns[col].min = min;
        columns[col].max = max;
    }
    ++g_waveform_latency_diag.ready_overview;
    return WAVEFORM_RESULT_READY;
}
