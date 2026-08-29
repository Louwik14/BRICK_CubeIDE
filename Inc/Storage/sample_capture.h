#ifndef SAMPLE_CAPTURE_H
#define SAMPLE_CAPTURE_H

#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Track/entity_topology.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_CAPTURE_TRACK_COUNT BRICK_ENTITY_TOP_LEVEL_COUNT
#define SAMPLE_CAPTURE_PATH_MAX AUDIO_RECORDER_PATH_MAX
#define SAMPLE_CAPTURE_WAVEFORM_POINTS 1024U
#define SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE 32767
#define SAMPLE_CAPTURE_DETAIL_POINTS 384U
#define SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS 126U
#define SAMPLE_CAPTURE_LINE_POINTS 128U
#define SAMPLE_CAPTURE_GLOBAL_OVERVIEW_POINTS 4096U

#ifndef SAMPLE_CAPTURE_DEBUG_UART
#define SAMPLE_CAPTURE_DEBUG_UART 0U
#endif

#ifndef SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS
#define SAMPLE_CAPTURE_WAVEFORM_DEBUG_LOGS 0U
#endif

typedef enum
{
    SAMPLE_CAPTURE_ARM_OFF = 0,
    SAMPLE_CAPTURE_ARM_REC,
    SAMPLE_CAPTURE_ARM_TRIG,
    SAMPLE_CAPTURE_ARM_COUNT
} sample_capture_arm_t;

typedef enum
{
    SAMPLE_CAPTURE_LEN_FREE = 0,
    SAMPLE_CAPTURE_LEN_FIXED_MIN = 1,
    SAMPLE_CAPTURE_LEN_FIXED_MAX = 64
} sample_capture_len_t;

typedef enum
{
    SAMPLE_CAPTURE_QUANT_NOW = 0,
    SAMPLE_CAPTURE_QUANT_BAR,
    SAMPLE_CAPTURE_QUANT_PATTERN,
    SAMPLE_CAPTURE_QUANT_COUNT
} sample_capture_quant_t;

typedef enum
{
    SAMPLE_CAPTURE_VIEW_AUDIO_REC = 0,
    SAMPLE_CAPTURE_VIEW_REC_EDIT
} sample_capture_view_t;

typedef enum
{
    SAMPLE_CAPTURE_PHASE_IDLE = 0,
    SAMPLE_CAPTURE_PHASE_ARMED,
    SAMPLE_CAPTURE_PHASE_WAIT_QUANT,
    SAMPLE_CAPTURE_PHASE_RECORDING,
    SAMPLE_CAPTURE_PHASE_STOPPING,
    SAMPLE_CAPTURE_PHASE_REC_EDIT,
    SAMPLE_CAPTURE_PHASE_SAVED,
    SAMPLE_CAPTURE_PHASE_ERROR
} sample_capture_phase_t;

typedef enum
{
    SAMPLE_CAPTURE_ERROR_NONE = 0,
    SAMPLE_CAPTURE_ERROR_INVALID_ARG,
    SAMPLE_CAPTURE_ERROR_NO_ROUTE,
    SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE,
    SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE,
    SAMPLE_CAPTURE_ERROR_SD_BUSY,
    SAMPLE_CAPTURE_ERROR_SD_IO,
    SAMPLE_CAPTURE_ERROR_NO_TAKE,
    SAMPLE_CAPTURE_ERROR_NO_SLOT,
    SAMPLE_CAPTURE_ERROR_LOAD_FAIL,
    SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL
} sample_capture_error_t;

typedef enum
{
    SAMPLE_CAPTURE_RENDERER_EMPTY = 0,
    SAMPLE_CAPTURE_RENDERER_GLOBAL_OVERVIEW,
    SAMPLE_CAPTURE_RENDERER_OLD_LINE,
    SAMPLE_CAPTURE_RENDERER_OLD_AUDIO_TILE,
    SAMPLE_CAPTURE_RENDERER_BRKWAVE_TILE,
    SAMPLE_CAPTURE_RENDERER_SD_LINE_FALLBACK,
    SAMPLE_CAPTURE_RENDERER_BUILDING,
    SAMPLE_CAPTURE_RENDERER_ERROR
} sample_capture_renderer_debug_t;

typedef struct
{
    int16_t min;
    int16_t max;
    int16_t first;
    int16_t last;
} sample_capture_waveform_bucket_t;

