#include "Storage/sample_capture.h"

#include "Core/brick6_looper_runtime.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Storage/looper_storage.h"
#include "Storage/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>

#define SAMPLE_CAPTURE_TEMP_REC_DIR "0:/PROJECT/REC"
#define SAMPLE_CAPTURE_TEMP_PATH "0:/PROJECT/REC/AUDIOREC_TMP.WAV"
#define SAMPLE_CAPTURE_FINAL_DIR "0:/Samples"
#define SAMPLE_CAPTURE_FINAL_TRIES 10000U
#define SAMPLE_CAPTURE_COPY_FRAMES 1024U
#define SAMPLE_CAPTURE_WAV_DATA_OFFSET MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES
#define SAMPLE_CAPTURE_EDIT_ZOOM_MAX 24U
#define SAMPLE_CAPTURE_STEPS_PER_BAR 16U
#define SAMPLE_CAPTURE_PCM24_PEAK 8388607UL
#define SAMPLE_CAPTURE_WAVEFORM_INITIAL_BUCKET_FRAMES 16U
#define SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES 2048U
#define SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT 1U
#define SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES 2048U
#define SAMPLE_CAPTURE_LINE_MIN_VISIBLE_FRAMES SAMPLE_CAPTURE_LINE_POINTS
#define SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES 2048U
#define SAMPLE_CAPTURE_LINE_LARGE_SETTLE_TICKS 4U

typedef struct
{
    sample_capture_state_t state;
    sample_capture_waveform_bucket_t waveform_pending;
    uint32_t waveform_pending_frames;
    uint8_t detail_requested;
    uint8_t detail_building;
    uint8_t detail_seen[SAMPLE_CAPTURE_DETAIL_POINTS];
    uint32_t detail_request_start_frame;
    uint32_t detail_request_frames;
    uint16_t detail_request_columns;
    uint32_t detail_build_next_frame;
    uint8_t route_mask[SAMPLE_CAPTURE_TRACK_COUNT];
    uint8_t audio_hook_enabled;
    uint8_t last_take_notified;
    uint8_t transport_was_running;
    uint8_t wait_step_valid;
    uint8_t wait_last_step;
    uint32_t wait_last_loop_generation;
    uint16_t final_counter;
} sample_capture_model_t;

typedef struct
{
    uint8_t valid;
    uint32_t start_frame;
    uint32_t frames;
    uint16_t count;
    uint16_t peak;
    int16_t points[SAMPLE_CAPTURE_LINE_POINTS];
    uint8_t requested;
    uint8_t request_settle_ticks;
    uint32_t request_start_frame;
    uint32_t request_frames;
    uint16_t request_points;
    uint8_t building;
    uint8_t seen[SAMPLE_CAPTURE_LINE_POINTS];
    int16_t min[SAMPLE_CAPTURE_LINE_POINTS];
    int16_t max[SAMPLE_CAPTURE_LINE_POINTS];
    int16_t first[SAMPLE_CAPTURE_LINE_POINTS];
    int16_t last[SAMPLE_CAPTURE_LINE_POINTS];
    uint32_t build_start_frame;
    uint32_t build_frames;
    uint32_t build_next_frame;
    uint16_t build_points;
} sample_capture_line_hot_t;

