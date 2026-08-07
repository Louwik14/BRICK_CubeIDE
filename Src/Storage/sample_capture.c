#include "Storage/sample_capture.h"

#include "Core/rec_live_debug.h"
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
#include "Storage/waveform_cache.h"
#include "ff.h"

#if SAMPLE_CAPTURE_DEBUG_UART
#include "stm32h7xx_hal.h"
#include "usart.h"
#include <stdarg.h>
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>

#if SAMPLE_CAPTURE_DEBUG_UART && SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
#define SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART 1U
#else
#define SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART 0U
#endif

#define SAMPLE_CAPTURE_TEMP_REC_DIR "0:/PROJECT/REC"
#define SAMPLE_CAPTURE_TEMP_PATH "0:/PROJECT/REC/AUDIOREC_TMP.WAV"
#define SAMPLE_CAPTURE_FINAL_DIR "0:/Samples"
#define SAMPLE_CAPTURE_FINAL_TRIES 10000U
#define SAMPLE_CAPTURE_COPY_FRAMES 1024U
#define SAMPLE_CAPTURE_WAV_DATA_OFFSET MULTI_RECORD_WRITER_WAV_DATA_OFFSET_BYTES
#define SAMPLE_CAPTURE_EDIT_ZOOM_MAX 255U
#define SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES 256U
#define SAMPLE_CAPTURE_STEPS_PER_BAR 16U
#define SAMPLE_CAPTURE_PCM24_PEAK 8388607UL
#define SAMPLE_CAPTURE_WAVEFORM_INITIAL_BUCKET_FRAMES 16U
#define SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES 2048U
#define SAMPLE_CAPTURE_DETAIL_CACHE_MARGIN_MULT 1U
#define SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES 2048U
#define SAMPLE_CAPTURE_LINE_MIN_VISIBLE_FRAMES SAMPLE_CAPTURE_LINE_POINTS
#define SAMPLE_CAPTURE_LINE_BUILD_CHUNK_FRAMES 2048U
#define SAMPLE_CAPTURE_LINE_LARGE_SETTLE_TICKS 4U
#define SAMPLE_CAPTURE_ZCROSS_SEARCH_FRAMES 2048U
#define SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES 8U
#define SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT 2U
#define SAMPLE_CAPTURE_EDIT_VZOOM_MAX 8U
#define SAMPLE_CAPTURE_EDITOR_TILE_COUNT 16U
#define SAMPLE_CAPTURE_EDITOR_TILE_FRAMES (MULTI_RECORD_WRITER_SAMPLE_RATE_HZ / 2U)
#define SAMPLE_CAPTURE_EDITOR_CACHE_FRAMES \
    (SAMPLE_CAPTURE_EDITOR_TILE_COUNT * SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
#define SAMPLE_CAPTURE_EDITOR_TILE_BUILD_CHUNK_FRAMES 2048U
#define SAMPLE_CAPTURE_EDITOR_TILE_REQUEST_MAX 4U
#define SAMPLE_CAPTURE_EDITOR_TILE_KEEP_RADIUS 2U
#define SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES 16U
#define SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES 64U
#define SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES 256U
#define SAMPLE_CAPTURE_EDITOR_LEVEL0_POINTS \
    ((SAMPLE_CAPTURE_EDITOR_TILE_FRAMES + SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES - 1U) \
        / SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES)
#define SAMPLE_CAPTURE_EDITOR_LEVEL1_POINTS \
    ((SAMPLE_CAPTURE_EDITOR_TILE_FRAMES + SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES - 1U) \
        / SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES)
#define SAMPLE_CAPTURE_EDITOR_LEVEL2_POINTS \
    ((SAMPLE_CAPTURE_EDITOR_TILE_FRAMES + SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES - 1U) \
        / SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES)
#define SAMPLE_CAPTURE_EDITOR_DIRECT_SCAN_MAX_FRAMES 4096U
#define SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES 2048U

typedef struct
{
    sample_capture_state_t state;
    waveform_cache_handle_t wave_cache_handle;
    uint8_t wave_cache_ready;
    uint8_t wave_cache_retry_countdown;
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
    uint32_t threshold_pcm24;
    uint8_t trigger_pending;
    uint8_t last_take_notified;
    uint8_t rec_edit_enter_deferred_services;
    uint8_t rec_edit_first_render_pending;
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

typedef struct
{
    int16_t min;
    int16_t max;
    int16_t first;
    int16_t last;
} sample_capture_editor_level_point_t;

typedef enum
{
    SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY = 0,
    SAMPLE_CAPTURE_EDITOR_CACHE_LOADING,
    SAMPLE_CAPTURE_EDITOR_CACHE_READY,
    SAMPLE_CAPTURE_EDITOR_CACHE_STALE
} sample_capture_editor_cache_state_t;

typedef struct
{
    sample_capture_editor_cache_state_t state;
    uint32_t start_frame;
    uint32_t frame_count;
    uint32_t load_next_frame;
    uint32_t generation;
    uint16_t level0_count;
    uint16_t level1_count;
    uint16_t level2_count;
} sample_capture_editor_audio_tile_t;

typedef struct
{
    char path[SAMPLE_CAPTURE_PATH_MAX];
    uint32_t generation;
    uint32_t focus_frame;
    uint32_t last_view_start_frame;
    uint32_t last_view_frames;
    sample_capture_editor_audio_tile_t tiles[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
} sample_capture_editor_audio_cache_t;

typedef struct
{
    int16_t min;
    int16_t max;
} sample_capture_global_overview_point_t;

typedef enum
{
    SAMPLE_CAPTURE_GLOBAL_OVERVIEW_EMPTY = 0,
    SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING,
    SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY,
    SAMPLE_CAPTURE_GLOBAL_OVERVIEW_ERROR
} sample_capture_global_overview_state_t;

typedef struct
{
    sample_capture_global_overview_state_t state;
    char path[SAMPLE_CAPTURE_PATH_MAX];
    uint32_t frame_count;
    uint32_t build_next_frame;
    uint16_t point_count;
    uint16_t peak;
    uint32_t generation;
} sample_capture_global_overview_t;

#if SAMPLE_CAPTURE_DEBUG_UART
typedef struct
{
    sample_capture_renderer_debug_t last_renderer;
    uint8_t last_renderer_valid;
    uint32_t last_summary_ms;
    uint32_t draw_count;
    uint32_t renderer_count[8U];
    uint16_t last_draw_segments;
    uint16_t max_draw_segments;
    uint32_t cache_hit_count;
    uint32_t cache_miss_count;
    uint32_t last_miss_start;
    uint32_t last_miss_frames;
    uint32_t last_miss_ms;
    uint8_t last_miss_valid;
    uint32_t cache_request_count;
    uint32_t cache_chunks;
    uint32_t cache_gate_busy_count;
    uint32_t cache_block_sample_count;
    uint32_t cache_block_pattern_count;
    uint32_t cache_block_preview_count;
    uint32_t cache_block_writer_count;
    uint32_t cache_block_export_count;
    uint32_t fill_passes;
    uint32_t fill_chunks;
    uint32_t fill_start_ms;
    uint32_t fill_last_ms;
    uint32_t fill_max_ms;
    uint32_t eline_count;
    uint32_t eline_last_ms;
    uint32_t eline_max_ms;
    uint32_t draw_last_ms;
    uint32_t draw_max_ms;
    uint32_t waveform_last_ms;
    uint32_t waveform_max_ms;
    uint32_t flush_count;
    uint32_t flush_cont_count;
    uint32_t flush_last_ms;
    uint32_t flush_max_ms;
    uint32_t last_summary_draw_count;
    uint32_t last_summary_eline_count;
    uint32_t last_summary_flush_count;
    uint8_t last_zoom;
    uint8_t last_source_change_valid;
    uint32_t last_view_start_frame;
    uint32_t last_view_frames;
    uint32_t last_samples_per_pixel;
    uint32_t last_wavecache_frames_per_column;
    uint8_t last_fallback_reason;
    uint8_t fill_started_logged;
} sample_capture_debug_t;
#endif

static sample_capture_model_t g_sample_capture;
static volatile sample_capture_live_summary_t g_sample_capture_live_summary;
UI_HOT_DTCM static sample_capture_line_hot_t g_sample_capture_line_hot;
STORAGE_STATE_SDRAM static sample_capture_editor_audio_cache_t g_sample_capture_editor_cache;
STORAGE_STATE_SDRAM static sample_capture_global_overview_t g_sample_capture_global_overview;
EDITOR_AUDIO_CACHE_SDRAM static int16_t
    g_sample_capture_editor_audio[SAMPLE_CAPTURE_EDITOR_TILE_COUNT][SAMPLE_CAPTURE_EDITOR_TILE_FRAMES];
EDITOR_AUDIO_CACHE_SDRAM static sample_capture_editor_level_point_t
    g_sample_capture_editor_level0[SAMPLE_CAPTURE_EDITOR_TILE_COUNT][SAMPLE_CAPTURE_EDITOR_LEVEL0_POINTS];
EDITOR_AUDIO_CACHE_SDRAM static sample_capture_editor_level_point_t
    g_sample_capture_editor_level1[SAMPLE_CAPTURE_EDITOR_TILE_COUNT][SAMPLE_CAPTURE_EDITOR_LEVEL1_POINTS];
EDITOR_AUDIO_CACHE_SDRAM static sample_capture_editor_level_point_t
    g_sample_capture_editor_level2[SAMPLE_CAPTURE_EDITOR_TILE_COUNT][SAMPLE_CAPTURE_EDITOR_LEVEL2_POINTS];
EDITOR_AUDIO_CACHE_SDRAM static sample_capture_global_overview_point_t
    g_sample_capture_global_overview_points[SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_copy_buf[SAMPLE_CAPTURE_COPY_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_detail_buf[SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static int16_t
    g_sample_capture_line_source[SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES];
#if SAMPLE_CAPTURE_DEBUG_UART
STORAGE_STATE_SDRAM static sample_capture_debug_t g_sample_capture_debug;
#endif

static void sample_capture_editor_cache_reset(void);
static void sample_capture_global_overview_reset(void);

static uint32_t sample_capture_threshold_dbfs_to_pcm24(int16_t threshold_dbfs)
{
    if(threshold_dbfs < SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN)
    {
        threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN;
    }
    if(threshold_dbfs > SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX)
    {
        threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX;
    }

    const float linear = powf(10.0f, (float)threshold_dbfs * 0.05f);
    const float scaled = linear * (float)SAMPLE_CAPTURE_LIVE_PCM24_FULL_SCALE;
    return (scaled >= (float)SAMPLE_CAPTURE_LIVE_PCM24_FULL_SCALE)
        ? SAMPLE_CAPTURE_LIVE_PCM24_FULL_SCALE
        : (uint32_t)(scaled + 0.5f);
}

static uint8_t sample_capture_mic_source_available(sample_capture_arm_t arm)
{
#if defined(BRICK6_VARIANT_LOWCOST)
    (void)arm;
    return (uint8_t)(g_sample_capture.state.mic_enabled != 0U);
#else
    (void)arm;
    return 0U;
#endif
}

#if SAMPLE_CAPTURE_DEBUG_UART
static void sample_capture_debug_log(const char *fmt, ...)
{
    char line[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    if(n <= 0)
    {
        return;
    }
    if((uint32_t)n >= sizeof(line))
    {
        n = (int)sizeof(line) - 1;
    }
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)n, 20U);
}

static void sample_capture_debug_reset(void)
{
    memset(&g_sample_capture_debug, 0, sizeof(g_sample_capture_debug));
}
#else
#define sample_capture_debug_reset() ((void)0)
#endif

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
static const char *sample_capture_debug_renderer_label(sample_capture_renderer_debug_t renderer)
{
    switch(renderer)
    {
        case SAMPLE_CAPTURE_RENDERER_EMPTY: return "EMPTY";
        case SAMPLE_CAPTURE_RENDERER_GLOBAL_OVERVIEW: return "GLOBAL_OVERVIEW";
        case SAMPLE_CAPTURE_RENDERER_OLD_LINE: return "OLD_LINE";
        case SAMPLE_CAPTURE_RENDERER_OLD_AUDIO_TILE: return "OLD_AUDIO_TILE";
        case SAMPLE_CAPTURE_RENDERER_BRKWAVE_TILE: return "BRKWAVE_TILE";
        case SAMPLE_CAPTURE_RENDERER_SD_LINE_FALLBACK: return "SD_LINE";
        case SAMPLE_CAPTURE_RENDERER_BUILDING: return "BUILDING";
        case SAMPLE_CAPTURE_RENDERER_ERROR: return "ERROR";
        default: return "?";
    }
}
#endif

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

static uint8_t sample_capture_path_is_temp(const char *path)
{
    if((path == 0) || (path[0] == '\0'))
    {
        return 1U;
    }
    if(strcmp(path, SAMPLE_CAPTURE_TEMP_PATH) == 0)
    {
        return 1U;
    }
    if((strstr(path, "_TMP") != 0) || (strstr(path, "_tmp") != 0))
    {
        return 1U;
    }
    return 0U;
}

static uint32_t sample_capture_debug_state_word(void)
{
    return ((uint32_t)g_sample_capture.state.phase & 0xFFU)
        | (((uint32_t)g_sample_capture.state.view & 0xFFU) << 8)
        | (((uint32_t)g_sample_capture.state.recording & 0x01U) << 16)
        | (((uint32_t)g_sample_capture.state.armed_pending & 0x01U) << 17)
        | (((uint32_t)g_sample_capture.state.take_valid & 0x01U) << 18);
}

static void sample_capture_debug_mark(rec_live_debug_code_t code,
                                      const multi_record_writer_status_t *status,
                                      const char *path,
                                      uint32_t recorded_frames)
{
    rec_live_debug_mark((uint32_t)code,
                        recorded_frames,
                        rec_live_debug_path_hash(path),
                        (status != 0) ? (uint32_t)status->state : 0U,
                        sample_capture_debug_state_word(),
                        (status != 0) ? (uint32_t)status->error
                                      : (uint32_t)g_sample_capture.state.error);
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
    if(recorded_frames == 0U)
    {
        return 1U;
    }
    const uint32_t min_frames =
        (recorded_frames < SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES)
            ? recorded_frames
            : SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES;
    if((zoom == 0U) || (recorded_frames <= min_frames))
    {
        return recorded_frames;
    }
    if(zoom >= SAMPLE_CAPTURE_EDIT_ZOOM_MAX)
    {
        return (min_frames == 0U) ? 1U : min_frames;
    }

    const float total = (float)recorded_frames;
    const float min_view = (float)min_frames;
    const float zoom_norm = (float)zoom / (float)SAMPLE_CAPTURE_EDIT_ZOOM_MAX;
    const float ratio = min_view / total;
    uint32_t frames = (uint32_t)((total * powf(ratio, zoom_norm)) + 0.5f);
    if(frames > recorded_frames) { frames = recorded_frames; }
    if(frames < min_frames) { frames = min_frames; }
    if(frames == 0U)
    {
        frames = 1U;
    }
    return frames;
}

uint32_t sample_capture_model_tile_cache_capacity_frames(void)
{
    return SAMPLE_CAPTURE_EDITOR_CACHE_FRAMES;
}

uint8_t sample_capture_model_view_uses_tile_cache(uint32_t frame_count)
{
    return (uint8_t)((frame_count != 0U)
            && (frame_count <= SAMPLE_CAPTURE_EDITOR_CACHE_FRAMES));
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
    sample_capture_editor_cache_reset();
    sample_capture_global_overview_reset();
    memset(&g_sample_capture.wave_cache_handle, 0, sizeof(g_sample_capture.wave_cache_handle));
    g_sample_capture.wave_cache_ready = 0U;
    g_sample_capture.wave_cache_retry_countdown = 0U;
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

static int16_t sample_capture_line_sample_at(const int16_t *frames,
                                             uint32_t frame_count,
                                             uint32_t point,
                                             uint32_t point_count);
static void sample_capture_line_accumulate_frame(uint32_t rel_frame, int16_t sample);
static int16_t sample_capture_line_point_from_accum(uint16_t point);

static int16_t sample_capture_line_point_from_values(uint16_t point,
                                                     uint16_t point_count,
                                                     int16_t min_v,
                                                     int16_t max_v,
                                                     int16_t first_v,
                                                     int16_t last_v)
{
    int16_t point_v = first_v;
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
    if((point > 0U) && (point < (uint16_t)(point_count - 1U))
            && (sample_capture_abs_i16(point_v) < sample_capture_abs_i16(last_v)))
    {
        point_v = last_v;
    }
    return point_v;
}

static void sample_capture_editor_cache_reset(void)
{
    memset(&g_sample_capture_editor_cache, 0, sizeof(g_sample_capture_editor_cache));
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        g_sample_capture_editor_cache.tiles[i].state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.fill_started_logged = 0U;
#endif
}

static void sample_capture_global_overview_reset(void)
{
    memset(&g_sample_capture_global_overview, 0, sizeof(g_sample_capture_global_overview));
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_EMPTY;
}

static uint8_t sample_capture_global_overview_path_matches(void)
{
    return (uint8_t)((g_sample_capture.state.temp_path[0] != '\0')
            && (strncmp(g_sample_capture_global_overview.path,
                        g_sample_capture.state.temp_path,
                        SAMPLE_CAPTURE_PATH_MAX) == 0));
}

static void sample_capture_global_overview_request(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recorded_frames == 0U)
            || (g_sample_capture.state.temp_path[0] == '\0'))
    {
        return;
    }
    if((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames))
    {
        return;
    }
    if((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames))
    {
        return;
    }

    sample_capture_copy_path(g_sample_capture_global_overview.path,
                             g_sample_capture.state.temp_path);
    g_sample_capture_global_overview.frame_count = g_sample_capture.state.recorded_frames;
    g_sample_capture_global_overview.point_count =
        (g_sample_capture.state.recorded_frames < SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS)
            ? (uint16_t)g_sample_capture.state.recorded_frames
            : (uint16_t)SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS;
    if(g_sample_capture_global_overview.point_count == 0U)
    {
        g_sample_capture_global_overview.point_count = 1U;
    }
    g_sample_capture_global_overview.build_next_frame = 0U;
    g_sample_capture_global_overview.peak = 0U;
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING;
    g_sample_capture_global_overview.generation++;
    for(uint16_t i = 0U; i < SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS; ++i)
    {
        g_sample_capture_global_overview_points[i].min = 32767;
        g_sample_capture_global_overview_points[i].max = (int16_t)-32768;
    }
}

static uint8_t sample_capture_global_overview_can_build(void)
{
    if((g_sample_capture_global_overview.state != SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILDING)
            || (g_sample_capture_global_overview.path[0] == '\0')
            || (g_sample_capture_global_overview.frame_count == 0U)
            || (g_sample_capture_global_overview.point_count == 0U)
            || (g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (sample_capture_global_overview_path_matches() == 0U))
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

static void sample_capture_global_overview_finish(void)
{
    uint16_t peak = 0U;
    for(uint16_t i = 0U; i < g_sample_capture_global_overview.point_count; ++i)
    {
        if(g_sample_capture_global_overview_points[i].min == 32767
                && g_sample_capture_global_overview_points[i].max == (int16_t)-32768)
        {
            g_sample_capture_global_overview_points[i].min = 0;
            g_sample_capture_global_overview_points[i].max = 0;
        }
        const uint16_t abs_min =
            sample_capture_abs_i16(g_sample_capture_global_overview_points[i].min);
        const uint16_t abs_max =
            sample_capture_abs_i16(g_sample_capture_global_overview_points[i].max);
        if(abs_min > peak)
        {
            peak = abs_min;
        }
        if(abs_max > peak)
        {
            peak = abs_max;
        }
    }
    g_sample_capture_global_overview.peak = peak;
    g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("GOVERVIEW DONE points=%u frames=%lu peak=%u\r\n",
                             (unsigned)g_sample_capture_global_overview.point_count,
                             (unsigned long)g_sample_capture_global_overview.frame_count,
                             (unsigned)peak);
#endif
}

static void sample_capture_global_overview_service(void)
{
    if(sample_capture_global_overview_can_build() == 0U)
    {
        return;
    }

    uint32_t frames_left = g_sample_capture_global_overview.frame_count
        - g_sample_capture_global_overview.build_next_frame;
    if(frames_left == 0U)
    {
        sample_capture_global_overview_finish();
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
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
    if(f_open(&fp, g_sample_capture_global_overview.path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    if(f_lseek(&fp, SAMPLE_CAPTURE_WAV_DATA_OFFSET
            + (g_sample_capture_global_overview.build_next_frame
               * MULTI_RECORD_WRITER_BYTES_PER_FRAME)) != FR_OK)
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
        const uint32_t frame_index = g_sample_capture_global_overview.build_next_frame + i;
        uint32_t point = (uint32_t)(((uint64_t)frame_index
                * (uint64_t)g_sample_capture_global_overview.point_count)
            / (uint64_t)g_sample_capture_global_overview.frame_count);
        if(point >= g_sample_capture_global_overview.point_count)
        {
            point = g_sample_capture_global_overview.point_count - 1U;
        }

        const uint8_t *frame = &g_sample_capture_detail_buf[i * MULTI_RECORD_WRITER_BYTES_PER_FRAME];
        const int16_t l = sample_capture_pcm24le_to_waveform_i16(frame);
        const int16_t r = sample_capture_pcm24le_to_waveform_i16(&frame[3]);
        const int16_t sample_min = (l < r) ? l : r;
        const int16_t sample_max = (l > r) ? l : r;
        if(sample_min < g_sample_capture_global_overview_points[point].min)
        {
            g_sample_capture_global_overview_points[point].min = sample_min;
        }
        if(sample_max > g_sample_capture_global_overview_points[point].max)
        {
            g_sample_capture_global_overview_points[point].max = sample_max;
        }
    }

    g_sample_capture_global_overview.build_next_frame += frames_read;
    ok = 1U;
    if((frames_read == 0U)
            || (g_sample_capture_global_overview.build_next_frame
                >= g_sample_capture_global_overview.frame_count))
    {
        sample_capture_global_overview_finish();
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    if(ok == 0U)
    {
        g_sample_capture_global_overview.state = SAMPLE_CAPTURE_GLOBAL_OVERVIEW_ERROR;
    }
}

static uint16_t sample_capture_editor_level_count(uint32_t frames, uint32_t block_frames)
{
    if((frames == 0U) || (block_frames == 0U))
    {
        return 0U;
    }
    return (uint16_t)((frames + block_frames - 1U) / block_frames);
}

static void sample_capture_editor_tile_levels_reset(uint8_t tile_index)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    memset(g_sample_capture_editor_level0[tile_index], 0, sizeof(g_sample_capture_editor_level0[tile_index]));
    memset(g_sample_capture_editor_level1[tile_index], 0, sizeof(g_sample_capture_editor_level1[tile_index]));
    memset(g_sample_capture_editor_level2[tile_index], 0, sizeof(g_sample_capture_editor_level2[tile_index]));
    g_sample_capture_editor_cache.tiles[tile_index].level0_count = 0U;
    g_sample_capture_editor_cache.tiles[tile_index].level1_count = 0U;
    g_sample_capture_editor_cache.tiles[tile_index].level2_count = 0U;
}

static void sample_capture_editor_level_accumulate(sample_capture_editor_level_point_t *level,
                                                   uint16_t level_count,
                                                   uint32_t block_frames,
                                                   uint32_t frame,
                                                   int16_t sample)
{
    if((level == 0) || (block_frames == 0U))
    {
        return;
    }
    const uint32_t idx = frame / block_frames;
    if(idx >= level_count)
    {
        return;
    }
    sample_capture_editor_level_point_t *const point = &level[idx];
    if((frame % block_frames) == 0U)
    {
        point->min = sample;
        point->max = sample;
        point->first = sample;
        point->last = sample;
        return;
    }
    if(sample < point->min)
    {
        point->min = sample;
    }
    if(sample > point->max)
    {
        point->max = sample;
    }
    point->last = sample;
}

static void sample_capture_editor_tile_levels_begin(uint8_t tile_index, uint32_t frame_count)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    sample_capture_editor_tile_levels_reset(tile_index);
    g_sample_capture_editor_cache.tiles[tile_index].level0_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES);
    g_sample_capture_editor_cache.tiles[tile_index].level1_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES);
    g_sample_capture_editor_cache.tiles[tile_index].level2_count =
        sample_capture_editor_level_count(frame_count,
                                          SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES);
}

static void sample_capture_editor_tile_level_accumulate(uint8_t tile_index,
                                                        uint32_t frame,
                                                        int16_t sample)
{
    if(tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        return;
    }
    sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[tile_index];
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level0[tile_index],
                                               tile->level0_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES,
                                               frame,
                                               sample);
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level1[tile_index],
                                               tile->level1_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES,
                                               frame,
                                               sample);
    sample_capture_editor_level_accumulate(g_sample_capture_editor_level2[tile_index],
                                               tile->level2_count,
                                               SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES,
                                               frame,
                                               sample);
}

static uint8_t sample_capture_editor_cache_path_matches(void)
{
    return (uint8_t)((g_sample_capture.state.temp_path[0] != '\0')
            && (strncmp(g_sample_capture_editor_cache.path,
                        g_sample_capture.state.temp_path,
                        SAMPLE_CAPTURE_PATH_MAX) == 0));
}

static uint32_t sample_capture_editor_tile_end(const sample_capture_editor_audio_tile_t *tile)
{
    if(tile == 0)
    {
        return 0U;
    }
    return tile->start_frame + tile->frame_count;
}

static int8_t sample_capture_editor_find_ready_tile(uint32_t frame)
{
    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        return -1;
    }
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_READY)
                && (frame >= tile->start_frame)
                && (frame < sample_capture_editor_tile_end(tile)))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static int8_t sample_capture_editor_find_any_tile(uint32_t tile_start)
{
    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        return -1;
    }
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY)
                && (tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_STALE)
                && (tile->start_frame == tile_start))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t sample_capture_editor_cache_covers(uint32_t start_frame,
                                                  uint32_t frame_count)
{
    if((sample_capture_editor_cache_path_matches() == 0U) || (frame_count == 0U))
    {
        return 0U;
    }
    uint32_t cursor = start_frame;
    const uint32_t end_frame = start_frame + frame_count;
    if(end_frame < start_frame)
    {
        return 0U;
    }
    while(cursor < end_frame)
    {
        const int8_t idx = sample_capture_editor_find_ready_tile(cursor);
        if(idx < 0)
        {
            return 0U;
        }
        const uint32_t tile_end = sample_capture_editor_tile_end(&g_sample_capture_editor_cache.tiles[(uint8_t)idx]);
        if(tile_end <= cursor)
        {
            return 0U;
        }
        cursor = (tile_end < end_frame) ? tile_end : end_frame;
    }
    return 1U;
}

