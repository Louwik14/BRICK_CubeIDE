#include "Storage/rec_source_waveform.h"

#include <string.h>

#include "Platform/memory_layout.h"

typedef struct
{
    uint32_t frames_per_bin;
    int16_t min[REC_SOURCE_WAVEFORM_BINS];
    int16_t max[REC_SOURCE_WAVEFORM_BINS];
    int16_t first[REC_SOURCE_WAVEFORM_BINS];
    int16_t last[REC_SOURCE_WAVEFORM_BINS];
} rec_source_waveform_build_level_t;

_Static_assert(sizeof(rec_source_waveform_build_level_t) == 32772U,
    "REC_SOURCE waveform build-level SDRAM budget");

typedef struct
{
    rec_source_waveform_build_level_t levels[2];
    uint32_t captured_frames;
    uint32_t compact_cursor;
    uint32_t compact_start_frame;
    uint32_t clear_cursor;
    rec_source_waveform_column_t live[REC_SOURCE_WAVEFORM_BINS];
    uint32_t live_block[REC_SOURCE_WAVEFORM_BINS];
    uint8_t active_level;
    uint8_t compacting;
    uint8_t active;
} rec_source_waveform_capture_t;

STORAGE_STATE_SDRAM static rec_source_waveform_capture_t g_rec_source_waveform;

static void clear_level(rec_source_waveform_build_level_t *level)
{
    memset(level, 0, sizeof(*level));
    for (uint32_t i = 0U; i < REC_SOURCE_WAVEFORM_BINS; ++i)
    {
        level->min[i] = 32767;
        level->max[i] = -32768;
    }
}

static void accumulate(rec_source_waveform_build_level_t *level, uint32_t bin,
                       int16_t min, int16_t max, int16_t first, int16_t last)
{
    if (bin >= REC_SOURCE_WAVEFORM_BINS) return;
    const uint8_t empty = (uint8_t)((level->min[bin] == 32767)
        && (level->max[bin] == -32768));
    if (min < level->min[bin]) level->min[bin] = min;
    if (max > level->max[bin]) level->max[bin] = max;
    if (empty != 0U) level->first[bin] = first;
    level->last[bin] = last;
}

static void compact_one(void)
{
    rec_source_waveform_build_level_t *const src =
        &g_rec_source_waveform.levels[g_rec_source_waveform.active_level];
    rec_source_waveform_build_level_t *const dst =
        &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
    const uint32_t i = g_rec_source_waveform.compact_cursor;
    if (i >= (REC_SOURCE_WAVEFORM_BINS / 2U)) return;
    const int16_t min = (src->min[i * 2U] < src->min[i * 2U + 1U])
        ? src->min[i * 2U] : src->min[i * 2U + 1U];
    const int16_t max = (src->max[i * 2U] > src->max[i * 2U + 1U])
        ? src->max[i * 2U] : src->max[i * 2U + 1U];
    const uint32_t left = i * 2U;
    const uint32_t right = left + 1U;
    const uint8_t right_seen = (uint8_t)((src->min[right] != 32767)
        || (src->max[right] != -32768));
    accumulate(dst, i, min, max, src->first[left],
        (right_seen != 0U) ? src->last[right] : src->last[left]);
    g_rec_source_waveform.compact_cursor++;
}

static int16_t float_to_i16(float value)
{
    if (value != value) value = 0.0f;
    if (value > 0.999969f) value = 0.999969f;
    else if (value < -1.0f) value = -1.0f;
    return (int16_t)(value * 32767.0f);
}

static void capture_frame(const float *lr, uint32_t frame)
{
    const int16_t l = float_to_i16(lr[0]);
    const int16_t r = float_to_i16(lr[1]);
    const int16_t sample_min = (l < r) ? l : r;
    const int16_t sample_max = (l > r) ? l : r;
    const int16_t mono = (int16_t)(((int32_t)l + r) / 2);
    const uint32_t live_block = frame / REC_SOURCE_WAVEFORM_LIVE_FRAMES_PER_BIN;
    const uint32_t live_bin = live_block % REC_SOURCE_WAVEFORM_BINS;
    rec_source_waveform_column_t *const live =
        &g_rec_source_waveform.live[live_bin];
    if (g_rec_source_waveform.live_block[live_bin] != live_block + 1U)
    {
        *live = (rec_source_waveform_column_t){ sample_min, sample_max,
            mono, mono };
        g_rec_source_waveform.live_block[live_bin] = live_block + 1U;
    }
    else
    {
        if (sample_min < live->min) live->min = sample_min;
        if (sample_max > live->max) live->max = sample_max;
        live->last = mono;
    }
    rec_source_waveform_build_level_t *active =
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
        rec_source_waveform_build_level_t *const dst =
            &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
        const uint32_t next_fpb = frames_per_bin * 2U;
        const uint32_t bin = (REC_SOURCE_WAVEFORM_BINS / 2U)
            + ((frame - g_rec_source_waveform.compact_start_frame) / next_fpb);
        accumulate(dst, bin, sample_min, sample_max, mono, mono);
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
        accumulate(active, frame / frames_per_bin, sample_min, sample_max,
            mono, mono);
        rec_source_waveform_build_level_t *const spare =
            &g_rec_source_waveform.levels[g_rec_source_waveform.active_level ^ 1U];
        for (uint8_t n = 0U; (n < 2U)
                && (g_rec_source_waveform.clear_cursor
                    < REC_SOURCE_WAVEFORM_BINS); ++n)
        {
            spare->min[g_rec_source_waveform.clear_cursor] = 32767;
            spare->max[g_rec_source_waveform.clear_cursor] = -32768;
            spare->first[g_rec_source_waveform.clear_cursor] = 0;
            spare->last[g_rec_source_waveform.clear_cursor] = 0;
            g_rec_source_waveform.clear_cursor++;
        }
    }
}