static sample_capture_model_t g_sample_capture;
UI_HOT_DTCM static sample_capture_line_hot_t g_sample_capture_line_hot;
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_copy_buf[SAMPLE_CAPTURE_COPY_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_detail_buf[SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static int16_t
    g_sample_capture_line_source[SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES];

static void sample_capture_copy_path(char *dst, const char *src)
{
    if(dst == 0)
    {
        return;
    }
    dst[0] = '\0';
    if(src == 0)
    {
        return;
    }

    for(uint32_t i = 0U; i < SAMPLE_CAPTURE_PATH_MAX; ++i)
    {
        dst[i] = src[i];
        if(src[i] == '\0')
        {
            return;
        }
    }
    dst[SAMPLE_CAPTURE_PATH_MAX - 1U] = '\0';
}

static uint8_t sample_capture_has_route(void)
{
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
    {
        if(g_sample_capture.route_mask[track] != 0U)
        {
            return 1U;
        }
    }
    return 0U;
}

static uint32_t sample_capture_len_to_frames(uint8_t len_bars)
{
    if(len_bars == 0U)
    {
        return 0U;
    }

    const uint32_t samples_per_step_q16 = seq_runtime_get_samples_per_step_q16();
    if(samples_per_step_q16 == 0U)
    {
        return 0U;
    }

    const uint64_t steps = (uint64_t)len_bars;
    uint64_t frames = ((steps * (uint64_t)samples_per_step_q16) + 0xFFFFULL) >> 16;
    if(frames > 0xFFFFFFFFULL)
    {
        frames = 0xFFFFFFFFULL;
    }
    return (uint32_t)frames;
}

uint32_t sample_capture_model_visible_frames_for_zoom(uint32_t recorded_frames, uint8_t zoom)
{
    static const uint16_t zoom_div_q8[] = {
        256U, 322U, 406U, 512U, 645U, 813U, 1024U, 1290U,
        1625U, 2048U, 2580U, 3251U, 4096U, 5161U, 6502U, 8192U,
        10321U, 13004U, 16384U, 20643U, 26008U, 32768U, 41285U, 52016U, 65535U
    };
    const uint8_t max_zoom = (uint8_t)((sizeof(zoom_div_q8) / sizeof(zoom_div_q8[0])) - 1U);
    if(recorded_frames == 0U)
    {
        return 1U;
    }
    if(zoom > max_zoom)
    {
        zoom = max_zoom;
    }

    uint32_t frames = (uint32_t)(((uint64_t)recorded_frames * 256ULL
            + ((uint64_t)zoom_div_q8[zoom] / 2ULL)) / (uint64_t)zoom_div_q8[zoom]);
    const uint32_t min_frames =
        (recorded_frames < SAMPLE_CAPTURE_LINE_MIN_VISIBLE_FRAMES)
            ? recorded_frames
            : SAMPLE_CAPTURE_LINE_MIN_VISIBLE_FRAMES;
    if((zoom == max_zoom) && (frames > min_frames))
    {
        frames = min_frames;
    }
    if(frames == 0U)
    {
        frames = 1U;
    }
    if(frames > recorded_frames)
    {
        frames = recorded_frames;
    }
    return frames;
}

static void sample_capture_set_error(sample_capture_error_t error)
{
    g_sample_capture.state.error = error;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ERROR;
}

static void sample_capture_waveform_reset(void)
{
    g_sample_capture.state.waveform_count = 0U;
    g_sample_capture.state.waveform_bucket_frames = SAMPLE_CAPTURE_WAVEFORM_INITIAL_BUCKET_FRAMES;
    g_sample_capture.waveform_pending.min = 0;
    g_sample_capture.waveform_pending.max = 0;
    g_sample_capture.waveform_pending.first = 0;
    g_sample_capture.waveform_pending.last = 0;
    g_sample_capture.waveform_pending_frames = 0U;
    memset(g_sample_capture.state.waveform, 0, sizeof(g_sample_capture.state.waveform));
}

static void sample_capture_detail_reset(void)
{
    g_sample_capture.state.detail_valid = 0U;
    g_sample_capture.state.detail_start_frame = 0U;
    g_sample_capture.state.detail_frames = 0U;
    g_sample_capture.state.detail_count = 0U;
    memset(g_sample_capture.state.detail, 0, sizeof(g_sample_capture.state.detail));
    memset(g_sample_capture.detail_seen, 0, sizeof(g_sample_capture.detail_seen));
    g_sample_capture.detail_requested = 0U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_request_start_frame = 0U;
    g_sample_capture.detail_request_frames = 0U;
    g_sample_capture.detail_request_columns = 0U;
    g_sample_capture.detail_build_next_frame = 0U;
    g_sample_capture.state.line_valid = 0U;
    g_sample_capture.state.line_start_frame = 0U;
    g_sample_capture.state.line_frames = 0U;
    g_sample_capture.state.line_count = 0U;
    g_sample_capture.state.line_peak = 0U;
    memset(g_sample_capture.state.line, 0, sizeof(g_sample_capture.state.line));
    memset(&g_sample_capture_line_hot, 0, sizeof(g_sample_capture_line_hot));
}

static int16_t sample_capture_pcm24_to_waveform_i16(int32_t v)
{
    if(v > (int32_t)SAMPLE_CAPTURE_PCM24_PEAK)
    {
        v = (int32_t)SAMPLE_CAPTURE_PCM24_PEAK;
    }
    else if(v < -8388608L)
    {
        v = -8388608L;
    }
    return (int16_t)(v >> 8);
}

static void sample_capture_waveform_compress(void)
{
    for(uint32_t i = 0U; i < (SAMPLE_CAPTURE_WAVEFORM_POINTS / 2U); ++i)
    {
        const sample_capture_waveform_bucket_t a = g_sample_capture.state.waveform[i * 2U];
        const sample_capture_waveform_bucket_t b = g_sample_capture.state.waveform[(i * 2U) + 1U];
        g_sample_capture.state.waveform[i].min = (a.min < b.min) ? a.min : b.min;
        g_sample_capture.state.waveform[i].max = (a.max > b.max) ? a.max : b.max;
        g_sample_capture.state.waveform[i].first = a.first;
        g_sample_capture.state.waveform[i].last = b.last;
    }
    g_sample_capture.state.waveform_count = SAMPLE_CAPTURE_WAVEFORM_POINTS / 2U;
    if(g_sample_capture.state.waveform_bucket_frames <= (0xFFFFFFFFUL / 2U))
    {
        g_sample_capture.state.waveform_bucket_frames *= 2U;
    }
}

static void sample_capture_waveform_append_bucket(const sample_capture_waveform_bucket_t *bucket)
{
    if(bucket == 0)
    {
        return;
    }
    if(g_sample_capture.state.waveform_count >= SAMPLE_CAPTURE_WAVEFORM_POINTS)
    {
        sample_capture_waveform_compress();
    }
    g_sample_capture.state.waveform[g_sample_capture.state.waveform_count++] = *bucket;
}

static void sample_capture_waveform_push_minmax_from_irq(const int32_t *lr_interleaved,
                                                         uint32_t frames)
{
    if((lr_interleaved == 0) || (frames == 0U))
    {
        return;
    }

    for(uint32_t i = 0U; i < frames; ++i)
    {
        const int16_t l = sample_capture_pcm24_to_waveform_i16(lr_interleaved[i * 2U]);
        const int16_t r = sample_capture_pcm24_to_waveform_i16(lr_interleaved[(i * 2U) + 1U]);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        const int16_t sample_mid = (int16_t)(((int32_t)l + (int32_t)r) / 2);

        if(g_sample_capture.waveform_pending_frames == 0U)
        {
            g_sample_capture.waveform_pending.min = sample_min;
            g_sample_capture.waveform_pending.max = sample_max;
            g_sample_capture.waveform_pending.first = sample_mid;
        }
        else
        {
            if(sample_min < g_sample_capture.waveform_pending.min)
            {
                g_sample_capture.waveform_pending.min = sample_min;
            }
            if(sample_max > g_sample_capture.waveform_pending.max)
            {
                g_sample_capture.waveform_pending.max = sample_max;
            }
        }
        g_sample_capture.waveform_pending.last = sample_mid;
        g_sample_capture.waveform_pending_frames++;
        if(g_sample_capture.waveform_pending_frames >= g_sample_capture.state.waveform_bucket_frames)
        {
            sample_capture_waveform_append_bucket(&g_sample_capture.waveform_pending);
            g_sample_capture.waveform_pending.min = 0;
            g_sample_capture.waveform_pending.max = 0;
            g_sample_capture.waveform_pending.first = 0;
            g_sample_capture.waveform_pending.last = 0;
            g_sample_capture.waveform_pending_frames = 0U;
        }
    }
}

static int16_t sample_capture_pcm24le_to_waveform_i16(const uint8_t *p)
{
    int32_t v = (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16));
    if((v & 0x00800000L) != 0)
    {
        v |= (int32_t)0xFF000000L;
    }
    return sample_capture_pcm24_to_waveform_i16(v);
}

static uint16_t sample_capture_abs_i16(int16_t v)
{
    if(v >= 0)
    {
        return (uint16_t)v;
    }
    if(v == (int16_t)-32768)
    {
        return 32768U;
    }
    return (uint16_t)(-v);
}

static uint8_t sample_capture_detail_can_build(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture.detail_request_frames == 0U)
            || (g_sample_capture.detail_request_columns == 0U)
            || (g_sample_capture.detail_request_columns > SAMPLE_CAPTURE_DETAIL_POINTS))
    {
        return 0U;
    }
    if((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U)
            || (sd_preview_is_active() != 0U)
            || (pattern_load_is_pending() != 0U)
            || (sample_cache_has_pending_sd_work() != 0U))
    {
        return 0U;
    }
    return 1U;
}

static uint8_t sample_capture_line_can_build(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture_line_hot.request_frames == 0U)
            || (g_sample_capture_line_hot.request_points == 0U)
            || (g_sample_capture_line_hot.request_points > SAMPLE_CAPTURE_LINE_POINTS))
    {
        return 0U;
    }
    if((multi_record_writer_any_active() != 0U)
            || (looper_storage_raw_export_is_active() != 0U)
            || (sd_preview_is_active() != 0U)
            || (pattern_load_is_pending() != 0U)
            || (sample_cache_has_pending_sd_work() != 0U))
    {
        return 0U;
    }
    return 1U;
}

static void sample_capture_detail_begin_build(void)
{
    g_sample_capture.state.detail_valid = 0U;
    g_sample_capture.state.detail_start_frame = g_sample_capture.detail_request_start_frame;
    g_sample_capture.state.detail_frames = g_sample_capture.detail_request_frames;
    g_sample_capture.state.detail_count = g_sample_capture.detail_request_columns;
    memset(g_sample_capture.state.detail, 0, sizeof(g_sample_capture.state.detail));
    memset(g_sample_capture.detail_seen, 0, sizeof(g_sample_capture.detail_seen));
    g_sample_capture.detail_build_next_frame = 0U;
    g_sample_capture.detail_building = 1U;
}

