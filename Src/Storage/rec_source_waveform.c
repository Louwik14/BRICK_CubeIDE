#include "Storage/rec_source_waveform.h"

#include <string.h>

#include "Platform/memory_layout.h"

#define REC_SOURCE_WAVEFORM_SERVICE_FRAMES 4096U
#define REC_SOURCE_FREE_FRAME_LIMIT ((UINT32_MAX - 44U) / 6U)

typedef struct
{
    rec_source_waveform_summary_t levels[2];
    uint32_t frame_limit;
    uint32_t captured_frames;
    uint32_t compact_cursor;
    uint32_t compact_start_frame;
    uint32_t clear_cursor;
    uint8_t active_level;
    uint8_t compacting;
    uint8_t fixed_length;
    uint8_t active;
} rec_source_waveform_capture_t;

STORAGE_STATE_SDRAM static rec_source_waveform_capture_t g_rec_source_waveform;

static void clear_level(rec_source_waveform_summary_t *level)
{
    memset(level, 0, sizeof(*level));
    for (uint32_t i = 0U; i < REC_SOURCE_WAVEFORM_BINS; ++i)
    {
        level->min[i] = 32767;
        level->max[i] = -32768;
    }
}

static void accumulate(rec_source_waveform_summary_t *level, uint32_t bin,
                       int16_t min, int16_t max)
{
    if (bin >= REC_SOURCE_WAVEFORM_BINS) return;
    if (min < level->min[bin]) level->min[bin] = min;
    if (max > level->max[bin]) level->max[bin] = max;
}

static void compact_one(void)
{
    rec_source_waveform_summary_t *const src =
        &g_rec_source_waveform.levels[g_rec_source_waveform.active_level];
    rec_source_waveform_summary_t *const dst =
        &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
    const uint32_t i = g_rec_source_waveform.compact_cursor;
    if (i >= (REC_SOURCE_WAVEFORM_BINS / 2U)) return;
    const int16_t min = (src->min[i * 2U] < src->min[i * 2U + 1U])
        ? src->min[i * 2U] : src->min[i * 2U + 1U];
    const int16_t max = (src->max[i * 2U] > src->max[i * 2U + 1U])
        ? src->max[i * 2U] : src->max[i * 2U + 1U];
    accumulate(dst, i, min, max);
    g_rec_source_waveform.compact_cursor++;
}

static void capture_frame(const int32_t *lr, uint32_t frame)
{
    const int16_t l = (int16_t)(lr[0] >> 8U);
    const int16_t r = (int16_t)(lr[1] >> 8U);
    const int16_t sample_min = (l < r) ? l : r;
    const int16_t sample_max = (l > r) ? l : r;
    if (g_rec_source_waveform.fixed_length != 0U)
    {
        const uint32_t bins = (g_rec_source_waveform.frame_limit
                < REC_SOURCE_WAVEFORM_BINS)
            ? g_rec_source_waveform.frame_limit : REC_SOURCE_WAVEFORM_BINS;
        uint32_t bin = (uint32_t)(((uint64_t)frame * bins)
                                  / g_rec_source_waveform.frame_limit);
        if (bin >= bins) bin = bins - 1U;
        accumulate(&g_rec_source_waveform.levels[0], bin,
                   sample_min, sample_max);
        return;
    }

    rec_source_waveform_summary_t *active =
        &g_rec_source_waveform.levels[g_rec_source_waveform.active_level];
    const uint32_t frames_per_bin = active->frames_per_bin;
    if ((g_rec_source_waveform.compacting == 0U)
            && ((frame / frames_per_bin) >= REC_SOURCE_WAVEFORM_BINS))
    {
        g_rec_source_waveform.compacting = 1U;
        g_rec_source_waveform.compact_cursor = 0U;
        g_rec_source_waveform.compact_start_frame = frame;
    }
    if (g_rec_source_waveform.compacting != 0U)
    {
        compact_one();
        rec_source_waveform_summary_t *const dst =
            &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
        const uint32_t next_fpb = frames_per_bin * 2U;
        const uint32_t bin = (REC_SOURCE_WAVEFORM_BINS / 2U)
            + ((frame - g_rec_source_waveform.compact_start_frame) / next_fpb);
        accumulate(dst, bin, sample_min, sample_max);
        if (g_rec_source_waveform.compact_cursor
                >= (REC_SOURCE_WAVEFORM_BINS / 2U))
        {
            g_rec_source_waveform.active_level ^= 1U;
            g_rec_source_waveform.levels[g_rec_source_waveform.active_level]
                .frames_per_bin = next_fpb;
            g_rec_source_waveform.compacting = 0U;
            g_rec_source_waveform.clear_cursor = 0U;
        }
    }
    else
    {
        accumulate(active, frame / frames_per_bin, sample_min, sample_max);
        rec_source_waveform_summary_t *const spare =
            &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
        for (uint8_t n = 0U; (n < 2U)
                && (g_rec_source_waveform.clear_cursor
                    < REC_SOURCE_WAVEFORM_BINS); ++n)
        {
            spare->min[g_rec_source_waveform.clear_cursor] = 32767;
            spare->max[g_rec_source_waveform.clear_cursor] = -32768;
            g_rec_source_waveform.clear_cursor++;
        }
    }
}