static uint32_t sample_capture_editor_align_tile_start(uint32_t frame)
{
    return (frame / SAMPLE_CAPTURE_EDITOR_TILE_FRAMES) * SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
}

static uint8_t sample_capture_editor_tile_start_in_list(uint32_t tile_start,
                                                        const uint32_t *list,
                                                        uint8_t count)
{
    for(uint8_t i = 0U; i < count; ++i)
    {
        if(list[i] == tile_start)
        {
            return 1U;
        }
    }
    return 0U;
}

static void sample_capture_editor_add_tile_request(uint32_t tile_start,
                                                   uint32_t *list,
                                                   uint8_t *count)
{
    if((list == 0) || (count == 0) || (*count >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
            || (tile_start >= g_sample_capture.state.recorded_frames)
            || (sample_capture_editor_tile_start_in_list(tile_start, list, *count) != 0U))
    {
        return;
    }
    list[*count] = tile_start;
    (*count)++;
}

static int8_t sample_capture_editor_pick_tile_slot(uint32_t tile_start, uint32_t focus);

static uint8_t sample_capture_editor_queue_tile_limited(uint32_t tile_start,
                                                        uint32_t focus,
                                                        uint8_t *queued_count)
{
    if((queued_count != 0) && (*queued_count >= SAMPLE_CAPTURE_EDITOR_TILE_REQUEST_MAX))
    {
        return 0U;
    }
    if(sample_capture_editor_find_any_tile(tile_start) >= 0)
    {
        return 0U;
    }
    const int8_t slot = sample_capture_editor_pick_tile_slot(tile_start, focus);
    if(slot < 0)
    {
        return 0U;
    }
    sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[(uint8_t)slot];
    tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_LOADING;
    tile->start_frame = tile_start;
    tile->frame_count = g_sample_capture.state.recorded_frames - tile_start;
    if(tile->frame_count > SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        tile->frame_count = SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    }
    tile->load_next_frame = 0U;
    tile->generation = ++g_sample_capture_editor_cache.generation;
    sample_capture_editor_tile_levels_begin((uint8_t)slot, tile->frame_count);
    if(queued_count != 0)
    {
        (*queued_count)++;
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_request_count++;
    sample_capture_debug_log("OLD_AUDIO_TILE_REQ slot=%u start=%lu frames=%lu focus=%lu\r\n",
                             (unsigned)(uint8_t)slot,
                             (unsigned long)tile->start_frame,
                             (unsigned long)tile->frame_count,
                             (unsigned long)focus);
#endif
    return 1U;
}

static uint32_t sample_capture_editor_distance_to_focus(uint32_t tile_start, uint32_t focus)
{
    if(focus < tile_start)
    {
        return tile_start - focus;
    }
    const uint32_t tile_end = tile_start + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    if(focus >= tile_end)
    {
        return focus - tile_end + 1U;
    }
    return 0U;
}

static int8_t sample_capture_editor_pick_tile_slot(uint32_t tile_start, uint32_t focus)
{
    int8_t candidate = -1;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY)
                || (tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_STALE))
        {
            return (int8_t)i;
        }
    }

    uint32_t worst_distance = 0U;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        const sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if(tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
        {
            continue;
        }
        const uint32_t distance = sample_capture_editor_distance_to_focus(tile->start_frame, focus);
        if((candidate < 0) || (distance > worst_distance))
        {
            candidate = (int8_t)i;
            worst_distance = distance;
        }
    }
    (void)tile_start;
    return candidate;
}