typedef struct
{
    sample_capture_view_t view;
    sample_capture_phase_t phase;
    sample_capture_arm_t arm;
    uint8_t len_bars; /* 0 = FREE, otherwise 1..64 steps */
    sample_capture_quant_t quant;
    int8_t threshold_dbfs;
    uint8_t line_enabled;
    uint8_t mic_enabled;
    uint8_t trigger_latched;
    uint32_t live_peak_abs_pcm24;
    uint32_t live_peak_generation;
    uint8_t route_enabled[SAMPLE_CAPTURE_TRACK_COUNT];
    uint8_t armed_pending;
    uint8_t recording;
    uint8_t take_valid;
    uint32_t planned_frames;
    uint32_t recorded_frames;
    uint32_t edit_start_frame;
    uint32_t edit_end_frame;
    uint32_t edit_loop_start_frame;
    uint32_t edit_loop_end_frame;
    uint8_t edit_zoom;
    uint8_t edit_vzoom;
    uint8_t edit_zcross_enabled;
    uint32_t edit_scroll_frame;
    uint16_t waveform_count;
    uint32_t waveform_bucket_frames;
    sample_capture_waveform_bucket_t waveform[SAMPLE_CAPTURE_WAVEFORM_POINTS];
    uint8_t detail_valid;
    uint32_t detail_start_frame;
    uint32_t detail_frames;
    uint16_t detail_count;
    sample_capture_waveform_bucket_t detail[SAMPLE_CAPTURE_DETAIL_POINTS];
    uint8_t line_valid;
    uint32_t line_start_frame;
    uint32_t line_frames;
    uint16_t line_count;
    uint16_t line_peak;
    int16_t line[SAMPLE_CAPTURE_LINE_POINTS];
    sample_capture_error_t error;
    char temp_path[SAMPLE_CAPTURE_PATH_MAX];
    char final_path[SAMPLE_CAPTURE_PATH_MAX];
} sample_capture_state_t;

uint8_t sample_capture_prepare_temp(const char *temp_path,
                                    const char *final_path,
                                    uint32_t frame_limit);
uint8_t sample_capture_start(void);
uint8_t sample_capture_push_audio_block_from_irq(const int32_t *lr_interleaved,
                                                 uint32_t frames);
uint8_t sample_capture_request_stop(void);
uint8_t sample_capture_get_status(audio_recorder_status_t *out_status);

void sample_capture_model_init(void);
void sample_capture_model_service(void);
void sample_capture_model_get_state(sample_capture_state_t *out_state);
void sample_capture_model_set_view(sample_capture_view_t view);
uint8_t sample_capture_model_toggle_route(uint8_t track);
uint8_t sample_capture_model_source_track_is_enabled(uint8_t track);
uint8_t sample_capture_model_set_arm(sample_capture_arm_t arm);
uint8_t sample_capture_model_step_arm(int16_t delta);
uint8_t sample_capture_model_step_len(int16_t delta);
uint8_t sample_capture_model_step_quant(int16_t delta);
uint8_t sample_capture_model_set_threshold_dbfs(int8_t threshold_dbfs);
uint8_t sample_capture_model_step_threshold(int16_t delta);
uint8_t sample_capture_model_set_line_enabled(uint8_t enabled);
uint8_t sample_capture_model_toggle_line(void);
uint8_t sample_capture_model_set_mic_enabled(uint8_t enabled);
uint8_t sample_capture_model_toggle_mic(void);
uint8_t sample_capture_model_step_edit(uint8_t encoder, int16_t delta, uint8_t alt_held);
uint32_t sample_capture_model_visible_frames_for_zoom(uint32_t recorded_frames, uint8_t zoom);
uint32_t sample_capture_model_tile_cache_capacity_frames(void);
uint8_t sample_capture_model_view_uses_tile_cache(uint32_t frame_count);
uint8_t sample_capture_model_global_overview_ready(void);
uint16_t sample_capture_model_global_overview_peak(void);
uint8_t sample_capture_model_global_overview_minmax(uint32_t start_frame,
                                                    uint32_t frame_count,
                                                    int16_t *out_min,
                                                    int16_t *out_max);
uint8_t sample_capture_model_waveform_cache_ready(void);
uint8_t sample_capture_model_waveform_cache_get_handle(waveform_cache_handle_t *out_handle);
void sample_capture_model_note_rec_edit_first_render(void);
void sample_capture_model_request_detail_waveform(uint32_t start_frame,
                                                  uint32_t frame_count,
                                                  uint16_t columns);
void sample_capture_model_request_line_waveform(uint32_t start_frame,
                                                uint32_t frame_count,
                                                uint16_t columns);
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
                                              uint8_t fallback_reason);
void sample_capture_model_debug_note_draw_cost(uint32_t page_ms, uint32_t waveform_ms);
void sample_capture_model_debug_note_flush_cost(uint32_t flush_ms, uint8_t continued_flush);
uint8_t sample_capture_model_return_to_audio_rec(void);
uint8_t sample_capture_model_audition_trimmed(void);
uint8_t sample_capture_model_save_trimmed(void);
uint8_t sample_capture_model_assign_trimmed(void);
uint8_t sample_capture_model_assign_saved_take_to_pool(void);
uint8_t sample_capture_model_toggle_zcross(void);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_CAPTURE_H */