static void sample_capture_detail_finish_build(void)
{
    for(uint16_t col = 0U; col < g_sample_capture.state.detail_count; ++col)
    {
        if(g_sample_capture.detail_seen[col] == 0U)
        {
            g_sample_capture.state.detail[col].min = 0;
            g_sample_capture.state.detail[col].max = 0;
            g_sample_capture.state.detail[col].first = 0;
            g_sample_capture.state.detail[col].last = 0;
        }
    }
    g_sample_capture.state.detail_valid = 1U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_requested = 0U;
}

static void sample_capture_detail_service(void)
{
    if(g_sample_capture.detail_requested == 0U)
    {
        return;
    }
    if(sample_capture_detail_can_build() == 0U)
    {
        return;
    }
    if(g_sample_capture.detail_building == 0U)
    {
        sample_capture_detail_begin_build();
    }

    uint32_t frames_left = g_sample_capture.detail_request_frames - g_sample_capture.detail_build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_detail_finish_build();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = g_sample_capture.detail_request_start_frame
        + g_sample_capture.detail_build_next_frame;
    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (absolute_frame * MULTI_RECORD_WRITER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < MULTI_RECORD_WRITER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint32_t rel_frame = g_sample_capture.detail_build_next_frame + i;
        uint32_t col = (uint32_t)(((uint64_t)rel_frame
                * (uint64_t)g_sample_capture.detail_request_columns)
            / (uint64_t)g_sample_capture.detail_request_frames);
        if(col >= g_sample_capture.detail_request_columns)
        {
            col = g_sample_capture.detail_request_columns - 1U;
        }

        const uint8_t *frame = &g_sample_capture_detail_buf[i * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        const int16_t sample_mid = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        if(g_sample_capture.detail_seen[col] == 0U)
        {
            g_sample_capture.state.detail[col].min = sample_min;
            g_sample_capture.state.detail[col].max = sample_max;
            g_sample_capture.state.detail[col].first = sample_mid;
            g_sample_capture.state.detail[col].last = sample_mid;
            g_sample_capture.detail_seen[col] = 1U;
        }
        else
        {
            if(sample_min < g_sample_capture.state.detail[col].min)
            {
                g_sample_capture.state.detail[col].min = sample_min;
            }
            if(sample_max > g_sample_capture.state.detail[col].max)
            {
                g_sample_capture.state.detail[col].max = sample_max;
            }
            g_sample_capture.state.detail[col].last = sample_mid;
        }
    }

    g_sample_capture.detail_build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture.detail_build_next_frame >= g_sample_capture.detail_request_frames))
    {
        sample_capture_detail_finish_build();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if(ok == 0U)
    {
        g_sample_capture.detail_building = 0U;
    }
}

static int16_t sample_capture_line_sample_at(const int16_t *frames,
                                             uint32_t frame_count,
                                             uint32_t point,
                                             uint32_t point_count)
{
    if((frames == 0) || (frame_count == 0U) || (point_count == 0U))
    {
        return 0;
    }
    if(frame_count == 1U || point_count == 1U)
    {
        return frames[0];
    }

    const uint64_t pos_q16 = ((uint64_t)point * (uint64_t)(frame_count - 1U) * 65536ULL)
        / (uint64_t)(point_count - 1U);
    uint32_t idx = (uint32_t)(pos_q16 >> 16);
    uint32_t frac = (uint32_t)(pos_q16 & 0xFFFFULL);
    if(idx >= (frame_count - 1U))
    {
        return frames[frame_count - 1U];
    }

    const int32_t a = frames[idx];
    const int32_t b = frames[idx + 1U];
    return (int16_t)(a + (((b - a) * (int32_t)frac) >> 16));
}

static void sample_capture_line_begin_build(void)
{
    g_sample_capture_line_hot.building = 1U;
    g_sample_capture_line_hot.requested = 0U;
    g_sample_capture_line_hot.build_start_frame = g_sample_capture_line_hot.request_start_frame;
    g_sample_capture_line_hot.build_frames = g_sample_capture_line_hot.request_frames;
    g_sample_capture_line_hot.build_points = g_sample_capture_line_hot.request_points;
    g_sample_capture_line_hot.build_next_frame = 0U;
    memset(g_sample_capture_line_hot.seen, 0, sizeof(g_sample_capture_line_hot.seen));
    memset(g_sample_capture_line_hot.min, 0, sizeof(g_sample_capture_line_hot.min));
    memset(g_sample_capture_line_hot.max, 0, sizeof(g_sample_capture_line_hot.max));
    memset(g_sample_capture_line_hot.first, 0, sizeof(g_sample_capture_line_hot.first));
    memset(g_sample_capture_line_hot.last, 0, sizeof(g_sample_capture_line_hot.last));
}

static void sample_capture_line_accumulate_frame(uint32_t rel_frame, int16_t sample)
{
    if((g_sample_capture_line_hot.build_frames == 0U)
            || (g_sample_capture_line_hot.build_points == 0U))
    {
        return;
    }

    uint32_t point = (uint32_t)(((uint64_t)rel_frame
            * (uint64_t)g_sample_capture_line_hot.build_points)
        / (uint64_t)g_sample_capture_line_hot.build_frames);
    if(point >= g_sample_capture_line_hot.build_points)
    {
        point = g_sample_capture_line_hot.build_points - 1U;
    }

    if(g_sample_capture_line_hot.seen[point] == 0U)
    {
        g_sample_capture_line_hot.min[point] = sample;
        g_sample_capture_line_hot.max[point] = sample;
        g_sample_capture_line_hot.first[point] = sample;
        g_sample_capture_line_hot.seen[point] = 1U;
    }
    else
    {
        if(sample < g_sample_capture_line_hot.min[point])
        {
            g_sample_capture_line_hot.min[point] = sample;
        }
        if(sample > g_sample_capture_line_hot.max[point])
        {
            g_sample_capture_line_hot.max[point] = sample;
        }
    }
    g_sample_capture_line_hot.last[point] = sample;
}

static int16_t sample_capture_line_point_from_accum(uint16_t point)
{
    if((point >= g_sample_capture_line_hot.build_points)
            || (g_sample_capture_line_hot.seen[point] == 0U))
    {
        return 0;
    }

    const int16_t min_v = g_sample_capture_line_hot.min[point];
    const int16_t max_v = g_sample_capture_line_hot.max[point];
    const int16_t last_v = g_sample_capture_line_hot.last[point];
    int16_t point_v = g_sample_capture_line_hot.first[point];
    if((min_v < 0) && (max_v > 0))
    {
        point_v = ((point & 1U) == 0U) ? max_v : min_v;
    }
    else
    {
        const uint16_t abs_min = sample_capture_abs_i16(min_v);
        const uint16_t abs_max = sample_capture_abs_i16(max_v);
        point_v = (abs_max >= abs_min) ? max_v : min_v;
    }
    if((point > 0U) && (point < (uint16_t)(g_sample_capture_line_hot.build_points - 1U))
            && (sample_capture_abs_i16(point_v) < sample_capture_abs_i16(last_v)))
    {
        point_v = last_v;
    }
    return point_v;
}