void rec_source_waveform_begin(uint32_t frame_limit)
{
    memset(&g_rec_source_waveform, 0, sizeof(g_rec_source_waveform));
    clear_level(&g_rec_source_waveform.levels[0]);
    clear_level(&g_rec_source_waveform.levels[1]);
    g_rec_source_waveform.levels[0].frames_per_bin = 1U;
    g_rec_source_waveform.levels[1].frames_per_bin = 2U;
    g_rec_source_waveform.frame_limit = frame_limit;
    g_rec_source_waveform.fixed_length =
        (frame_limit < REC_SOURCE_FREE_FRAME_LIMIT) ? 1U : 0U;
    g_rec_source_waveform.active = 1U;
}

void rec_source_waveform_abort(void)
{
    g_rec_source_waveform.active = 0U;
}

void rec_source_waveform_capture_service(const int32_t *ring_interleaved,
                                         uint32_t ring_capacity_frames,
                                         uint32_t published_frames)
{
    if ((g_rec_source_waveform.active == 0U) || (ring_interleaved == NULL)
            || (ring_capacity_frames == 0U)) return;
    if (published_frames <= g_rec_source_waveform.captured_frames) return;
    uint32_t limit = published_frames;
    if ((limit - g_rec_source_waveform.captured_frames)
            > REC_SOURCE_WAVEFORM_SERVICE_FRAMES)
        limit = g_rec_source_waveform.captured_frames
            + REC_SOURCE_WAVEFORM_SERVICE_FRAMES;
    while (g_rec_source_waveform.captured_frames < limit)
    {
        const uint32_t frame = g_rec_source_waveform.captured_frames;
        const uint32_t ring_frame = frame % ring_capacity_frames;
        capture_frame(&ring_interleaved[ring_frame * 2U], frame);
        g_rec_source_waveform.captured_frames++;
    }
}

uint32_t rec_source_waveform_captured_frames(void)
{
    return g_rec_source_waveform.captured_frames;
}

uint8_t rec_source_waveform_finish(rec_source_waveform_summary_t *out_summary)
{
    if ((out_summary == NULL) || (g_rec_source_waveform.active == 0U)) return 0U;
    while (g_rec_source_waveform.compacting != 0U)
    {
        compact_one();
        if (g_rec_source_waveform.compact_cursor
                >= (REC_SOURCE_WAVEFORM_BINS / 2U))
        {
            const uint8_t next = g_rec_source_waveform.active_level ^ 1U;
            g_rec_source_waveform.levels[next].frames_per_bin =
                g_rec_source_waveform.levels[g_rec_source_waveform.active_level]
                    .frames_per_bin * 2U;
            g_rec_source_waveform.active_level = next;
            g_rec_source_waveform.compacting = 0U;
        }
    }
    const uint8_t level = (g_rec_source_waveform.fixed_length != 0U)
        ? 0U : g_rec_source_waveform.active_level;
    *out_summary = g_rec_source_waveform.levels[level];
    out_summary->frame_count = g_rec_source_waveform.captured_frames;
    if (g_rec_source_waveform.fixed_length != 0U)
    {
        out_summary->frames_per_bin = 0U;
        out_summary->bin_domain_frames = g_rec_source_waveform.frame_limit;
        out_summary->bin_count = (g_rec_source_waveform.frame_limit
                < REC_SOURCE_WAVEFORM_BINS)
            ? (uint16_t)g_rec_source_waveform.frame_limit
            : REC_SOURCE_WAVEFORM_BINS;
    }
    else
    {
        out_summary->bin_domain_frames = out_summary->frame_count;
        uint32_t bins = (out_summary->frame_count + out_summary->frames_per_bin - 1U)
            / out_summary->frames_per_bin;
        if (bins > REC_SOURCE_WAVEFORM_BINS) bins = REC_SOURCE_WAVEFORM_BINS;
        out_summary->bin_count = (uint16_t)bins;
    }
    for (uint16_t i = 0U; i < out_summary->bin_count; ++i)
        if ((out_summary->min[i] == 32767) && (out_summary->max[i] == -32768))
        {
            out_summary->min[i] = 0;
            out_summary->max[i] = 0;
        }
    out_summary->ready = (out_summary->bin_count != 0U) ? 1U : 0U;
    g_rec_source_waveform.active = 0U;
    return out_summary->ready;
}
