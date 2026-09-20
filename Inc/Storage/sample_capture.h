#ifndef SAMPLE_CAPTURE_H
#define SAMPLE_CAPTURE_H

#include "Storage/audio_recorder.h"
#include "Storage/waveform_cache.h"
#include "Storage/waveform_service.h"
#include "Track/entity_topology.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_CAPTURE_TRACK_COUNT BRICK_ENTITY_TOP_LEVEL_COUNT
#define SAMPLE_CAPTURE_PATH_MAX AUDIO_RECORDER_PATH_MAX
#define SAMPLE_CAPTURE_WAVEFORM_FULL_SCALE 32767
#define SAMPLE_CAPTURE_DETAIL_VISIBLE_POINTS 126U

typedef enum
{
    SAMPLE_CAPTURE_ARM_OFF = 0,
    SAMPLE_CAPTURE_ARM_REC,
    SAMPLE_CAPTURE_ARM_TRIG,
    SAMPLE_CAPTURE_ARM_COUNT
} sample_capture_arm_t;

typedef enum
{
    SAMPLE_CAPTURE_TRIG_NOW = 0,
    SAMPLE_CAPTURE_TRIG_THRESHOLD,
    SAMPLE_CAPTURE_TRIG_PATTERN,
    SAMPLE_CAPTURE_TRIG_THRESHOLD_PLAY,
    SAMPLE_CAPTURE_TRIG_COUNT
} sample_capture_trig_t;

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
    SAMPLE_CAPTURE_REC_LED_OFF = 0,
    SAMPLE_CAPTURE_REC_LED_WAITING,
    SAMPLE_CAPTURE_REC_LED_ACTIVE
} sample_capture_rec_led_state_t;

typedef enum
{
    SAMPLE_CAPTURE_PHASE_IDLE = 0,
    SAMPLE_CAPTURE_PHASE_ARMED,
    SAMPLE_CAPTURE_PHASE_WAIT_QUANT,
    SAMPLE_CAPTURE_PHASE_RECORDING,
    SAMPLE_CAPTURE_PHASE_STOPPING,
    SAMPLE_CAPTURE_PHASE_TAKE_READY,
    SAMPLE_CAPTURE_PHASE_REC_EDIT,
    SAMPLE_CAPTURE_PHASE_SAVED,
    SAMPLE_CAPTURE_PHASE_ERROR
} sample_capture_phase_t;

typedef enum
{
    SAMPLE_CAPTURE_ERROR_NONE = 0,
    SAMPLE_CAPTURE_ERROR_INVALID_ARG,
    SAMPLE_CAPTURE_ERROR_NO_ROUTE,
    SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE,
    SAMPLE_CAPTURE_ERROR_SD_BUSY,
    SAMPLE_CAPTURE_ERROR_SD_IO,
    SAMPLE_CAPTURE_ERROR_NO_TAKE,
    SAMPLE_CAPTURE_ERROR_NO_SLOT,
    SAMPLE_CAPTURE_ERROR_LOAD_FAIL,
    SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL,
    SAMPLE_CAPTURE_ERROR_OVERDUB
} sample_capture_error_t;

typedef struct
{
    sample_capture_view_t view;
    sample_capture_phase_t phase;
    sample_capture_arm_t arm;
    sample_capture_trig_t trig;
    uint8_t len_bars; /* 0 = FREE, otherwise 1/2/4/.../64 bars */
    sample_capture_quant_t quant;
    int8_t threshold_dbfs;
    uint8_t line_enabled;
    uint8_t mic_enabled;
    uint8_t usb_enabled;
    uint8_t overdub_enabled;
    uint8_t trigger_latched;
    uint32_t live_peak_abs_pcm24;
    uint32_t live_peak_generation;
    uint8_t route_enabled[SAMPLE_CAPTURE_TRACK_COUNT];
    uint8_t armed_pending;
    uint8_t recording;
    uint8_t take_valid;
    uint8_t save_pending;
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
    sample_capture_error_t error;
    char temp_path[SAMPLE_CAPTURE_PATH_MAX];
    char final_path[SAMPLE_CAPTURE_PATH_MAX];
} sample_capture_state_t;

void sample_capture_model_init(void);
void sample_capture_model_service(void);
uint8_t sample_capture_model_storage_service(void);
void sample_capture_control_on_transport_start(uint64_t sample_time);
void sample_capture_control_on_musical_boundary(uint8_t track,
                                                uint64_t sample_time);
void sample_capture_control_on_transport_stop(uint64_t sample_time);
void sample_capture_control_on_global_rec_arm(uint8_t armed);
void sample_capture_model_get_state(sample_capture_state_t *out_state);
sample_capture_rec_led_state_t sample_capture_model_rec_led_state(void);
void sample_capture_model_set_view(sample_capture_view_t view);
uint8_t sample_capture_model_open_crop(void);
uint8_t sample_capture_model_toggle_route(uint8_t track);
uint8_t sample_capture_model_source_track_is_enabled(uint8_t track);
uint8_t sample_capture_model_set_arm(sample_capture_arm_t arm);
uint8_t sample_capture_model_step_arm(int16_t delta);
uint8_t sample_capture_model_step_trig(int16_t delta);
uint8_t sample_capture_model_toggle_record(void);
uint8_t sample_capture_model_cancel_for_note_rec(void);
uint8_t sample_capture_model_step_len(int16_t delta);
uint8_t sample_capture_model_step_quant(int16_t delta);
uint8_t sample_capture_model_set_threshold_dbfs(int8_t threshold_dbfs);
uint8_t sample_capture_model_step_threshold(int16_t delta);
uint8_t sample_capture_model_set_line_enabled(uint8_t enabled);
uint8_t sample_capture_model_toggle_line(void);
uint8_t sample_capture_model_set_mic_enabled(uint8_t enabled);
uint8_t sample_capture_model_toggle_mic(void);
uint8_t sample_capture_model_set_usb_enabled(uint8_t enabled);
uint8_t sample_capture_model_toggle_usb(void);
uint8_t sample_capture_model_toggle_overdub(void);
uint8_t sample_capture_model_step_edit(uint8_t encoder, int16_t delta, uint8_t alt_held);
uint32_t sample_capture_model_visible_frames_for_zoom(uint32_t recorded_frames, uint8_t zoom);
uint8_t sample_capture_model_rec_waveform_source(waveform_source_t *out_source);
uint8_t sample_capture_model_waveform_cache_get_handle(waveform_cache_handle_t *out_handle);
void sample_capture_model_note_rec_edit_first_render(void);
uint8_t sample_capture_model_return_to_audio_rec(void);
uint8_t sample_capture_model_audition_trimmed(void);
uint8_t sample_capture_model_save_trimmed(void);
uint8_t sample_capture_model_assign_saved_take_to_pool(void);
uint8_t sample_capture_model_assign_refresh(void);
uint8_t sample_capture_model_assign_count(void);
uint8_t sample_capture_model_assign_selected(uint8_t *out_track);
uint8_t sample_capture_model_assign_step(int16_t delta);
uint8_t sample_capture_model_assign_selected_take(void);
void sample_capture_model_assign_service(void);
void sample_capture_model_assign_cancel(void);
uint8_t sample_capture_model_toggle_zcross(void);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLE_CAPTURE_H */