static void sample_capture_editor_cache_request(uint32_t view_start_frame,
                                                uint32_t view_frames)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (g_sample_capture.state.recorded_frames == 0U)
            || (view_frames == 0U))
    {
        return;
    }

    if(sample_capture_editor_cache_path_matches() == 0U)
    {
        sample_capture_editor_cache_reset();
        sample_capture_copy_path(g_sample_capture_editor_cache.path,
                                 g_sample_capture.state.temp_path);
    }

    const uint32_t view_end = view_start_frame + view_frames;
    uint32_t focus = view_start_frame + (view_frames / 2U);
    if((view_end < view_start_frame) || (focus >= g_sample_capture.state.recorded_frames))
    {
        focus = g_sample_capture.state.recorded_frames - 1U;
    }
    g_sample_capture_editor_cache.focus_frame = focus;
    g_sample_capture_editor_cache.last_view_start_frame = view_start_frame;
    g_sample_capture_editor_cache.last_view_frames = view_frames;

    uint32_t required[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint32_t keep[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint32_t optional[SAMPLE_CAPTURE_EDITOR_TILE_COUNT];
    uint8_t required_count = 0U;
    uint8_t keep_count = 0U;
    uint8_t optional_count = 0U;
    uint8_t queued_count = 0U;

    uint32_t visible_tiles = (view_frames + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES - 1U)
        / SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    if(visible_tiles == 0U)
    {
        visible_tiles = 1U;
    }
    if(visible_tiles > SAMPLE_CAPTURE_EDITOR_TILE_COUNT)
    {
        visible_tiles = SAMPLE_CAPTURE_EDITOR_TILE_COUNT;
    }

    const uint32_t focus_tile = sample_capture_editor_align_tile_start(focus);
    sample_capture_editor_add_tile_request(focus_tile, required, &required_count);
    sample_capture_editor_add_tile_request(focus_tile, keep, &keep_count);

    uint32_t tile_start = sample_capture_editor_align_tile_start(view_start_frame);
    for(uint32_t i = 0U; (i < visible_tiles) && (required_count < SAMPLE_CAPTURE_EDITOR_TILE_COUNT); ++i)
    {
        sample_capture_editor_add_tile_request(tile_start, required, &required_count);
        sample_capture_editor_add_tile_request(tile_start, keep, &keep_count);
        if(tile_start > (0xFFFFFFFFUL - SAMPLE_CAPTURE_EDITOR_TILE_FRAMES))
        {
            break;
        }
        tile_start += SAMPLE_CAPTURE_EDITOR_TILE_FRAMES;
    }

    if(view_start_frame >= SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        const uint32_t left_tile = sample_capture_editor_align_tile_start(view_start_frame - 1U);
        sample_capture_editor_add_tile_request(left_tile, keep, &keep_count);
        sample_capture_editor_add_tile_request(left_tile, optional, &optional_count);
    }
    const uint32_t right_tile = sample_capture_editor_align_tile_start(view_end);
    sample_capture_editor_add_tile_request(right_tile, keep, &keep_count);
    sample_capture_editor_add_tile_request(right_tile, optional, &optional_count);
    if(focus_tile >= SAMPLE_CAPTURE_EDITOR_TILE_FRAMES)
    {
        sample_capture_editor_add_tile_request(focus_tile - SAMPLE_CAPTURE_EDITOR_TILE_FRAMES,
                                              keep,
                                              &keep_count);
    }
    sample_capture_editor_add_tile_request(focus_tile + SAMPLE_CAPTURE_EDITOR_TILE_FRAMES,
                                          keep,
                                          &keep_count);

    for(uint8_t radius = 2U; radius <= SAMPLE_CAPTURE_EDITOR_TILE_KEEP_RADIUS; ++radius)
    {
        const uint32_t delta = SAMPLE_CAPTURE_EDITOR_TILE_FRAMES * (uint32_t)radius;
        if(focus_tile >= delta)
        {
            sample_capture_editor_add_tile_request(focus_tile - delta, keep, &keep_count);
        }
        sample_capture_editor_add_tile_request(focus_tile + delta, keep, &keep_count);
    }

    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if((tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
                && (sample_capture_editor_tile_start_in_list(tile->start_frame,
                                                            keep,
                                                            keep_count) == 0U))
        {
            tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_STALE;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
            sample_capture_debug_log("OLD_AUDIO_TILE_DROP slot=%u start=%lu\r\n",
                                     (unsigned)i,
                                     (unsigned long)tile->start_frame);
#endif
        }
    }

    for(uint8_t i = 0U; i < required_count; ++i)
    {
        (void)sample_capture_editor_queue_tile_limited(required[i], focus, &queued_count);
    }
    for(uint8_t i = 0U; i < optional_count; ++i)
    {
        (void)sample_capture_editor_queue_tile_limited(optional[i], focus, &queued_count);
    }
}

static uint8_t sample_capture_editor_cache_can_fill(uint8_t *out_reason)
{
    if(out_reason != 0)
    {
        *out_reason = 0U;
    }
    if((g_sample_capture_editor_cache.path[0] == '\0')
            || (g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (sample_capture_editor_cache_path_matches() == 0U))
    {
        return 0U;
    }
    if(multi_record_writer_any_active() != 0U)
    {
        if(out_reason != 0) { *out_reason = 1U; }
        return 0U;
    }
    if(looper_storage_raw_export_is_active() != 0U)
    {
        if(out_reason != 0) { *out_reason = 2U; }
        return 0U;
    }
    if(sd_preview_is_active() != 0U)
    {
        if(out_reason != 0) { *out_reason = 3U; }
        return 0U;
    }
    if(pattern_load_is_pending() != 0U)
    {
        if(out_reason != 0) { *out_reason = 4U; }
        return 0U;
    }
    if(sample_cache_has_pending_sd_work() != 0U)
    {
        if(out_reason != 0) { *out_reason = 5U; }
        return 0U;
    }
    return 1U;
}

static int8_t sample_capture_editor_next_loading_tile(void)
{
    int8_t best = -1;
    uint32_t best_distance = 0xFFFFFFFFUL;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        sample_capture_editor_audio_tile_t *const tile = &g_sample_capture_editor_cache.tiles[i];
        if(tile->state == SAMPLE_CAPTURE_EDITOR_CACHE_STALE)
        {
            tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
        }
        if(tile->state != SAMPLE_CAPTURE_EDITOR_CACHE_LOADING)
        {
            continue;
        }
        const uint32_t distance =
            sample_capture_editor_distance_to_focus(tile->start_frame,
                                                    g_sample_capture_editor_cache.focus_frame);
        if(distance < best_distance)
        {
            best = (int8_t)i;
            best_distance = distance;
        }
    }
    return best;
}

static void sample_capture_editor_cache_service(void)
{
    const int8_t load_idx = sample_capture_editor_next_loading_tile();
    if(load_idx < 0)
    {
        return;
    }

    uint8_t block_reason = 0U;
    if(sample_capture_editor_cache_can_fill(&block_reason) == 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        if(block_reason == 1U) { g_sample_capture_debug.cache_block_writer_count++; }
        else if(block_reason == 2U) { g_sample_capture_debug.cache_block_export_count++; }
        else if(block_reason == 3U) { g_sample_capture_debug.cache_block_preview_count++; }
        else if(block_reason == 4U) { g_sample_capture_debug.cache_block_pattern_count++; }
        else if(block_reason == 5U) { g_sample_capture_debug.cache_block_sample_count++; }
#endif
        return;
    }

    sample_capture_editor_audio_tile_t *const tile =
        &g_sample_capture_editor_cache.tiles[(uint8_t)load_idx];

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t service_start_ms = HAL_GetTick();
    g_sample_capture_debug.fill_passes++;
    if(g_sample_capture_debug.fill_started_logged == 0U)
    {
        g_sample_capture_debug.fill_started_logged = 1U;
        g_sample_capture_debug.fill_start_ms = service_start_ms;
        sample_capture_debug_log("OLD_AUDIO_TILE_START slot=%u start=%lu frames=%lu focus=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->frame_count,
                                 (unsigned long)g_sample_capture_editor_cache.focus_frame);
    }
#endif

    uint32_t frames_left = tile->frame_count - tile->load_next_frame;
    if(frames_left == 0U)
    {
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_READY;
        return;
    }
    if(frames_left > SAMPLE_CAPTURE_EDITOR_TILE_BUILD_CHUNK_FRAMES)
    {
        frames_left = SAMPLE_CAPTURE_EDITOR_TILE_BUILD_CHUNK_FRAMES;
    }

    if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_EDITOR_CACHE) == 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        g_sample_capture_debug.cache_gate_busy_count++;
#endif
        return;
    }

    FIL fp;
    uint8_t file_open = 0U;
    uint8_t ok = 0U;
    if(sd_access_fs_mount_if_needed() == 0U)
    {
        goto done;
    }
    if(f_open(&fp, g_sample_capture_editor_cache.path, FA_READ) != FR_OK)
    {
        goto done;
    }
    file_open = 1U;

    const uint32_t absolute_frame = tile->start_frame + tile->load_next_frame;
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
        const uint32_t rel = tile->load_next_frame + i;
        const int16_t mono = (int16_t)(((int32_t)l + (int32_t)r) / 2);
        g_sample_capture_editor_audio[(uint8_t)load_idx][rel] = mono;
        sample_capture_editor_tile_level_accumulate((uint8_t)load_idx, rel, mono);
    }

    tile->load_next_frame += frames_read;
    ok = 1U;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_chunks++;
    g_sample_capture_debug.fill_chunks++;