void rec_source_waveform_begin(uint32_t frame_limit)
{
    /* Every take starts at one PCM frame per bin.  Planned duration is a UI
       limit, not a reason to discard temporal detail before it is needed. */
    (void)frame_limit;
    memset(&g_rec_source_waveform, 0, sizeof(g_rec_source_waveform));
    clear_level(&g_rec_source_waveform.levels[0]);
    clear_level(&g_rec_source_waveform.levels[1]);
    g_rec_source_waveform.levels[0].frames_per_bin = 1U;
    g_rec_source_waveform.levels[1].frames_per_bin = 2U;
    g_rec_source_waveform.active = 1U;
}

void rec_source_waveform_abort(void)
{
    g_rec_source_waveform.active = 0U;
}

void rec_source_waveform_capture_service(const float *ring_interleaved,
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

static void live_range(uint32_t frame0, uint32_t frame1,
                       rec_source_waveform_column_t *out)
{
    uint8_t seen = 0U;
    const uint32_t first_block = frame0 / REC_SOURCE_WAVEFORM_LIVE_FRAMES_PER_BIN;
    const uint32_t last_block = (frame1 - 1U)
        / REC_SOURCE_WAVEFORM_LIVE_FRAMES_PER_BIN;
    for (uint32_t block = first_block; block <= last_block; ++block)
    {
        const uint32_t bin = block % REC_SOURCE_WAVEFORM_BINS;
        if (g_rec_source_waveform.live_block[bin] != block + 1U) continue;
        const rec_source_waveform_column_t *const value =
            &g_rec_source_waveform.live[bin];
        if (seen == 0U) { *out = *value; seen = 1U; }
        else
        {
            if (value->min < out->min) out->min = value->min;
            if (value->max > out->max) out->max = value->max;
            out->last = value->last;
        }
    }
    if (seen == 0U) *out = (rec_source_waveform_column_t){0, 0, 0, 0};
}

uint8_t rec_source_waveform_active_overview(uint8_t pixel_width,
                                            rec_source_waveform_column_t *columns,
                                            uint32_t *out_captured_frames,
                                            uint32_t *out_domain_frames)
{
    if ((g_rec_source_waveform.active == 0U) || (pixel_width == 0U)
            || (columns == NULL) || (out_captured_frames == NULL)
            || (out_domain_frames == NULL)) return 0U;
    const uint32_t capacity = REC_SOURCE_WAVEFORM_BINS
        * REC_SOURCE_WAVEFORM_LIVE_FRAMES_PER_BIN;
    const uint32_t domain = (g_rec_source_waveform.captured_frames < capacity)
        ? g_rec_source_waveform.captured_frames : capacity;
    const uint32_t start = g_rec_source_waveform.captured_frames - domain;
    const uint32_t safe_domain = (domain != 0U) ? domain : 1U;
    for (uint8_t col = 0U; col < pixel_width; ++col)
    {
        const uint32_t frame0 = start + (uint32_t)(((uint64_t)col * safe_domain)
            / pixel_width);
        uint32_t frame1 = start + (uint32_t)(((uint64_t)(col + 1U) * safe_domain)
            / pixel_width);
        if (frame1 <= frame0) frame1 = frame0 + 1U;
        if (frame0 >= g_rec_source_waveform.captured_frames)
            columns[col] = (rec_source_waveform_column_t){0, 0, 0, 0};
        else
        {
            if (frame1 > g_rec_source_waveform.captured_frames)
                frame1 = g_rec_source_waveform.captured_frames;
            live_range(frame0, frame1, &columns[col]);
        }
    }
    *out_captured_frames = g_rec_source_waveform.captured_frames;
    *out_domain_frames = safe_domain;
    return 1U;
}

uint8_t rec_source_waveform_finish(
    rec_source_waveform_final_summary_t *out_summary)
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
    const uint8_t level = g_rec_source_waveform.active_level;
    const rec_source_waveform_build_level_t *const final_level =
        &g_rec_source_waveform.levels[level];
    memset(out_summary, 0, sizeof(*out_summary));
    out_summary->frame_count = g_rec_source_waveform.captured_frames;
    out_summary->frames_per_bin = final_level->frames_per_bin;
    out_summary->bin_domain_frames = out_summary->frame_count;
    uint32_t bins = (out_summary->frame_count + out_summary->frames_per_bin - 1U)
        / out_summary->frames_per_bin;
    if (bins > REC_SOURCE_WAVEFORM_BINS) bins = REC_SOURCE_WAVEFORM_BINS;
    out_summary->bin_count = (uint16_t)bins;
    memcpy(out_summary->min, final_level->min, sizeof(out_summary->min));
    memcpy(out_summary->max, final_level->max, sizeof(out_summary->max));
    for (uint16_t i = 0U; i < out_summary->bin_count; ++i)
        if ((out_summary->min[i] == 32767) && (out_summary->max[i] == -32768))
        {
            out_summary->min[i] = 0;
            out_summary->max[i] = 0;
        }
    out_summary->ready = (out_summary->bin_count != 0U) ? 1U : 0U;
    return out_summary->ready;
}

void rec_source_waveform_commit(void)
{
    g_rec_source_waveform.active = 0U;
}