static void sample_capture_line_finish_build(void)
{
    uint16_t line_peak = 0U;
    for(uint16_t point = 0U; point < g_sample_capture_line_hot.build_points; ++point)
    {
        const int16_t point_v = sample_capture_line_point_from_accum(point);
        g_sample_capture_line_hot.points[point] = point_v;
        const uint16_t abs_v = sample_capture_abs_i16(point_v);
        if(abs_v > line_peak)
        {
            line_peak = abs_v;
        }
    }
    g_sample_capture_line_hot.valid = 1U;
    g_sample_capture_line_hot.start_frame = g_sample_capture_line_hot.build_start_frame;
    g_sample_capture_line_hot.frames = g_sample_capture_line_hot.build_frames;
    g_sample_capture_line_hot.count = g_sample_capture_line_hot.build_points;
    g_sample_capture_line_hot.peak = line_peak;
    g_sample_capture_line_hot.building = 0U;
    g_sample_capture_line_hot.build_next_frame = 0U;
}

static void sample_capture_line_service(void)
{
    if((g_sample_capture_line_hot.requested == 0U)
            && (g_sample_capture_line_hot.building == 0U))
    {
        return;
    }
    if(sample_capture_line_can_build() == 0U)
    {
        return;
    }

    if(g_sample_capture_line_hot.building == 0U)
    {
        if(g_sample_capture_line_hot.request_settle_ticks != 0U)
        {
            g_sample_capture_line_hot.request_settle_ticks--;
            return;
        }
        sample_capture_line_begin_build();
    }

    if(g_sample_capture_line_hot.build_frames <= SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES)
    {
        if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
        {
            return;
        }

        FIL fp;
        uint8_t file_open = 0U;
        uint8_t ok = 0U;
        if(sd_access_fs_mount_if_needed() == 0U)
        {
            goto short_done;
        }
        if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
        {
            goto short_done;
        }
        file_open = 1U;

        if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
                + (g_sample_capture_line_hot.build_start_frame * MULTI_RECORD_WRITER_BYTES_PER_FRAME)) != FR_OK)
        {
            goto short_done;
        }

        const uint32_t bytes_to_read =
            g_sample_capture_line_hot.build_frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
        UINT br = 0U;
        if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
                || (br < MULTI_RECORD_WRITER_BYTES_PER_FRAME))
        {
            goto short_done;
        }

        const uint32_t frames_read = br / MULTI_RECORD_WRITER_BYTES_PER_FRAME;
        if(frames_read == 0U)
        {
            goto short_done;
        }

        for(uint32_t i = 0U; i < frames_read; ++i)
        {
            const uint8_t *frame = &g_sample_capture_detail_buf[i * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
            const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
            const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
            g_sample_capture_line_source[i] = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        }

        uint16_t line_peak = 0U;
        for(uint16_t point = 0U; point < g_sample_capture_line_hot.build_points; ++point)
        {
            g_sample_capture_line_hot.points[point] =
                sample_capture_line_sample_at(g_sample_capture_line_source,
                                              frames_read,
                                              point,
                                              g_sample_capture_line_hot.build_points);
            const uint16_t abs_v = sample_capture_abs_i16(g_sample_capture_line_hot.points[point]);
            if(abs_v > line_peak)
            {
                line_peak = abs_v;
            }
        }
        g_sample_capture_line_hot.valid = 1U;
        g_sample_capture_line_hot.start_frame = g_sample_capture_line_hot.build_start_frame;
        g_sample_capture_line_hot.frames = frames_read;
        g_sample_capture_line_hot.count = g_sample_capture_line_hot.build_points;
        g_sample_capture_line_hot.peak = line_peak;
        g_sample_capture_line_hot.building = 0U;
        g_sample_capture_line_hot.build_next_frame = 0U;
        ok = 1U;

short_done:
        if(file_open != 0U)
        {
            (void)f_close(&fp);
        }
        sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
        if(ok == 0U)
        {
            g_sample_capture_line_hot.building = 0U;
        }
        return;
    }

    uint32_t frames_left = g_sample_capture_line_hot.build_frames
        - g_sample_capture_line_hot.build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_line_finish_build();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PREVIEW) == 0U)
    {
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture.state.temp_path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = g_sample_capture_line_hot.build_start_frame
        + g_sample_capture_line_hot.build_next_frame;
    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (absolute_frame * MULTI_RECORD_WRITER_BYTES_PER_FRAME)) != FR_OK)
    {
        goto done;
    }

    const uint32_t bytes_to_read = frames_left * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    UINT br = 0U;
    if((f_read(&fp, g_sample_capture_detail_buf, bytes_to_read, &br) != FR_OK)
            || (br < MULTI_RECORD_WRITER_BYTES_PER_FRAME))
    {
        goto done;
    }

    const uint32_t frames_read = br / MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    for(uint32_t i = 0U; i < frames_read; ++i)
    {
        const uint8_t *frame = &g_sample_capture_detail_buf[i * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        sample_capture_line_accumulate_frame(g_sample_capture_line_hot.build_next_frame + i,
                                             sample);
    }
    g_sample_capture_line_hot.build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture_line_hot.build_next_frame >= g_sample_capture_line_hot.build_frames))
    {
        sample_capture_line_finish_build();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PREVIEW);
    if(ok == 0U)
    {
        g_sample_capture_line_hot.building = 0U;
    }
}

static uint8_t sample_capture_looper_record_conflict(void)
{
    if(brick6_looper_runtime_record_is_active_or_armed() != 0U)
    {
        return 1U;
    }
    return multi_record_writer_any_active_backend(MULTI_RECORD_WRITER_BACKEND_LOOPER_RAW);
}

static uint32_t sample_capture_edit_marker_step(void)
{
    const uint8_t zoom = (g_sample_capture.state.edit_zoom > SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
        ? SAMPLE_CAPTURE_EDIT_ZOOM_MAX
        : g_sample_capture.state.edit_zoom;
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        return 1U;
    }

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames, zoom);

    uint32_t step = visible_frames / 96U;
    if(step == 0U)
    {
        step = 1U;
    }
    return step;
}

static uint32_t sample_capture_edit_scroll_step(void)
{
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        return 1U;
    }

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);

    uint32_t step = visible_frames / 8U;
    if(step == 0U)
    {
        step = 1U;
    }
    return step;
}

static void sample_capture_clamp_edit_window(void)
{
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        g_sample_capture.state.edit_start_frame = 0U;
        g_sample_capture.state.edit_end_frame = 0U;
        g_sample_capture.state.edit_scroll_frame = 0U;
        return;
    }

    if(g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames)
    {
        g_sample_capture.state.edit_end_frame = g_sample_capture.state.recorded_frames;
    }
    if(g_sample_capture.state.edit_start_frame >= g_sample_capture.state.edit_end_frame)
    {
        g_sample_capture.state.edit_start_frame = g_sample_capture.state.edit_end_frame - 1U;
    }

    uint32_t visible_frames =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    if(visible_frames >= g_sample_capture.state.recorded_frames)
    {
        g_sample_capture.state.edit_scroll_frame = 0U;
    }
    else if(g_sample_capture.state.edit_scroll_frame > (g_sample_capture.state.recorded_frames - visible_frames))
    {
        g_sample_capture.state.edit_scroll_frame = g_sample_capture.state.recorded_frames - visible_frames;
    }
}