#endif
    if((frames_read == 0U)
            || (tile->load_next_frame >= tile->frame_count))
    {
        tile->frame_count = tile->load_next_frame;
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_READY;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        const uint32_t elapsed = HAL_GetTick() - g_sample_capture_debug.fill_start_ms;
        sample_capture_debug_log("OLD_AUDIO_TILE_DONE slot=%u start=%lu frames=%lu chunks=%lu passes=%lu gate_busy=%lu ms=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->frame_count,
                                 (unsigned long)g_sample_capture_debug.fill_chunks,
                                 (unsigned long)g_sample_capture_debug.fill_passes,
                                 (unsigned long)g_sample_capture_debug.cache_gate_busy_count,
                                 (unsigned long)elapsed);
        g_sample_capture_debug.fill_chunks = 0U;
        g_sample_capture_debug.fill_passes = 0U;
        g_sample_capture_debug.cache_gate_busy_count = 0U;
        g_sample_capture_debug.fill_started_logged = 0U;
#endif
    }

done:
    if(file_open != 0U)
    {
        (void)f_close(&fp);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_EDITOR_CACHE);
    if(ok == 0U)
    {
        tile->state = SAMPLE_CAPTURE_EDITOR_CACHE_EMPTY;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        sample_capture_debug_log("OLD_AUDIO_TILE_ERR slot=%u start=%lu loaded=%lu req=%lu\r\n",
                                 (unsigned)(uint8_t)load_idx,
                                 (unsigned long)tile->start_frame,
                                 (unsigned long)tile->load_next_frame,
                                 (unsigned long)tile->frame_count);
#endif
    }
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t service_ms = HAL_GetTick() - service_start_ms;
    g_sample_capture_debug.fill_last_ms = service_ms;
    if(service_ms > g_sample_capture_debug.fill_max_ms)
    {
        g_sample_capture_debug.fill_max_ms = service_ms;
    }
#endif
}

static int16_t sample_capture_editor_sample_at(uint32_t frame)
{
    const int8_t idx = sample_capture_editor_find_ready_tile(frame);
    if(idx < 0)
    {
        return 0;
    }
    const sample_capture_editor_audio_tile_t *const tile =
        &g_sample_capture_editor_cache.tiles[(uint8_t)idx];
    return g_sample_capture_editor_audio[(uint8_t)idx][frame - tile->start_frame];
}

static void sample_capture_editor_range_from_level(uint8_t tile_index,
                                                   uint32_t rel_start,
                                                   uint32_t rel_end,
                                                   uint32_t block_frames,
                                                   const sample_capture_editor_level_point_t *level,
                                                   uint16_t level_count,
                                                   uint8_t *seen,
                                                   int16_t *min_v,
                                                   int16_t *max_v,
                                                   int16_t *first_v,
                                                   int16_t *last_v)
{
    if((level == 0) || (seen == 0) || (min_v == 0) || (max_v == 0)
            || (first_v == 0) || (last_v == 0) || (rel_end <= rel_start)
            || (block_frames == 0U) || (level_count == 0U)
            || (tile_index >= SAMPLE_CAPTURE_EDITOR_TILE_COUNT))
    {
        return;
    }
    uint32_t idx0 = rel_start / block_frames;
    uint32_t idx1 = (rel_end + block_frames - 1U) / block_frames;
    if(idx0 >= level_count)
    {
        return;
    }
    if(idx1 > level_count)
    {
        idx1 = level_count;
    }
    for(uint32_t idx = idx0; idx < idx1; ++idx)
    {
        const sample_capture_editor_level_point_t *const p = &level[idx];
        if(*seen == 0U)
        {
            *min_v = p->min;
            *max_v = p->max;
            *first_v = p->first;
            *last_v = p->last;
            *seen = 1U;
        }
        else
        {
            if(p->min < *min_v) { *min_v = p->min; }
            if(p->max > *max_v) { *max_v = p->max; }
            *last_v = p->last;
        }
    }
}

static uint8_t sample_capture_editor_accumulate_range(uint32_t start_frame,
                                                      uint32_t end_frame,
                                                      uint32_t samples_per_point,
                                                      int16_t *min_v,
                                                      int16_t *max_v,
                                                      int16_t *first_v,
                                                      int16_t *last_v)
{
    uint8_t seen = 0U;
    uint32_t cursor = start_frame;
    while(cursor < end_frame)
    {
        const int8_t idx = sample_capture_editor_find_ready_tile(cursor);
        if(idx < 0)
        {
            return 0U;
        }
        const sample_capture_editor_audio_tile_t *const tile =
            &g_sample_capture_editor_cache.tiles[(uint8_t)idx];
        uint32_t span_end = sample_capture_editor_tile_end(tile);
        if(span_end > end_frame)
        {
            span_end = end_frame;
        }
        const uint32_t rel_start = cursor - tile->start_frame;
        const uint32_t rel_end = span_end - tile->start_frame;
        if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL2_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level2[(uint8_t)idx],
                                                   tile->level2_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL1_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level1[(uint8_t)idx],
                                                   tile->level1_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else if(samples_per_point >= SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES)
        {
            sample_capture_editor_range_from_level((uint8_t)idx, rel_start, rel_end,
                                                   SAMPLE_CAPTURE_EDITOR_LEVEL0_BLOCK_FRAMES,
                                                   g_sample_capture_editor_level0[(uint8_t)idx],
                                                   tile->level0_count,
                                                   &seen, min_v, max_v, first_v, last_v);
        }
        else
        {
            for(uint32_t frame = cursor; frame < span_end; ++frame)
            {
                const int16_t sample = sample_capture_editor_sample_at(frame);
                if(seen == 0U)
                {
                    *min_v = sample;
                    *max_v = sample;
                    *first_v = sample;
                    *last_v = sample;
                    seen = 1U;
                }
                else
                {
                    if(sample < *min_v) { *min_v = sample; }
                    if(sample > *max_v) { *max_v = sample; }
                    *last_v = sample;
                }
            }
        }
        cursor = span_end;
    }
    return seen;
}

static void sample_capture_line_publish_from_editor_cache(uint32_t start_frame,
                                                          uint32_t frame_count,
                                                          uint16_t points)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t eline_start_ms = HAL_GetTick();
    const char *eline_src = "OLD_AUDIO_TILE";
