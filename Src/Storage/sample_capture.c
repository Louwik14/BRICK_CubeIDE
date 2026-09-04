#include "Storage/sample_capture.h"

#include "App/control_domain.h"
#include "IPC/control_audio_rec_bus.h"
#include "IPC/audio_rec_bus_contract.h"
#include "IPC/control_music_publication.h"
#include "Track/track_input_ownership.h"
#include "Track/track_runtime.h"
#include "IPC/audio_rec_level_reader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "ControlRT/control_rt_publication.h"
#include "Storage/audio_recorder_wav.h"
#include "Platform/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/pattern_load_storage.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "Storage/waveform_cache.h"
#include "ff.h"
#include "main.h"

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
#define SAMPLE_CAPTURE_TEMP_PATH "0:/PROJECT/REC/AUDIOREC_TMP.REC"
#define SAMPLE_CAPTURE_WORKING_WAV_PATH "0:/PROJECT/REC/AUDIOREC_TMP.WAV"
#define SAMPLE_CAPTURE_FINAL_DIR "0:/Samples"
#define SAMPLE_CAPTURE_FINAL_TRIES 10000U
#define SAMPLE_CAPTURE_COPY_FRAMES 1024U
#define SAMPLE_CAPTURE_WAV_DATA_OFFSET AUDIO_RECORDER_WAV_HEADER_BYTES
#define SAMPLE_CAPTURE_EDIT_ZOOM_MAX 255U
#define SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES 256U
#define SAMPLE_CAPTURE_STEPS_PER_BAR 16U
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN (-60)
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX (-6)
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT (-36)
#define SAMPLE_CAPTURE_PCM24_PEAK 8388607UL
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
#define SAMPLE_CAPTURE_EDITOR_TILE_FRAMES (AUDIO_RECORDER_SAMPLE_RATE_HZ / 2U)
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
    uint8_t detail_requested;
    uint8_t detail_building;
    uint8_t detail_seen[SAMPLE_CAPTURE_DETAIL_POINTS];
    uint32_t detail_request_start_frame;
    uint32_t detail_request_frames;
    uint16_t detail_request_columns;
    uint32_t detail_build_next_frame;
    uint8_t route_mask[SAMPLE_CAPTURE_TRACK_COUNT];
    uint8_t capture_enabled;
    uint8_t last_take_notified;
    uint8_t rec_edit_enter_deferred_services;
    uint8_t rec_edit_first_render_pending;
    uint16_t final_counter;
    uint32_t trigger_threshold_peak_abs_pcm24;
    uint32_t trigger_last_level_generation;
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
static uint8_t g_sample_capture_control_context;
CONTROL_M4_SRAM2 static sample_capture_line_hot_t g_sample_capture_line_hot;
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
    g_sample_capture_copy_buf[SAMPLE_CAPTURE_COPY_FRAMES * AUDIO_RECORDER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_detail_buf[SAMPLE_CAPTURE_DETAIL_BUILD_CHUNK_FRAMES * AUDIO_RECORDER_BYTES_PER_FRAME];
RECORDER_SCRATCH_SDRAM static int16_t
    g_sample_capture_line_source[SAMPLE_CAPTURE_LINE_MAX_SOURCE_FRAMES];
#if SAMPLE_CAPTURE_DEBUG_UART
STORAGE_STATE_SDRAM static sample_capture_debug_t g_sample_capture_debug;
#endif


/* Capture, waveform caches, editor model, service and save/assign remain in their original sequence.
 * Private fragments share this translation unit to preserve static state and call order. */

#include "SampleCapture/sample_capture_common.inc"

#include "SampleCapture/sample_capture_waveform.inc"

#include "SampleCapture/sample_capture_record.inc"

#include "SampleCapture/sample_capture_editor.inc"

#include "SampleCapture/sample_capture_service.inc"

#include "SampleCapture/sample_capture_save_assign.inc"

uint8_t sample_capture_control_start_prepared_at(uint64_t sample_time)
{
    return sample_capture_start_prepared_at(sample_time);
}

void sample_capture_model_set_control_context(uint8_t enabled)
{
    g_sample_capture_control_context = (enabled != 0U) ? 1U : 0U;
}