static void sample_capture_capture_wait_baseline(void)
{
    seq_step_id_t step = 0U;
    g_sample_capture.wait_step_valid = seq_runtime_get_playhead_step(0U, &step);
    g_sample_capture.wait_last_step = (uint8_t)step;
    (void)seq_runtime_get_track_loop_generation(0U, &g_sample_capture.wait_last_loop_generation);
}

static uint8_t sample_capture_quant_is_due(uint8_t transport_started)
{
    if(g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_NOW)
    {
        return 1U;
    }

    if(transport_started != 0U)
    {
        return 1U;
    }

    seq_step_id_t step = 0U;
    if(seq_runtime_get_playhead_step(0U, &step) == 0U)
    {
        return 0U;
    }

    if(g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_PATTERN)
    {
        uint32_t generation = 0U;
        if(seq_runtime_get_track_loop_generation(0U, &generation) == 0U)
        {
            return 0U;
        }
        if(generation != g_sample_capture.wait_last_loop_generation)
        {
            return 1U;
        }
        return 0U;
    }

    const uint8_t step_u8 = (uint8_t)step;
    if((g_sample_capture.wait_step_valid != 0U)
            && (step_u8 != g_sample_capture.wait_last_step)
            && ((step_u8 % (uint8_t)SAMPLE_CAPTURE_STEPS_PER_BAR) == 0U))
    {
        return 1U;
    }
    g_sample_capture.wait_step_valid = 1U;
    g_sample_capture.wait_last_step = step_u8;
    return 0U;
}

static uint8_t sample_capture_prepare_paths(void)
{
    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_mkdir("0:/PROJECT");
        (void)f_mkdir(SAMPLE_CAPTURE_TEMP_REC_DIR);
        (void)f_mkdir(SAMPLE_CAPTURE_FINAL_DIR);
        sample_capture_copy_path(g_sample_capture.state.temp_path, SAMPLE_CAPTURE_TEMP_PATH);
        ok = 1U;
    }
    else
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

static uint8_t sample_capture_make_next_final_path(char *out_path, uint32_t out_len)
{
    if((out_path == 0) || (out_len == 0U))
    {
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() != 0U)
    {
        (void)f_mkdir(SAMPLE_CAPTURE_FINAL_DIR);
        for(uint32_t attempt = 0U; attempt < SAMPLE_CAPTURE_FINAL_TRIES; ++attempt)
        {
            const uint16_t n = g_sample_capture.final_counter++;
            (void)snprintf(out_path, out_len, SAMPLE_CAPTURE_FINAL_DIR "/REC%04u.WAV", (unsigned)n);
            FILINFO info;
            if(f_stat(out_path, &info) == FR_NO_FILE)
            {
                ok = 1U;
                break;
            }
        }
    }
    else
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
    }

    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

static uint8_t sample_capture_start_now(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(sample_capture_has_route() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_ROUTE);
        return 0U;
    }

    if(sample_capture_looper_record_conflict() != 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE);
        return 0U;
    }

    if(sample_capture_prepare_paths() == 0U)
    {
        return 0U;
    }

    const uint32_t frame_limit = sample_capture_len_to_frames(g_sample_capture.state.len_bars);
    if(sample_capture_prepare_temp(g_sample_capture.state.temp_path,
                                   g_sample_capture.state.temp_path,
                                   frame_limit) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        return 0U;
    }

    if(sample_capture_start() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE);
        return 0U;
    }

    g_sample_capture.state.planned_frames = frame_limit;
    g_sample_capture.state.recording = 1U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.take_valid = 0U;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_RECORDING;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.last_take_notified = 0U;
    sample_capture_waveform_reset();
    sample_capture_detail_reset();
    sample_capture_set_audio_hook_enabled(1U);
    return 1U;
}

static void sample_capture_on_take_ready(const multi_record_writer_status_t *status)
{
    if((status == 0) || (g_sample_capture.last_take_notified != 0U))
    {
        return;
    }

    const char *path = 0;
    uint32_t frames = 0U;
    if(multi_record_writer_get_last_sample_wav_take(SAMPLE_CAPTURE_RECORD_CLIENT_ID, &path, &frames) == 0U)
    {
        return;
    }
    if(frames == 0U)
    {
        sample_capture_set_audio_hook_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        g_sample_capture.last_take_notified = 1U;
        return;
    }

    g_sample_capture.state.recording = 0U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.take_valid = 1U;
    g_sample_capture.state.recorded_frames = frames;
    g_sample_capture.state.edit_start_frame = 0U;
    g_sample_capture.state.edit_end_frame = frames;
    g_sample_capture.state.edit_zoom = 0U;
    g_sample_capture.state.edit_scroll_frame = 0U;
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_REC_EDIT;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    sample_capture_copy_path(g_sample_capture.state.temp_path, path);
    sample_capture_detail_reset();
    g_sample_capture.last_take_notified = 1U;
    (void)status;
}

uint8_t sample_capture_prepare_temp(const char *temp_path,
                                    const char *final_path,
                                    uint32_t frame_limit)
{
    return multi_record_writer_prepare_sample_wav(SAMPLE_CAPTURE_RECORD_CLIENT_ID,
                                                  temp_path,
                                                  final_path,
                                                  frame_limit);
}

uint8_t sample_capture_start(void)
{
    return multi_record_writer_start(SAMPLE_CAPTURE_RECORD_CLIENT_ID);
}

uint8_t sample_capture_push_audio_block_from_irq(const int32_t *lr_interleaved,
                                                 uint32_t frames)
{
    sample_capture_waveform_push_minmax_from_irq(lr_interleaved, frames);
    return multi_record_writer_push_audio_block_from_irq(SAMPLE_CAPTURE_RECORD_CLIENT_ID,
                                                         lr_interleaved,
                                                         frames);
}

uint8_t sample_capture_request_stop(void)
{
    return multi_record_writer_request_stop(SAMPLE_CAPTURE_RECORD_CLIENT_ID);
}

uint8_t sample_capture_get_status(multi_record_writer_status_t *out_status)
{
    return multi_record_writer_get_status(SAMPLE_CAPTURE_RECORD_CLIENT_ID, out_status);
}

void sample_capture_set_audio_hook_enabled(uint8_t enabled)
{
    g_sample_capture.audio_hook_enabled = (enabled != 0U) ? 1U : 0U;
}

uint8_t sample_capture_audio_hook_is_enabled(void)
{
    return g_sample_capture.audio_hook_enabled;
}

void sample_capture_model_init(void)
{
    memset(&g_sample_capture, 0, sizeof(g_sample_capture));
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.len_bars = 1U;
    g_sample_capture.state.quant = SAMPLE_CAPTURE_QUANT_NOW;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    sample_capture_waveform_reset();
    sample_capture_detail_reset();
}