#endif
    if((points == 0U) || (points > SAMPLE_CAPTURE_LINE_POINTS)
            || (sample_capture_editor_cache_covers(start_frame, frame_count) == 0U))
    {
        return;
    }

    uint16_t line_peak = 0U;
    const uint32_t samples_per_point = (frame_count + (uint32_t)points - 1U) / (uint32_t)points;

    g_sample_capture_line_hot.valid = 1U;
    g_sample_capture_line_hot.start_frame = start_frame;
    g_sample_capture_line_hot.frames = frame_count;
    g_sample_capture_line_hot.count = points;
    g_sample_capture_line_hot.requested = 0U;
    g_sample_capture_line_hot.building = 0U;
    g_sample_capture_line_hot.build_next_frame = 0U;

    for(uint16_t point = 0U; point < points; ++point)
    {
        uint32_t frame0 = (uint32_t)(((uint64_t)point * (uint64_t)frame_count)
            / (uint64_t)points);
        uint32_t frame1 = (uint32_t)(((uint64_t)(point + 1U) * (uint64_t)frame_count)
            / (uint64_t)points);
        if(frame1 <= frame0)
        {
            frame1 = frame0 + 1U;
        }
        frame0 += start_frame;
        frame1 += start_frame;
        int16_t min_v = 0;
        int16_t max_v = 0;
        int16_t first_v = 0;
        int16_t last_v = 0;
        if(sample_capture_editor_accumulate_range(frame0, frame1, samples_per_point,
                                                  &min_v, &max_v, &first_v, &last_v) == 0U)
        {
            g_sample_capture_line_hot.valid = 0U;
            return;
        }
        const int16_t v = sample_capture_line_point_from_values(point,
                                                                points,
                                                                min_v,
                                                                max_v,
                                                                first_v,
                                                                last_v);
        g_sample_capture_line_hot.points[point] = v;
        const uint16_t abs_v = sample_capture_abs_i16(v);
        if(abs_v > line_peak)
        {
            line_peak = abs_v;
        }
    }
    g_sample_capture_line_hot.peak = line_peak;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint32_t eline_ms = HAL_GetTick() - eline_start_ms;
    g_sample_capture_debug.eline_count++;
    g_sample_capture_debug.eline_last_ms = eline_ms;
    if(eline_ms > g_sample_capture_debug.eline_max_ms)
    {
        g_sample_capture_debug.eline_max_ms = eline_ms;
    }
    if(eline_ms != 0U)
    {
        sample_capture_debug_log("ELINE src=%s points=%u vf=%lu spp=%lu ms=%lu peak=%u\r\n",
                                 eline_src,
                                 (unsigned)points,
                                 (unsigned long)frame_count,
                                 (unsigned long)samples_per_point,
                                 (unsigned long)eline_ms,
                                 (unsigned)line_peak);
    }
#endif
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
    return sample_capture_line_point_from_values(point,
                                                 g_sample_capture_line_hot.build_points,
                                                 min_v,
                                                 max_v,
                                                 g_sample_capture_line_hot.first[point],
                                                 last_v);
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

static uint32_t sample_capture_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if(v < lo)
    {
        return lo;
    }
    if(v > hi)
    {
        return hi;
    }
    return v;
}

