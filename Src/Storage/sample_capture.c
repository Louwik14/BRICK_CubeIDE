#include "Storage/sample_capture.h"

#include "Board/board_audio_input.h"

#include "IPC/control_audio_rec_bus.h"
#include "IPC/control_music_publication.h"
#include "Track/track_input_ownership.h"
#include "Track/track_runtime.h"
#include "IPC/audio_rec_level_reader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_page_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "ControlRT/control_rt_publication.h"
#include "Storage/audio_recorder_wav.h"
#include "Storage/rec_source.h"
#include "Storage/project_control.h"
#include "Storage/asset_ref.h"
#include "Track/track_state.h"
#include "Platform/memory_layout.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/sd_access_gate.h"
#include "Storage/sd_preview.h"
#include "SD/sd_scheduler_runtime.h"
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

#define SAMPLE_CAPTURE_REC_DIR REC_SOURCE_DIRECTORY
#define SAMPLE_CAPTURE_FINAL_TRIES 10000U
#define SAMPLE_CAPTURE_WAV_DATA_OFFSET AUDIO_RECORDER_WAV_HEADER_BYTES
#define SAMPLE_CAPTURE_EDIT_ZOOM_MAX 255U
#define SAMPLE_CAPTURE_EDIT_MIN_VISIBLE_FRAMES SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS
#define SAMPLE_CAPTURE_STEPS_PER_BAR 16U
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_MIN (-60)
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_MAX (-6)
#define SAMPLE_CAPTURE_THRESHOLD_DBFS_DEFAULT (-36)
#define SAMPLE_CAPTURE_PCM24_PEAK 8388607UL
#define SAMPLE_CAPTURE_ZCROSS_SEARCH_FRAMES 2048U
#define SAMPLE_CAPTURE_ZCROSS_SAME_GUARD_FRAMES 8U
#define SAMPLE_CAPTURE_EDIT_VZOOM_DEFAULT 2U
#define SAMPLE_CAPTURE_EDIT_VZOOM_MAX 8U
#define SAMPLE_CAPTURE_SAVE_CHUNK_BYTES SD_SCHEDULER_BULK_COPY_MAX_DATA_BYTES

typedef enum
{
    SAMPLE_CAPTURE_SAVE_IDLE = 0,
    SAMPLE_CAPTURE_SAVE_SELECT_PATH,
    SAMPLE_CAPTURE_SAVE_PROMOTE,
    SAMPLE_CAPTURE_SAVE_OPEN_SOURCE,
    SAMPLE_CAPTURE_SAVE_OPEN_DESTINATION,
    SAMPLE_CAPTURE_SAVE_READ_HEADER,
    SAMPLE_CAPTURE_SAVE_WRITE_HEADER,
    SAMPLE_CAPTURE_SAVE_SEEK_DATA,
    SAMPLE_CAPTURE_SAVE_READ_DATA,
    SAMPLE_CAPTURE_SAVE_WRITE_DATA,
    SAMPLE_CAPTURE_SAVE_SYNC,
    SAMPLE_CAPTURE_SAVE_CLOSE_DESTINATION,
    SAMPLE_CAPTURE_SAVE_CLOSE_SOURCE,
    SAMPLE_CAPTURE_SAVE_RENAME,
    SAMPLE_CAPTURE_SAVE_ROLLBACK_DESTINATION,
    SAMPLE_CAPTURE_SAVE_ROLLBACK_SOURCE,
    SAMPLE_CAPTURE_SAVE_ROLLBACK_UNLINK
} sample_capture_save_phase_t;

typedef struct
{
    FIL source;
    FIL destination;
    sample_capture_save_phase_t phase;
    uint32_t bytes_left;
    uint32_t chunk_bytes;
    uint32_t saved_frames;
    uint32_t start_frame;
    uint16_t path_attempts;
    uint8_t source_open;
    uint8_t destination_open;
    uint8_t no_copy;
    char source_path[SAMPLE_CAPTURE_PATH_MAX];
    char temporary_path[SAMPLE_CAPTURE_PATH_MAX];
    char final_path[SAMPLE_CAPTURE_PATH_MAX];
    uint8_t header[SAMPLE_CAPTURE_WAV_DATA_OFFSET];
} sample_capture_save_job_t;

typedef struct
{
    sample_capture_state_t state;
    waveform_cache_handle_t wave_cache_handle;
    uint8_t wave_cache_ready;
    uint8_t wave_cache_retry_countdown;
    uint8_t route_mask[SAMPLE_CAPTURE_TRACK_COUNT];
    uint8_t capture_enabled;
    uint8_t last_take_notified;
    uint8_t rec_edit_enter_deferred_services;
    uint8_t rec_edit_first_render_pending;
    uint16_t final_counter;
    uint32_t trigger_threshold_peak_abs_pcm24;
    uint32_t trigger_arm_epoch;
    uint8_t assign_tracks[BRICK_ENTITY_CAPACITY];
    uint8_t assign_count;
    uint8_t assign_index;
    uint8_t assign_loading;
    uint8_t assign_target;
    uint32_t visible_rec_generation;
    sample_capture_save_job_t save_job;
} sample_capture_model_t;

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
RECORDER_SCRATCH_SDRAM static uint8_t
    g_sample_capture_copy_buf[SAMPLE_CAPTURE_SAVE_CHUNK_BYTES];
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