void sample_capture_model_get_state(sample_capture_state_t *out_state)
{
    if(out_state == 0)
    {
        return;
    }
    *out_state = g_sample_capture.state;
    out_state->line_valid = g_sample_capture_line_hot.valid;
    out_state->line_start_frame = g_sample_capture_line_hot.start_frame;
    out_state->line_frames = g_sample_capture_line_hot.frames;
    out_state->line_count = g_sample_capture_line_hot.count;
    out_state->line_peak = g_sample_capture_line_hot.peak;
    memcpy(out_state->line, g_sample_capture_line_hot.points, sizeof(out_state->line));
    if((g_sample_capture.waveform_pending_frames != 0U)
            && (out_state->waveform_count < SAMPLE_CAPTURE_WAVEFORM_POINTS))
    {
        out_state->waveform[out_state->waveform_count++] = g_sample_capture.waveform_pending;
    }
    else if((g_sample_capture.waveform_pending_frames != 0U)
            && (out_state->waveform_count != 0U))
    {
        sample_capture_waveform_bucket_t *last =
            &out_state->waveform[out_state->waveform_count - 1U];
        if(g_sample_capture.waveform_pending.min < last->min)
        {
            last->min = g_sample_capture.waveform_pending.min;
        }
        if(g_sample_capture.waveform_pending.max > last->max)
        {
            last->max = g_sample_capture.waveform_pending.max;
        }
        last->last = g_sample_capture.waveform_pending.last;
    }
    for(uint8_t track = 0U; track < SAMPLE_CAPTURE_TRACK_COUNT; ++track)
    {
        out_state->route_enabled[track] = g_sample_capture.route_mask[track];
    }
}

void sample_capture_model_set_view(sample_capture_view_t view)
{
    g_sample_capture.state.view = view;
}

uint8_t sample_capture_model_toggle_route(uint8_t track)
{
    if(track >= SAMPLE_CAPTURE_TRACK_COUNT)
    {
        return 0U;
    }

    g_sample_capture.route_mask[track] = (g_sample_capture.route_mask[track] == 0U) ? 1U : 0U;
    return 1U;
}

uint8_t sample_capture_model_source_track_is_enabled(uint8_t track)
{
    if(track >= SAMPLE_CAPTURE_TRACK_COUNT)
    {
        return 0U;
    }
    return g_sample_capture.route_mask[track];
}

uint8_t sample_capture_model_set_arm(sample_capture_arm_t arm)
{
    if(arm == SAMPLE_CAPTURE_ARM_OFF)
    {
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        g_sample_capture.state.armed_pending = 0U;
        if(g_sample_capture.state.recording != 0U)
        {
            sample_capture_set_audio_hook_enabled(0U);
            (void)sample_capture_request_stop();
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
        }
        else if(g_sample_capture.state.take_valid == 0U)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
        }
        return 1U;
    }

    if(sample_capture_looper_record_conflict() != 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE);
        return 0U;
    }
    if(sample_capture_has_route() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_ROUTE);
        return 0U;
    }

    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_REC;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
    return 1U;
}

uint8_t sample_capture_model_step_arm(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    if(delta > 0)
    {
        if(g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
        {
            return 0U;
        }
        return sample_capture_model_set_arm(SAMPLE_CAPTURE_ARM_REC);
    }

    if(g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_OFF)
    {
        return 0U;
    }
    return sample_capture_model_set_arm(SAMPLE_CAPTURE_ARM_OFF);
}

uint8_t sample_capture_model_step_len(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    if((g_sample_capture.state.recording != 0U) || (g_sample_capture.state.armed_pending != 0U))
    {
        return 0U;
    }

    int16_t next = (int16_t)g_sample_capture.state.len_bars + ((delta > 0) ? 1 : -1);
    if(next < 0)
    {
        next = 0;
    }
    if(next > (int16_t)SAMPLE_CAPTURE_LEN_FIXED_MAX)
    {
        next = SAMPLE_CAPTURE_LEN_FIXED_MAX;
    }
    g_sample_capture.state.len_bars = (uint8_t)next;
    return 1U;
}

uint8_t sample_capture_model_step_quant(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    if((g_sample_capture.state.recording != 0U) || (g_sample_capture.state.armed_pending != 0U))
    {
        return 0U;
    }

    int16_t next = (int16_t)g_sample_capture.state.quant + ((delta > 0) ? 1 : -1);
    if(next < 0)
    {
        next = 0;
    }
    if(next >= (int16_t)SAMPLE_CAPTURE_QUANT_COUNT)
    {
        next = (int16_t)SAMPLE_CAPTURE_QUANT_COUNT - 1;
    }
    g_sample_capture.state.quant = (sample_capture_quant_t)next;
    return 1U;
}

uint8_t sample_capture_model_step_edit(uint8_t encoder, int16_t delta)
{
    if((delta == 0) || (g_sample_capture.state.take_valid == 0U))
    {
        return 0U;
    }

    const uint32_t coarse = (delta > 0) ? (uint32_t)delta : (uint32_t)(-delta);
    const uint32_t step = coarse * sample_capture_edit_marker_step();
    uint8_t changed = 0U;

    if(encoder == 2U)
    {
        if(delta > 0)
        {
            if(step > (0xFFFFFFFFUL - g_sample_capture.state.edit_start_frame))
            {
                g_sample_capture.state.edit_start_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_start_frame += step;
            }
            if(g_sample_capture.state.edit_start_frame >= g_sample_capture.state.edit_end_frame)
            {
                g_sample_capture.state.edit_start_frame = g_sample_capture.state.edit_end_frame - 1U;
            }
        }
        else if(step < g_sample_capture.state.edit_start_frame)
        {
            g_sample_capture.state.edit_start_frame -= step;
        }
        else
        {
            g_sample_capture.state.edit_start_frame = 0U;
        }
        changed = 1U;
    }
    else if(encoder == 3U)
    {
        if(delta > 0)
        {
            if(step > (0xFFFFFFFFUL - g_sample_capture.state.edit_end_frame))
            {
                g_sample_capture.state.edit_end_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_end_frame += step;
            }
            if(g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames)
            {
                g_sample_capture.state.edit_end_frame = g_sample_capture.state.recorded_frames;
            }
        }
        else if((g_sample_capture.state.edit_end_frame > g_sample_capture.state.edit_start_frame + 1U)
                && (step < (g_sample_capture.state.edit_end_frame - g_sample_capture.state.edit_start_frame)))
        {
            g_sample_capture.state.edit_end_frame -= step;
        }
        else
        {
            g_sample_capture.state.edit_end_frame = g_sample_capture.state.edit_start_frame + 1U;
        }
        changed = 1U;
    }
    else if(encoder == 0U)
    {
        if(delta > 0)
        {
            if(g_sample_capture.state.edit_zoom < SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
            {
                g_sample_capture.state.edit_zoom++;
            }
        }
        else if(g_sample_capture.state.edit_zoom > 0U)
        {
            g_sample_capture.state.edit_zoom--;
        }
        changed = 1U;
    }
    else if(encoder == 1U)
    {
        const uint32_t scroll_step = coarse * sample_capture_edit_scroll_step();
        if(delta > 0)
        {
            if(scroll_step > (0xFFFFFFFFUL - g_sample_capture.state.edit_scroll_frame))
            {
                g_sample_capture.state.edit_scroll_frame = 0xFFFFFFFFUL;
            }
            else
            {
                g_sample_capture.state.edit_scroll_frame += scroll_step;
            }
        }
        else if(scroll_step < g_sample_capture.state.edit_scroll_frame)
        {
            g_sample_capture.state.edit_scroll_frame -= scroll_step;
        }
        else
        {
            g_sample_capture.state.edit_scroll_frame = 0U;
        }
        changed = 1U;
    }

    if(changed != 0U)
    {
        sample_capture_clamp_edit_window();
        if(g_sample_capture.state.final_path[0] != '\0')
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
            g_sample_capture.state.final_path[0] = '\0';
        }
        else if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
            g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
        }
    }
    return 1U;
}