static void sample_capture_clamp_edit_markers(void)
{
    if(g_sample_capture.state.recorded_frames == 0U)
    {
        g_sample_capture.state.edit_start_frame = 0U;
        g_sample_capture.state.edit_end_frame = 0U;
        g_sample_capture.state.edit_loop_start_frame = 0U;
        g_sample_capture.state.edit_loop_end_frame = 0U;
        g_sample_capture.state.edit_scroll_frame = 0U;
        return;
    }

    if(g_sample_capture.state.edit_end_frame == 0U)
    {
        g_sample_capture.state.edit_end_frame = 1U;
    }
    if(g_sample_capture.state.edit_end_frame > g_sample_capture.state.recorded_frames)
    {
        g_sample_capture.state.edit_end_frame = g_sample_capture.state.recorded_frames;
    }
    if(g_sample_capture.state.edit_start_frame >= g_sample_capture.state.edit_end_frame)
    {
        g_sample_capture.state.edit_start_frame = g_sample_capture.state.edit_end_frame - 1U;
    }
    if(g_sample_capture.state.edit_loop_end_frame == 0U)
    {
        g_sample_capture.state.edit_loop_end_frame = g_sample_capture.state.edit_end_frame;
    }
    g_sample_capture.state.edit_loop_start_frame =
        sample_capture_clamp_u32(g_sample_capture.state.edit_loop_start_frame,
                                 g_sample_capture.state.edit_start_frame,
                                 g_sample_capture.state.edit_end_frame - 1U);
    g_sample_capture.state.edit_loop_end_frame =
        sample_capture_clamp_u32(g_sample_capture.state.edit_loop_end_frame,
                                 g_sample_capture.state.edit_loop_start_frame + 1U,
                                 g_sample_capture.state.edit_end_frame);

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

static uint32_t sample_capture_clamp_scroll_for_visible(uint32_t start_frame,
                                                        uint32_t visible_frames)
{
    if((g_sample_capture.state.recorded_frames == 0U)
            || (visible_frames >= g_sample_capture.state.recorded_frames))
    {
        return 0U;
    }
    const uint32_t max_start = g_sample_capture.state.recorded_frames - visible_frames;
    return (start_frame > max_start) ? max_start : start_frame;
}

static void sample_capture_set_zoom_preserve_center(uint8_t next_zoom)
{
    const uint32_t old_visible =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    const uint32_t old_start =
        sample_capture_clamp_scroll_for_visible(g_sample_capture.state.edit_scroll_frame,
                                                old_visible);
    uint64_t center = (uint64_t)old_start + ((uint64_t)old_visible / 2ULL);
    if(center > g_sample_capture.state.recorded_frames)
    {
        center = g_sample_capture.state.recorded_frames;
    }

    g_sample_capture.state.edit_zoom = next_zoom;

    const uint32_t new_visible =
        sample_capture_model_visible_frames_for_zoom(g_sample_capture.state.recorded_frames,
                                                     g_sample_capture.state.edit_zoom);
    uint32_t new_start = 0U;
    if(new_visible < g_sample_capture.state.recorded_frames)
    {
        if(center > ((uint64_t)new_visible / 2ULL))
        {
            const uint64_t centered = center - ((uint64_t)new_visible / 2ULL);
            new_start = (centered > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)centered;
        }
        new_start = sample_capture_clamp_scroll_for_visible(new_start, new_visible);
    }
    g_sample_capture.state.edit_scroll_frame = new_start;
}

static uint8_t sample_capture_sign_crosses(int16_t a, int16_t b)
{
    return (uint8_t)(((a <= 0) && (b >= 0)) || ((a >= 0) && (b <= 0)));
}

static uint32_t sample_capture_add_saturate_u32(uint32_t a, uint32_t b)
{
    return (b > (0xFFFFFFFFUL - a)) ? 0xFFFFFFFFUL : (a + b);
}

static uint32_t sample_capture_snap_to_zcross(uint32_t current,
                                              uint32_t candidate,
                                              int16_t delta,
                                              uint32_t min_frame,
                                              uint32_t max_frame)
{
    if(g_sample_capture.state.edit_zcross_enabled == 0U)
    {
        return candidate;
    }

    if(min_frame > max_frame)
    {
        return candidate;
    }

    current = sample_capture_clamp_u32(current, min_frame, max_frame);
    candidate = sample_capture_clamp_u32(candidate, min_frame, max_frame);
    const uint8_t right = (delta > 0) ? 1U : 0U;
    const uint32_t candidate_dist =
        (candidate > current) ? (candidate - current) : (current - candidate);
    uint32_t search_span = SAMPLE_CAPTURE_ZCROSS_SEARCH_FRAMES;
    if(candidate_dist > search_span)
    {
        search_span = candidate_dist;
    }

    uint32_t search_start = min_frame;
    uint32_t search_end = max_frame;
    if(right != 0U)
    {
        search_start = sample_capture_add_saturate_u32(current,
                                                       SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES);
        search_end = sample_capture_add_saturate_u32(current, search_span);
    }
    else
    {
        search_start = (current > search_span) ? (current - search_span) : 0U;
        search_end = (current > SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES)
            ? (current - SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES)
            : 0U;
    }
    search_start = sample_capture_clamp_u32(search_start, min_frame, max_frame);
    search_end = sample_capture_clamp_u32(search_end, min_frame, max_frame);
    if(search_end < search_start)
    {
        return candidate;
    }

    uint32_t cover_start = search_start;
    if(cover_start > min_frame)
    {
        cover_start--;
    }
    if(sample_capture_editor_cache_covers(cover_start, (search_end - cover_start) + 1U) != 0U)
    {
        if(right != 0U)
        {
            int16_t prev = sample_capture_editor_sample_at(cover_start);
            for(uint32_t frame = cover_start + 1U; frame <= search_end; ++frame)
            {
                const int16_t cur = sample_capture_editor_sample_at(frame);
                if((frame >= search_start) && (sample_capture_sign_crosses(prev, cur) != 0U))
                {
                    return sample_capture_clamp_u32(frame, min_frame, max_frame);
                }
                prev = cur;
                if(frame == 0xFFFFFFFFUL)
                {
                    break;
                }
            }
        }
        else
        {
            for(uint32_t frame = search_end; frame > cover_start; --frame)
            {
                if(frame < search_start)
                {
                    continue;
                }
                const int16_t prev = sample_capture_editor_sample_at(frame - 1U);
                const int16_t cur = sample_capture_editor_sample_at(frame);
                if(sample_capture_sign_crosses(prev, cur) != 0U)
                {
                    return sample_capture_clamp_u32(frame, min_frame, max_frame);
                }
            }
        }
    }

    if((g_sample_capture_line_hot.valid != 0U)
            && (g_sample_capture_line_hot.count > 1U)
            && (g_sample_capture_line_hot.frames != 0U)
            && (search_start >= g_sample_capture_line_hot.start_frame)
            && (search_end <= (g_sample_capture_line_hot.start_frame + g_sample_capture_line_hot.frames)))
    {
        uint32_t best_frame = candidate;
        uint32_t best_dist = 0xFFFFFFFFUL;
        uint16_t best_abs = 0xFFFFU;
        for(uint16_t point = 0U; point < g_sample_capture_line_hot.count; ++point)
        {
            const uint32_t frame = g_sample_capture_line_hot.start_frame
                + (uint32_t)(((uint64_t)point * (uint64_t)g_sample_capture_line_hot.frames)
                    / (uint64_t)(g_sample_capture_line_hot.count - 1U));
            if((frame < search_start) || (frame > search_end))
            {
                continue;
            }
            const uint16_t abs_v = sample_capture_abs_i16(g_sample_capture_line_hot.points[point]);
            const uint32_t dist = (frame > current) ? (frame - current) : (current - frame);
            if((abs_v < best_abs) || ((abs_v == best_abs) && (dist < best_dist)))
            {
                best_frame = frame;
                best_dist = dist;
                best_abs = abs_v;
            }
        }
        if(best_dist != 0xFFFFFFFFUL)
        {
            return sample_capture_clamp_u32(best_frame, min_frame, max_frame);
        }
    }

    return candidate;
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

    if((sample_capture_has_route() == 0U)
            && (sample_capture_mic_source_available(g_sample_capture.state.arm) == 0U))
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
    g_sample_capture.trigger_pending = 0U;
    g_sample_capture.state.take_valid = 0U;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_RECORDING;
    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    g_sample_capture.last_take_notified = 0U;
    g_sample_capture.rec_edit_enter_deferred_services = 0U;
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
    sample_capture_debug_mark(REC_LIVE_DEBUG_TAKE_READY_ENTER, status, path, frames);
    if(frames == 0U)
    {
        sample_capture_set_audio_hook_enabled(0U);
        g_sample_capture.state.recording = 0U;
        g_sample_capture.state.armed_pending = 0U;
        g_sample_capture.trigger_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        sample_capture_live_summary_reset();
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_SD_IO);
        g_sample_capture.last_take_notified = 1U;
        return;
    }

    g_sample_capture.state.recording = 0U;
    g_sample_capture.state.armed_pending = 0U;
    g_sample_capture.trigger_pending = 0U;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    sample_capture_live_summary_reset();
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_LIVE_DONE frames=%lu\r\n", (unsigned long)frames);
    sample_capture_debug_log("WRITER_FINAL_READY path=%s frames=%lu\r\n",
                             path,
                             (unsigned long)frames);
#endif
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_EDIT_ENTER_REQUEST path=%s\r\n", path);
#endif
    sample_capture_debug_mark(REC_LIVE_DEBUG_REC_EDIT_ENTER_REQUEST, status, path, frames);
    g_sample_capture.state.take_valid = 1U;
    g_sample_capture.state.recorded_frames = frames;
    g_sample_capture.state.edit_start_frame = 0U;
    g_sample_capture.state.edit_end_frame = frames;
    g_sample_capture.state.edit_loop_start_frame = 0U;
    g_sample_capture.state.edit_loop_end_frame = frames;
    g_sample_capture.state.edit_zoom = 0U;
    g_sample_capture.state.edit_vzoom = SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT;
    g_sample_capture.state.edit_zcross_enabled = 0U;
    g_sample_capture.state.edit_scroll_frame = 0U;
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_REC_EDIT;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_REC_EDIT;
    sample_capture_copy_path(g_sample_capture.state.temp_path, path);
    sample_capture_detail_reset();
    sample_capture_global_overview_request();
    sample_capture_debug_mark(REC_LIVE_DEBUG_REC_EDIT_MODEL_INIT, status, path, frames);
    uint8_t cache_queued = 0U;
    if(sample_capture_path_is_temp(path) == 0U)
    {
        if(frames >= WAVEFORM_CACHE_PERSIST_MIN_FRAMES)
        {
            cache_queued = waveform_cache_request_for_wav_known_duration(
                path,
                WAVEFORM_CACHE_REASON_POST_AUDIO_REC,
                frames,
                MULTI_RECORD_WRITER_SAMPLE_RATE_HZ);
            g_sample_capture.wave_cache_retry_countdown = 32U;
        }
        else
        {
            g_sample_capture.wave_cache_retry_countdown = 0U;
        }
    }
    else
    {
        g_sample_capture.wave_cache_retry_countdown = 0U;
    }
    g_sample_capture.rec_edit_enter_deferred_services = 2U;
    g_sample_capture.rec_edit_first_render_pending = 1U;
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    sample_capture_debug_log("WAVEFORM_CACHE_%s path=%s\r\n",
                             (cache_queued != 0U) ? "JOB_QUEUED" : "DEFERRED",
                             path);
#else
    (void)cache_queued;
#endif
#if SAMPLE_CAPTURE_DEBUG_UART
    sample_capture_debug_log("REC_EDIT_ENTER_OK\r\n");
#endif
    sample_capture_debug_mark(REC_LIVE_DEBUG_REC_EDIT_ENTER_OK, status, path, frames);
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
    multi_record_writer_status_t status;
    if(sample_capture_get_status(&status) != 0U)
    {
        sample_capture_debug_mark(REC_LIVE_DEBUG_REC_LIVE_STOP_REQUESTED,
                                  &status,
                                  g_sample_capture.state.temp_path,
                                  status.frames_received);
    }
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

uint8_t sample_capture_recorder_is_active(void)
{
    /* The writer hook is disabled as soon as capture enters STOPPING. */
    return g_sample_capture.audio_hook_enabled;
}

void sample_capture_live_summary_reset(void)
{
    g_sample_capture_live_summary.peak_pcm24 = 0U;
    g_sample_capture_live_summary.block_sequence = 0U;
    g_sample_capture_live_summary.valid = 0U;
    g_sample_capture_live_summary.trigger_latched = 0U;
}

void sample_capture_live_publish_peak_from_irq(uint32_t peak_pcm24)
{
    if(peak_pcm24 > SAMPLE_CAPTURE_LIVE_PCM24_FULL_SCALE)
    {
        peak_pcm24 = SAMPLE_CAPTURE_LIVE_PCM24_FULL_SCALE;
    }
    g_sample_capture_live_summary.peak_pcm24 = peak_pcm24;
    g_sample_capture_live_summary.block_sequence++;
    g_sample_capture_live_summary.valid = 1U;

    if((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
            && (g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (g_sample_capture.trigger_pending == 0U)
            && (g_sample_capture_live_summary.trigger_latched == 0U)
            && (peak_pcm24 >= g_sample_capture.threshold_pcm24))
    {
        g_sample_capture_live_summary.trigger_latched = 1U;
    }
}

void sample_capture_live_latch_trigger_from_irq(void)
{
    g_sample_capture_live_summary.trigger_latched = 1U;
}

uint8_t sample_capture_live_take_trigger(void)
{
    const uint8_t latched = g_sample_capture_live_summary.trigger_latched;
    g_sample_capture_live_summary.trigger_latched = 0U;
    return latched;
}

void sample_capture_live_get_summary(sample_capture_live_summary_t *out_summary)
{
    if(out_summary == 0)
    {
        return;
    }
    out_summary->peak_pcm24 = g_sample_capture_live_summary.peak_pcm24;
    out_summary->block_sequence = g_sample_capture_live_summary.block_sequence;
    out_summary->valid = g_sample_capture_live_summary.valid;
    out_summary->trigger_latched = g_sample_capture_live_summary.trigger_latched;
}

void sample_capture_model_init(void)
{
    memset(&g_sample_capture, 0, sizeof(g_sample_capture));
    sample_capture_debug_reset();
    g_sample_capture.state.view = SAMPLE_CAPTURE_VIEW_AUDIO_REC;
    g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
    g_sample_capture.state.len_bars = 1U;
    g_sample_capture.state.quant = SAMPLE_CAPTURE_QUANT_NOW;
    g_sample_capture.state.threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT;
    g_sample_capture.threshold_pcm24 =
        sample_capture_threshold_dbfs_to_pcm24(SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT);
    g_sample_capture.state.mic_enabled = 1U;
    g_sample_capture.state.live_monitor_enabled = 0U;
    g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_IDLE;
    g_sample_capture.state.edit_vzoom = SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT;
    sample_capture_live_summary_reset();
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
    if(arm >= SAMPLE_CAPTURE_ARM_COUNT)
    {
        return 0U;
    }

    if(arm == SAMPLE_CAPTURE_ARM_OFF)
    {
        sample_capture_live_summary_reset();
        g_sample_capture.trigger_pending = 0U;
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
    if((sample_capture_has_route() == 0U)
            && (sample_capture_mic_source_available(arm) == 0U))
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_NO_ROUTE);
        return 0U;
    }

    sample_capture_live_summary_reset();
    g_sample_capture.trigger_pending = 0U;
    g_sample_capture.state.arm = arm;
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
    int16_t next = (int16_t)g_sample_capture.state.arm + ((delta > 0) ? 1 : -1);
    if(next < (int16_t)SAMPLE_CAPTURE_ARM_OFF)
    {
        next = (int16_t)SAMPLE_CAPTURE_ARM_COUNT - 1;
    }
    else if(next >= (int16_t)SAMPLE_CAPTURE_ARM_COUNT)
    {
        next = (int16_t)SAMPLE_CAPTURE_ARM_OFF;
    }
    return sample_capture_model_set_arm((sample_capture_arm_t)next);
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

uint8_t sample_capture_model_set_threshold_dbfs(int16_t threshold_dbfs)
{
    if(threshold_dbfs < SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN)
    {
        threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN;
    }
    if(threshold_dbfs > SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX)
    {
        threshold_dbfs = SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX;
    }
    g_sample_capture.state.threshold_dbfs = (int8_t)threshold_dbfs;
    g_sample_capture.threshold_pcm24 = sample_capture_threshold_dbfs_to_pcm24(threshold_dbfs);
    return 1U;
}

int8_t sample_capture_model_get_threshold_dbfs(void)
{
    return g_sample_capture.state.threshold_dbfs;
}

uint8_t sample_capture_model_step_threshold(int16_t delta)
{
    if(delta == 0)
    {
        return 0U;
    }
    return sample_capture_model_set_threshold_dbfs(
        (int16_t)g_sample_capture.state.threshold_dbfs + delta * SAMPLE_CAPTURE_THRESHOLD_DBFS_STEP);
}

uint8_t sample_capture_model_set_mic_enabled(uint8_t enabled)
{
    g_sample_capture.state.mic_enabled = (enabled != 0U) ? 1U : 0U;
    return 1U;
}

uint8_t sample_capture_model_mic_is_enabled(void)
{
    return g_sample_capture.state.mic_enabled;
}

uint8_t sample_capture_model_toggle_mic(void)
{
    return sample_capture_model_set_mic_enabled(
        (g_sample_capture.state.mic_enabled == 0U) ? 1U : 0U);
}

uint8_t sample_capture_model_set_live_monitor_enabled(uint8_t enabled)
{
    g_sample_capture.state.live_monitor_enabled = (enabled != 0U) ? 1U : 0U;
    return 1U;
}

uint8_t sample_capture_model_live_monitor_is_enabled(void)
{
    return g_sample_capture.state.live_monitor_enabled;
}

uint8_t sample_capture_model_live_bus_required(void)
{
    if(g_sample_capture.state.live_monitor_enabled != 0U)
    {
        return 1U;
    }

    return (uint8_t)((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
        && (g_sample_capture.state.armed_pending != 0U)
        && (g_sample_capture.state.recording == 0U));
}

uint8_t sample_capture_model_step_edit(uint8_t encoder, int16_t delta, uint8_t alt_held)
{
    if((delta == 0) || (g_sample_capture.state.take_valid == 0U))
    {
        return 0U;
    }

    const uint32_t coarse = (delta > 0) ? (uint32_t)delta : (uint32_t)(-delta);
    uint32_t marker_step = sample_capture_edit_marker_step();
    if((alt_held != 0U) && (encoder == 1U))
    {
        marker_step /= 8U;
        if(marker_step == 0U)
        {
            marker_step = 1U;
        }
    }
    const uint32_t step = coarse * marker_step;
    uint8_t changed = 0U;

    if((alt_held != 0U) && (encoder == 0U))
    {
        int16_t next = (int16_t)g_sample_capture.state.edit_vzoom + ((delta > 0) ? 1 : -1);
        if(next < 0)
        {
            next = 0;
        }
        if(next > (int16_t)SAMPLE_CAPTURE_EDIT_VZOOM_MAX)
        {
            next = (int16_t)SAMPLE_CAPTURE_EDIT_VZOOM_MAX;
        }
        g_sample_capture.state.edit_vzoom = (uint8_t)next;
        changed = 1U;
    }
    else if((alt_held != 0U) && (encoder == 2U))
    {
        const uint32_t current = g_sample_capture.state.edit_loop_start_frame;
        uint32_t next = current;
        if(delta > 0)
        {
            next = (step > (0xFFFFFFFFUL - next)) ? 0xFFFFFFFFUL : (next + step);
        }
        else
        {
            next = (step < next) ? (next - step) : 0U;
        }
        next = sample_capture_clamp_u32(next,
                                        g_sample_capture.state.edit_start_frame,
                                        g_sample_capture.state.edit_loop_end_frame - 1U);
        g_sample_capture.state.edit_loop_start_frame =
            sample_capture_snap_to_zcross(current,
                                          next,
                                          delta,
                                          g_sample_capture.state.edit_start_frame,
                                          g_sample_capture.state.edit_loop_end_frame - 1U);
        changed = 1U;
    }
    else if((alt_held != 0U) && (encoder == 3U))
    {
        const uint32_t current = g_sample_capture.state.edit_loop_end_frame;
        uint32_t next = current;
        if(delta > 0)
        {
            next = (step > (0xFFFFFFFFUL - next)) ? 0xFFFFFFFFUL : (next + step);
        }
        else
        {
            next = (step < next) ? (next - step) : 0U;
        }
        next = sample_capture_clamp_u32(next,
                                        g_sample_capture.state.edit_loop_start_frame + 1U,
                                        g_sample_capture.state.edit_end_frame);
        g_sample_capture.state.edit_loop_end_frame =
            sample_capture_snap_to_zcross(current,
                                          next,
                                          delta,
                                          g_sample_capture.state.edit_loop_start_frame + 1U,
                                          g_sample_capture.state.edit_end_frame);
        changed = 1U;
    }
    else if(encoder == 2U)
    {
        const uint32_t current = g_sample_capture.state.edit_start_frame;
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
        g_sample_capture.state.edit_start_frame =
            sample_capture_snap_to_zcross(current,
                                          g_sample_capture.state.edit_start_frame,
                                          delta,
                                          0U,
                                          g_sample_capture.state.edit_end_frame - 1U);
        changed = 1U;
    }
    else if(encoder == 3U)
    {
        const uint32_t current = g_sample_capture.state.edit_end_frame;
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
        g_sample_capture.state.edit_end_frame =
            sample_capture_snap_to_zcross(current,
                                          g_sample_capture.state.edit_end_frame,
                                          delta,
                                          g_sample_capture.state.edit_start_frame + 1U,
                                          g_sample_capture.state.recorded_frames);
        changed = 1U;
    }
    else if(encoder == 0U)
    {
        uint8_t next_zoom = g_sample_capture.state.edit_zoom;
        uint32_t zoom_step = coarse * 2U;
        if(coarse >= 4U)
        {
            zoom_step += coarse;
        }
        if(zoom_step == 0U)
        {
            zoom_step = 1U;
        }
        if(delta > 0)
        {
            if(zoom_step >= ((uint32_t)SAMPLE_CAPTURE_EDIT_ZOOM_MAX - (uint32_t)next_zoom))
            {
                next_zoom = SAMPLE_CAPTURE_EDIT_ZOOM_MAX;
            }
            else
            {
                next_zoom = (uint8_t)((uint32_t)next_zoom + zoom_step);
            }
        }
        else if(zoom_step >= next_zoom)
        {
            next_zoom = 0U;
        }
        else
        {
            next_zoom = (uint8_t)((uint32_t)next_zoom - zoom_step);
        }
        sample_capture_set_zoom_preserve_center(next_zoom);
        changed = 1U;
    }
    else if(encoder == 1U)
    {
        uint32_t scroll_unit = sample_capture_edit_scroll_step();
        if(alt_held != 0U)
        {
            scroll_unit /= 8U;
            if(scroll_unit == 0U)
            {
                scroll_unit = 1U;
            }
        }
        const uint32_t scroll_step = coarse * scroll_unit;
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
        sample_capture_clamp_edit_markers();
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

uint8_t sample_capture_model_global_overview_ready(void)
{
    return (uint8_t)((g_sample_capture_global_overview.state == SAMPLE_CAPTURE_GLOBAL_OVERVIEW_READY)
            && (sample_capture_global_overview_path_matches() != 0U)
            && (g_sample_capture_global_overview.frame_count == g_sample_capture.state.recorded_frames)
            && (g_sample_capture_global_overview.point_count != 0U));
}

uint16_t sample_capture_model_global_overview_peak(void)
{
    return sample_capture_model_global_overview_ready()
        ? g_sample_capture_global_overview.peak
        : 0U;
}

uint8_t sample_capture_model_global_overview_minmax(uint32_t start_frame,
                                                    uint32_t frame_count,
                                                    int16_t *out_min,
                                                    int16_t *out_max)
{
    if(out_min != 0)
    {
        *out_min = 0;
    }
    if(out_max != 0)
    {
        *out_max = 0;
    }
    if((sample_capture_model_global_overview_ready() == 0U)
            || (frame_count == 0U)
            || (start_frame >= g_sample_capture_global_overview.frame_count))
    {
        return 0U;
    }

    uint32_t end_frame = start_frame + frame_count;
    if((end_frame < start_frame) || (end_frame > g_sample_capture_global_overview.frame_count))
    {
        end_frame = g_sample_capture_global_overview.frame_count;
    }
    if(end_frame <= start_frame)
    {
        return 0U;
    }

    uint32_t idx0 = (uint32_t)(((uint64_t)start_frame
            * (uint64_t)g_sample_capture_global_overview.point_count)
        / (uint64_t)g_sample_capture_global_overview.frame_count);
    uint32_t idx1 = (uint32_t)(((uint64_t)end_frame
            * (uint64_t)g_sample_capture_global_overview.point_count
            + (uint64_t)g_sample_capture_global_overview.frame_count - 1ULL)
        / (uint64_t)g_sample_capture_global_overview.frame_count);
    if(idx0 >= g_sample_capture_global_overview.point_count)
    {
        idx0 = g_sample_capture_global_overview.point_count - 1U;
    }
    if(idx1 <= idx0)
    {
        idx1 = idx0 + 1U;
    }
    if(idx1 > g_sample_capture_global_overview.point_count)
    {
        idx1 = g_sample_capture_global_overview.point_count;
    }

    int16_t min_v = g_sample_capture_global_overview_points[idx0].min;
    int16_t max_v = g_sample_capture_global_overview_points[idx0].max;
    for(uint32_t idx = idx0 + 1U; idx < idx1; ++idx)
    {
        if(g_sample_capture_global_overview_points[idx].min < min_v)
        {
            min_v = g_sample_capture_global_overview_points[idx].min;
        }
        if(g_sample_capture_global_overview_points[idx].max > max_v)
        {
            max_v = g_sample_capture_global_overview_points[idx].max;
        }
    }
    if(out_min != 0)
    {
        *out_min = min_v;
    }
    if(out_max != 0)
    {
        *out_max = max_v;
    }
    return 1U;
}

uint8_t sample_capture_model_waveform_cache_ready(void)
{
    return g_sample_capture.wave_cache_ready;
}

uint8_t sample_capture_model_waveform_cache_get_handle(waveform_cache_handle_t *out_handle)
{
    if((out_handle == 0) || (g_sample_capture.wave_cache_ready == 0U))
    {
        return 0U;
    }
    *out_handle = g_sample_capture.wave_cache_handle;
    return 1U;
}

void sample_capture_model_note_rec_edit_first_render(void)
{
    if(g_sample_capture.rec_edit_first_render_pending == 0U)
    {
        return;
    }
    g_sample_capture.rec_edit_first_render_pending = 0U;

    multi_record_writer_status_t status;
    const multi_record_writer_status_t *status_ptr = 0;
    if(sample_capture_get_status(&status) != 0U)
    {
        status_ptr = &status;
    }
    sample_capture_debug_mark(REC_LIVE_DEBUG_REC_EDIT_FIRST_RENDER,
                              status_ptr,
                              g_sample_capture.state.temp_path,
                              g_sample_capture.state.recorded_frames);
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

    sample_capture_global_overview_request();
    if(sample_capture_model_view_uses_tile_cache(frame_count) == 0U)
    {
        return;
    }

    if(sample_capture_editor_cache_covers(start_frame, frame_count) != 0U)
    {
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
        g_sample_capture_debug.cache_hit_count++;
        const uint32_t hit_now = HAL_GetTick();
        if((g_sample_capture_debug.last_miss_start != start_frame)
                || (g_sample_capture_debug.last_miss_frames != frame_count)
                || ((hit_now - g_sample_capture_debug.last_miss_ms) >= 500U))
        {
            sample_capture_debug_log("OLD_AUDIO_TILE_HIT vs=%lu vf=%lu\r\n",
                                     (unsigned long)start_frame,
                                     (unsigned long)frame_count);
            g_sample_capture_debug.last_miss_start = start_frame;
            g_sample_capture_debug.last_miss_frames = frame_count;
            g_sample_capture_debug.last_miss_ms = hit_now;
        }
#endif
        if((g_sample_capture_line_hot.valid != 0U)
                && (g_sample_capture_line_hot.start_frame == start_frame)
                && (g_sample_capture_line_hot.frames == frame_count)
                && (g_sample_capture_line_hot.count == points))
        {
            return;
        }
        sample_capture_line_publish_from_editor_cache(start_frame,
                                                      frame_count,
                                                      points);
        return;
    }

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.cache_miss_count++;
    const uint32_t miss_now = HAL_GetTick();
    if((g_sample_capture_debug.last_miss_valid == 0U)
            || (g_sample_capture_debug.last_miss_start != start_frame)
            || (g_sample_capture_debug.last_miss_frames != frame_count)
            || ((miss_now - g_sample_capture_debug.last_miss_ms) >= 500U))
    {
        sample_capture_debug_log("OLD_AUDIO_TILE_MISS vs=%lu vf=%lu\r\n",
                                 (unsigned long)start_frame,
                                 (unsigned long)frame_count);
        g_sample_capture_debug.last_miss_start = start_frame;
        g_sample_capture_debug.last_miss_frames = frame_count;
        g_sample_capture_debug.last_miss_ms = miss_now;
        g_sample_capture_debug.last_miss_valid = 1U;
    }
#endif
    sample_capture_editor_cache_request(start_frame, frame_count);

    if((g_sample_capture_line_hot.valid != 0U)
            && (g_sample_capture_line_hot.start_frame == start_frame)
            && (g_sample_capture_line_hot.frames == frame_count)
            && (g_sample_capture_line_hot.count == points))
    {
        return;
    }
}

#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
static uint8_t sample_capture_editor_count_tiles(sample_capture_editor_cache_state_t state)
{
    uint8_t count = 0U;
    for(uint8_t i = 0U; i < SAMPLE_CAPTURE_EDITOR_TILE_COUNT; ++i)
    {
        if(g_sample_capture_editor_cache.tiles[i].state == state)
        {
            count++;
        }
    }
    return count;
}
#endif

void sample_capture_model_debug_note_renderer(sample_capture_renderer_debug_t renderer,
                                              uint8_t zoom,
                                              uint32_t view_start_frame,
                                              uint32_t view_frames,
                                              uint16_t inner_w,
                                              uint32_t samples_per_pixel,
                                              uint32_t wavecache_frames_per_column,
                                              uint8_t line_valid,
                                              uint16_t line_points,
                                              uint16_t draw_line_segments,
                                              uint8_t fallback_reason)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    const uint8_t cache_valid =
        sample_capture_editor_count_tiles(SAMPLE_CAPTURE_EDITOR_CACHE_READY);
    const uint8_t cache_loading =
        sample_capture_editor_count_tiles(SAMPLE_CAPTURE_EDITOR_CACHE_LOADING);
    const uint8_t covered = sample_capture_editor_cache_covers(view_start_frame, view_frames);
    const uint32_t now = HAL_GetTick();

    g_sample_capture_debug.draw_count++;
    if((uint8_t)renderer < 8U)
    {
        g_sample_capture_debug.renderer_count[(uint8_t)renderer]++;
    }
    g_sample_capture_debug.last_draw_segments = draw_line_segments;
    if(draw_line_segments > g_sample_capture_debug.max_draw_segments)
    {
        g_sample_capture_debug.max_draw_segments = draw_line_segments;
    }

    const uint8_t renderer_changed =
        ((g_sample_capture_debug.last_renderer_valid == 0U)
         || (g_sample_capture_debug.last_renderer != renderer)) ? 1U : 0U;
    const uint8_t view_changed =
        ((g_sample_capture_debug.last_source_change_valid == 0U)
         || (g_sample_capture_debug.last_zoom != zoom)
         || (g_sample_capture_debug.last_view_start_frame != view_start_frame)
         || (g_sample_capture_debug.last_view_frames != view_frames)
         || (g_sample_capture_debug.last_samples_per_pixel != samples_per_pixel)
         || (g_sample_capture_debug.last_wavecache_frames_per_column
             != wavecache_frames_per_column)
         || (g_sample_capture_debug.last_fallback_reason != fallback_reason)) ? 1U : 0U;
    const uint8_t summary_due =
        ((now - g_sample_capture_debug.last_summary_ms) >= 1000U) ? 1U : 0U;

    if((renderer_changed == 0U) && (view_changed == 0U) && (summary_due == 0U))
    {
        return;
    }

    const uint32_t sec_draws = g_sample_capture_debug.draw_count
        - g_sample_capture_debug.last_summary_draw_count;
    const uint32_t sec_elines = g_sample_capture_debug.eline_count
        - g_sample_capture_debug.last_summary_eline_count;
    const uint32_t sec_flushes = g_sample_capture_debug.flush_count
        - g_sample_capture_debug.last_summary_flush_count;

    if(wavecache_frames_per_column != 0U)
    {
        sample_capture_debug_log("RECEDIT R=%s z=%u vs=%lu vf=%lu w=%u spp=%lu wc=%lu fb=%u line=%u lp=%u old_ready=%u old_load=%u old_cov=%u seg=%u hit=%lu miss=%lu d/e/f=%lu/%lu/%lu\r\n",
                                 sample_capture_debug_renderer_label(renderer),
                                 (unsigned)zoom,
                                 (unsigned long)view_start_frame,
                                 (unsigned long)view_frames,
                                 (unsigned)inner_w,
                                 (unsigned long)samples_per_pixel,
                                 (unsigned long)wavecache_frames_per_column,
                                 (unsigned)fallback_reason,
                                 (unsigned)line_valid,
                                 (unsigned)line_points,
                                 (unsigned)cache_valid,
                                 (unsigned)cache_loading,
                                 (unsigned)covered,
                                 (unsigned)draw_line_segments,
                                 (unsigned long)g_sample_capture_debug.cache_hit_count,
                                 (unsigned long)g_sample_capture_debug.cache_miss_count,
                                 (unsigned long)sec_draws,
                                 (unsigned long)sec_elines,
                                 (unsigned long)sec_flushes);
    }
    else
    {
        sample_capture_debug_log("RECEDIT R=%s z=%u vs=%lu vf=%lu w=%u spp=%lu wc=NONE fb=%u line=%u lp=%u old_ready=%u old_load=%u old_cov=%u seg=%u hit=%lu miss=%lu d/e/f=%lu/%lu/%lu\r\n",
                                 sample_capture_debug_renderer_label(renderer),
                                 (unsigned)zoom,
                                 (unsigned long)view_start_frame,
                                 (unsigned long)view_frames,
                                 (unsigned)inner_w,
                                 (unsigned long)samples_per_pixel,
                                 (unsigned)fallback_reason,
                                 (unsigned)line_valid,
                                 (unsigned)line_points,
                                 (unsigned)cache_valid,
                                 (unsigned)cache_loading,
                                 (unsigned)covered,
                                 (unsigned)draw_line_segments,
                                 (unsigned long)g_sample_capture_debug.cache_hit_count,
                                 (unsigned long)g_sample_capture_debug.cache_miss_count,
                                 (unsigned long)sec_draws,
                                 (unsigned long)sec_elines,
                                 (unsigned long)sec_flushes);
    }

    g_sample_capture_debug.last_renderer = renderer;
    g_sample_capture_debug.last_renderer_valid = 1U;
    g_sample_capture_debug.last_zoom = zoom;
    g_sample_capture_debug.last_view_start_frame = view_start_frame;
    g_sample_capture_debug.last_view_frames = view_frames;
    g_sample_capture_debug.last_samples_per_pixel = samples_per_pixel;
    g_sample_capture_debug.last_wavecache_frames_per_column = wavecache_frames_per_column;
    g_sample_capture_debug.last_fallback_reason = fallback_reason;
    g_sample_capture_debug.last_source_change_valid = 1U;
    if(summary_due != 0U)
    {
        g_sample_capture_debug.last_summary_ms = now;
        g_sample_capture_debug.last_summary_draw_count = g_sample_capture_debug.draw_count;
        g_sample_capture_debug.last_summary_eline_count = g_sample_capture_debug.eline_count;
        g_sample_capture_debug.last_summary_flush_count = g_sample_capture_debug.flush_count;
    }
#else
    (void)renderer;
    (void)zoom;
    (void)view_start_frame;
    (void)view_frames;
    (void)inner_w;
    (void)samples_per_pixel;
    (void)wavecache_frames_per_column;
    (void)line_valid;
    (void)line_points;
    (void)draw_line_segments;
    (void)fallback_reason;
#endif
}

void sample_capture_model_debug_note_draw_cost(uint32_t page_ms, uint32_t waveform_ms)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.draw_last_ms = page_ms;
    if(page_ms > g_sample_capture_debug.draw_max_ms)
    {
        g_sample_capture_debug.draw_max_ms = page_ms;
    }
    g_sample_capture_debug.waveform_last_ms = waveform_ms;
    if(waveform_ms > g_sample_capture_debug.waveform_max_ms)
    {
        g_sample_capture_debug.waveform_max_ms = waveform_ms;
    }
#else
    (void)page_ms;
    (void)waveform_ms;
#endif
}

void sample_capture_model_debug_note_flush_cost(uint32_t flush_ms, uint8_t continued_flush)
{
#if SAMPLE_CAPTURE_WAVEFORM_DEBUG_UART
    g_sample_capture_debug.flush_count++;
    if(continued_flush != 0U)
    {
        g_sample_capture_debug.flush_cont_count++;
    }
    g_sample_capture_debug.flush_last_ms = flush_ms;
    if(flush_ms > g_sample_capture_debug.flush_max_ms)
    {
        g_sample_capture_debug.flush_max_ms = flush_ms;
    }
#else
    (void)flush_ms;
    (void)continued_flush;
#endif
}

static void sample_capture_waveform_cache_service_handle(void)
{
    if((g_sample_capture.state.take_valid == 0U)
            || (g_sample_capture.state.recording != 0U)
            || (g_sample_capture.state.armed_pending != 0U)
            || (g_sample_capture.state.temp_path[0] == '\0')
            || (sample_capture_path_is_temp(g_sample_capture.state.temp_path) != 0U)
            || (g_sample_capture.state.recorded_frames < WAVEFORM_CACHE_PERSIST_MIN_FRAMES)
            || (g_sample_capture.wave_cache_ready != 0U))
    {
        return;
    }
    if(g_sample_capture.wave_cache_retry_countdown != 0U)
    {
        g_sample_capture.wave_cache_retry_countdown--;
        return;
    }
    if(waveform_cache_open_for_wav(g_sample_capture.state.temp_path,
                                   &g_sample_capture.wave_cache_handle) != 0U)
    {
        g_sample_capture.wave_cache_ready = 1U;
        return;
    }
    g_sample_capture.wave_cache_retry_countdown = 32U;
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
        g_sample_capture.trigger_pending = 0U;
        g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
        sample_capture_live_summary_reset();
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

    if(((g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
            || (g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG))
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
        g_sample_capture.trigger_pending = 0U;
        sample_capture_live_summary_reset();
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_ARMED;
    }

    if((g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (rec_global_armed != 0U)
            && (g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
            && (g_sample_capture.trigger_pending == 0U)
            && (sample_capture_live_take_trigger() != 0U))
    {
        g_sample_capture.trigger_pending = 1U;
        sample_capture_capture_wait_baseline();
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_WAIT_QUANT;
    }

    /* Keep the historical ARM REC start path intact. */
    if((g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (rec_global_armed != 0U)
            && (g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_REC)
            && (transport_running != 0U)
            && (sample_capture_quant_is_due(transport_started) != 0U))
    {
        if(sample_capture_start_now() == 0U)
        {
            g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
            g_sample_capture.state.armed_pending = 0U;
        }
    }

    if((g_sample_capture.state.armed_pending != 0U)
            && (g_sample_capture.state.recording == 0U)
            && (rec_global_armed != 0U)
            && (g_sample_capture.state.arm == SAMPLE_CAPTURE_ARM_TRIG)
            && (g_sample_capture.trigger_pending != 0U)
            && ((g_sample_capture.state.quant == SAMPLE_CAPTURE_QUANT_NOW)
                || (transport_running != 0U))
            && (sample_capture_quant_is_due(transport_started) != 0U))
    {
        if(sample_capture_start_now() == 0U)
        {
            g_sample_capture.state.arm = SAMPLE_CAPTURE_ARM_OFF;
            g_sample_capture.state.armed_pending = 0U;
            g_sample_capture.trigger_pending = 0U;
            sample_capture_live_summary_reset();
        }
    }

    if(status.state == MULTI_RECORD_WRITER_STATE_TAKE_READY)
    {
        sample_capture_on_take_ready(&status);
        if(g_sample_capture.rec_edit_enter_deferred_services != 0U)
        {
            g_sample_capture.rec_edit_enter_deferred_services--;
            return;
        }
    }

    sample_capture_global_overview_service();
    sample_capture_waveform_cache_service_handle();
    sample_capture_editor_cache_service();
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
    (void)waveform_cache_request_for_wav_known_duration(
        final_path,
        WAVEFORM_CACHE_REASON_EDITOR_VISIBLE,
        g_sample_capture.state.edit_end_frame - g_sample_capture.state.edit_start_frame,
        MULTI_RECORD_WRITER_SAMPLE_RATE_HZ);
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

uint8_t sample_capture_model_assign_saved_take_to_pool(void)
{
    if(g_sample_capture.state.final_path[0] == '\0')
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

    if(sample_pool_load(slot, g_sample_capture.state.final_path) == false)
    {
        sample_capture_set_error(SAMPLE_CAPTURE_ERROR_LOAD_FAIL);
        return 0U;
    }

    g_sample_capture.state.error = SAMPLE_CAPTURE_ERROR_NONE;
    if(g_sample_capture.state.phase == SAMPLE_CAPTURE_PHASE_ERROR)
    {
        g_sample_capture.state.phase = SAMPLE_CAPTURE_PHASE_SAVED;
    }
    return 1U;
}

uint8_t sample_capture_model_toggle_zcross(void)
{
    g_sample_capture.state.edit_zcross_enabled =
        (g_sample_capture.state.edit_zcross_enabled == 0U) ? 1U : 0U;
    return 1U;
}