void sample_capture_model_request_detail_waveform(uint32_t start_frame,
                                                  uint32_t frame_count,
                                                  uint16_t columns)
{
    if(SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS > SAMPLE_CAPTURE_DETAIL_POINTS)
    {
        return;
    }
    if((g_sample_capture.state.take_valid == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture.state.recorded_frames)
            || ((g_sample_capture.state.recorded_frames - start_frame) < frame_count))
    {
        return;
    }

    uint32_t margin = frame_count * SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT;
    if(margin == 0U)
    {
        margin = 1U;
    }
    uint32_t cache_start = (start_frame > margin) ? (start_frame - margin) : 0U;
    uint32_t cache_end = start_frame + frame_count;
    if((0xFFFFFFFFUL - cache_end) > margin)
    {
        cache_end += margin;
    }
    if(cache_end > g_sample_capture.state.recorded_frames)
    {
        cache_end = g_sample_capture.state.recorded_frames;
    }
    if(cache_end <= cache_start)
    {
        return;
    }

    uint32_t request_columns = (uint32_t)columns;
    if(request_columns == 0U)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    request_columns *= (1U + (2U * SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT));
    if(request_columns < SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    if(request_columns > SAMPLE_CAPTURE_DETAIL_POINTS)
    {
        request_columns = SAMPLE_CAPTURE_DETAIL_POINTS;
    }

    if((g_sample_capture.state.detail_valid != 0U)
            && (g_sample_capture.state.detail_start_frame <= start_frame)
            && ((g_sample_capture.state.detail_start_frame + g_sample_capture.state.detail_frames)
                >= (start_frame + frame_count))
            && (g_sample_capture.state.detail_count >= request_columns))
    {
        return;
    }
    if((g_sample_capture.detail_requested != 0U)
            && (g_sample_capture.detail_request_start_frame <= start_frame)
            && ((g_sample_capture.detail_request_start_frame + g_sample_capture.detail_request_frames)
                >= (start_frame + frame_count))
            && (g_sample_capture.detail_request_columns >= request_columns))
    {
        return;
    }

    g_sample_capture.detail_requested = 1U;
    g_sample_capture.detail_building = 0U;
    g_sample_capture.detail_request_start_frame = cache_start;
    g_sample_capture.detail_request_frames = cache_end - cache_start;
    g_sample_capture.detail_request_columns = (uint16_t)request_columns;
    g_sample_capture.detail_build_next_frame = 0U;
}

void sample_capture_model_request_line_waveform(uint32_t start_frame,
                                                uint32_t frame_count,
                                                uint16_t columns)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture.state.recorded_frames)
            || ((g_sample_capture.state.recorded_frames - start_frame) < frame_count))
    {
        return;
    }

    uint16_t points = columns;
    if(points == 0U)
    {
        points = SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS;
    }
    if(points > SAMPLE_CAPTURE_LINE_POINTS)
    {
        points = SAMPLE_CAPTURE_LINE_POINTS;
    }

    if((g_sample_capture_line_hot.valid != 0U)
            && (g_sample_capture_line_hot.start_frame == start_frame)
            && (g_sample_capture_line_hot.frames == frame_count)
            && (g_sample_capture_line_hot.count == points))
    {
        return;
    }
    if((g_sample_capture_line_hot.requested != 0U)
            && (g_sample_capture_line_hot.request_start_frame == start_frame)
            && (g_sample_capture_line_hot.request_frames == frame_count)
            && (g_sample_capture_line_hot.request_points == points))
    {
        return;
    }
    if((g_sample_capture_line_hot.building != 0U)
            && (g_sample_capture_line_hot.build_start_frame == start_frame)
            && (g_sample_capture_line_hot.build_frames == frame_count)
            && (g_sample_capture_line_hot.build_points == points))
    {
        return;
    }

    g_sample_capture_line_hot.building = 0U;
    g_sample_capture_line_hot.requested = 1U;
    g_sample_capture_line_hot.request_settle_ticks =
        (frame_count > SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES)
            ? SAMPLE_CAPTURE_LINE_LARGE_SETTLE_TICKS
            : 0U;
    g_sample_capture_line_hot.request_start_frame = start_frame;
    g_sample_capture_line_hot.request_frames = frame_count;
    g_sample_capture_line_hot.request_points = points;
}

void sample_capture_model_service(void)
{
    multi_record_writer_status_t status;
    if(sample_capture_get_status(&status) == 0U)
    {
        return;
    }

    const uint8_t transport_running = seq_runtime_is_running();
    const uint8_t rec_global_armed = seq_runtime_rec_is_armed();
    const uint8_t transport_started =
        ((transport_running != 0U) && (g_sample_capture.transport_was_running == 0U)) ? 1U : 0U;
    g_sample_capture.transport_was_running = transport_running;

    if((status.state == MULTI_RECORD_WRITER_STATE_FAILED)
            && ((g_sample_capture.state.recording != 0U) || (g_sample_capture.state.armed_pending != 0U)))
    {
        sample_capture_set_audio_hook_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        sample_capture_set_error((status.error == MULTI_RECORD_WRITER_ERROR_RING_OVERFLOW)
            ? SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE
            : SAMPLE_CAPTURE_ERROR_SD_IO);
        return;
    }

    if((g_sample_capture.state.recording != 0U)
            && (rec_global_armed == 0U))
    {
        sample_capture_set_audio_hook_enabled(0U);
        (void)sample_capture_request_stop();
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
    }

    if((g_sample_capture.state.recording != 0U)
            && (g_sample_capture.state.len_bars == 0U)
            && (transport_running == 0U))
    {
        sample_capture_set_audio_hook_enabled(0U);
        (void)sample_capture_request_stop();
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
    }

    if((g_sample_capture.state.recording != 0U)
            && (g_sample_capture.state.planned_frames != 0U)
            && (status.frames_received >= g_sample_capture.state.planned_frames))
    {
        sample_capture_set_audio_hook_enabled(0U);
        (void)sample_capture_request_stop();
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_STOPPING;
    }

    if((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
            && (g_sample_capture.state.recording == 0U)
            && (g_sample_capture.state.armed_pending == 0U)
            && (rec_global_armed != 0U))
    {
        g_sample_capture.state.armed_pending = 1U;
        g_sample_capture.state.phase = (transport_running != 0U)
            ? SAMPLE_CAPTURE_PHASE_WAIT_QUANT
            : SAMPLE_CAPTURE_PHASE_ARMED;
        sample_capture_capture_wait_baseline();
    }

    if((g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (rec_global_armed == 0U))
    {
        g_sample_capture.state.armed_pending = 0U;
        if(g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
        }
    }

    if((g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (rec_global_armed != 0U)
            && (transport_running != 0U)
            && (sample_capture_quant_is_due(transport_started) != 0U))
    {
        if(sample_capture_start_now() == 0U)
        {
            g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
            g_sample_capture.state.armed_pending = 0U;
        }
    }

    if(status.state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
    {
        sample_capture_on_take_ready(&status);
    }

    sample_capture_detail_service();
    sample_capture_line_service();
}

uint8_t sample_capture_model_return_to_audio_rec(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    if(g_sample_capture.state.recording == 0U && g_sample_capture.state.armed_pending == 0U)
    {
        g_sample_capture.state.phase = (g_sample_capture.state.take_valid != 0U)
            ? SAMPLE_CAPTURE_PHASE_REC_EDIT
            : SAMPLE_CAPTURE_PHASE_IDLE;
    }
    return 1U;
}

uint8_t sample_capture_model_audition_trimmed(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || ((g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_REC_EDIT)
                && (g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_SAVED)
                && (g_sample_capture.state.phase != SAMPLE_CAPTURE_PHASE_ERROR))
            || (g_sample_capture.state.edit_end_frame <= g_sample_capture.state.edit_start_frame)
            || (g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_INVALID_ARG);
        return 0U;
    }

    if((sd_preview_is_active() != 0U)
            && (strcmp(sd_preview_get_path(), g_sample_capture.state.temp_path) == 0))
    {
        sd_preview_stop();
        g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
        if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
        {
            g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
        }
        return 1U;
    }

    if(sd_preview_begin_range(g_sample_capture.state.temp_path,
                              g_sample_capture.state.edit_start_frame,
                              g_sample_capture.state.edit_end_frame) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL);
        return 0U;
    }

    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
    {
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    }
    return 1U;
}

static uint8_t sample_capture_copy_trimmed_take(const char *src_path,
                                                const char *dst_path,
                                                uint32_t start_frame,
                                                uint32_t end_frame)
{
    if((src_path == 0) || (dst_path == 0) || (end_frame <= start_frame))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_INVALID_ARG);
        return 0U;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_RECORDER) == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_BUSY);
        return 0U;
    }

    FIL src;
    FIL dst;
    uint8_t src_open = 0U;
    uint8_t dst_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    FRESULT fr = f_open(&src, src_path, FA_READ);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    src_open = 1U;

    fr = f_open(&dst, dst_path, FA_CREATE_NEW | FA_WRITE);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    dst_open = 1U;

    uint8_t header[SAMPLE_CAPTURE_WAV_DATA_OFFSET];
    UINT br = 0U;
    fr = f_lseek(&src, 0U);
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }
    fr = f_read(&src, header, sizeof(header), &br);
    if((fr != FR_OK) || (br != sizeof(header)))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    const uint32_t frames = end_frame - start_frame;
    const uint32_t data_bytes = frames * MULTI_RECORD_WRITER_BYTES_PER_FRAME;
    header[4] = (uint8_t)(((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) & 0xFFU);
    header[5] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 8) & 0xFFU);
    header[6] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 16) & 0xFFU);
    header[7] = (uint8_t)((((SAMPLE_CAPTURE_WAV_DATA_OFFSET - 8U) + data_bytes) >> 24) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 4U] = (uint8_t)(data_bytes & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 3U] = (uint8_t)((data_bytes >> 8) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 2U] = (uint8_t)((data_bytes >> 16) & 0xFFU);
    header[SAMPLE_CAPTURE_WAV_DATA_OFFSET - 1U] = (uint8_t)((data_bytes >> 24) & 0xFFU);

    UINT bw = 0U;
    fr = f_write(&dst, header, sizeof(header), &bw);
    if((fr != FR_OK) || (bw != sizeof(header)))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    fr = f_lseek(&src, SAMPLE_CAPTURE_WAV_DATA_OFFSET +
        (start_frame * MULTI_RECORD_WRITER_BYTES_PER_FRAME));
    if(fr != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    uint32_t bytes_left = data_bytes;
    while(bytes_left != 0U)
    {
        uint32_t chunk = sizeof(g_sample_capture_copy_buf);
        if(chunk > bytes_left)
        {
            chunk = bytes_left;
        }
        br = 0U;
        fr = f_read(&src, g_sample_capture_copy_buf, chunk, &br);
        if((fr != FR_OK) || (br != chunk))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            goto done;
        }
        bw = 0U;
        fr = f_write(&dst, g_sample_capture_copy_buf, chunk, &bw);
        if((fr != FR_OK) || (bw != chunk))
        {
            sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
            goto done;
        }
        bytes_left -= chunk;
    }

    if(f_sync(&dst) != FR_OK)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        goto done;
    }

    ok = 1U;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;

done:
    if(dst_open != 0U)
    {
        (void)f_close(&dst);
    }
    if(src_open != 0U)
    {
        (void)f_close(&src);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_RECORDER);
    return ok;
}

uint8_t sample_capture_model_save_trimmed(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(g_sample_capture.state.take_valid == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }
    if(g_sample_capture.state.final_path[0] != '\0')
    {
        return 1U;
    }

    char final_path[SAMPLE_CAPTURE_PATH_MAX];
    if(sample_capture_make_next_final_path(final_path, sizeof(final_path)) == 0U)
    {
        return 0U;
    }

    if(sample_capture_copy_trimmed_take(g_sample_capture.state.temp_path,
                                        final_path,
                                        g_sample_capture.state.edit_start_frame,
                                        g_sample_capture.state.edit_end_frame) == 0U)
    {
        return 0U;
    }

    sample_capture_copy_path(g_sample_capture.state.final_path, final_path);
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_SAVED;
    return 1U;
}

uint8_t sample_capture_model_assign_trimmed(void)
{
    if(sd_preview_is_active() != 0U)
    {
        sd_preview_stop();
    }

    if(g_sample_capture.state.take_valid == 0U)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_TAKE);
        return 0U;
    }

    uint16_t slot = SAMPLE_POOL_SIZE;
    for(uint16_t i = 0U; i < SAMPLE_POOL_SIZE; ++i)
    {
        if(sample_pool_get_state(i) == SAMPLE_POOL_SLOT_EMPTY)
        {
            slot = i;
            break;
        }
    }

    if(slot >= SAMPLE_POOL_SIZE)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_SLOT);
        return 0U;
    }

    if(sample_capture_model_save_trimmed() == 0U)
    {
        return 0U;
    }

    if(sample_pool_load(slot, g_sample_capture.state.final_path) == false)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
        return 0U;
    }

    return 1U;
}
