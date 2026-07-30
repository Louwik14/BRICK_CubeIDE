#ifndef LOOPER_RAW_DEBUG_H
#define LOOPER_RAW_DEBUG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOOPER_RAW_DEBUG_RING_CAP 64U
#define LOOPER_RAW_DEBUG_WRAP_CAPTURE_CAP 4U

typedef enum
{
    LOOPER_RAW_DEBUG_EVENT_NONE = 0,
    LOOPER_RAW_DEBUG_EVENT_BOUNDARY,
    LOOPER_RAW_DEBUG_EVENT_REC_START,
    LOOPER_RAW_DEBUG_EVENT_REC_STOP,
    LOOPER_RAW_DEBUG_EVENT_REC_FINAL,
    LOOPER_RAW_DEBUG_EVENT_PLAY_START,
    LOOPER_RAW_DEBUG_EVENT_WRAP,
    LOOPER_RAW_DEBUG_EVENT_CACHE_MISS,
    LOOPER_RAW_DEBUG_EVENT_WRITER_STATE,
    LOOPER_RAW_DEBUG_EVENT_PREROLL_USED,
    LOOPER_RAW_DEBUG_EVENT_PREROLL_UNDERRUN,
    LOOPER_RAW_DEBUG_EVENT_PREROLL_REUSED_AFTER_WRAP,
    LOOPER_RAW_DEBUG_EVENT_RAW_RELAY_DONE
} looper_raw_debug_event_type_t;

typedef enum
{
    LOOPER_RAW_DEBUG_SOURCE_NONE = 0,
    LOOPER_RAW_DEBUG_SOURCE_PREROLL_RAM,
    LOOPER_RAW_DEBUG_SOURCE_RAW_PAGE_CACHE,
    LOOPER_RAW_DEBUG_SOURCE_RAW_PAGE_MISS
} looper_raw_debug_source_t;

typedef struct
{
    uint32_t boundary_id;
    uint32_t samples_per_step_q16;
    uint64_t audio_sample_abs;
    uint16_t sample_offset_in_block;
    uint8_t logical_track;
    uint8_t step;
} looper_raw_debug_boundary_t;

typedef struct
{
    uint32_t take_id;
    uint64_t rec_request_sample_abs;
    uint64_t rec_actual_start_sample_abs;
    uint64_t rec_start_boundary_sample_abs;
    uint64_t rec_actual_stop_sample_abs;
    uint64_t rec_stop_boundary_sample_abs;
    uint64_t boundary_span_frames;
    int64_t delta_start_to_boundary;
    int64_t delta_stop_to_boundary;
    int64_t delta_recorded_vs_boundary_span;
    uint32_t rec_start_boundary_id;
    uint32_t rec_stop_boundary_id;
    uint32_t recorded_frames;
    uint32_t expected_frames;
    uint8_t logical_track;
    uint8_t raw_slot;
    uint8_t len_mode;
    uint8_t writer_state;
} looper_raw_debug_rec_t;

typedef struct
{
    uint32_t take_id;
    uint32_t frames_total;
    uint32_t start_playhead;
    uint32_t wrap_count;
    uint32_t playhead_before_wrap;
    uint32_t playhead_after_wrap;
    uint32_t frame_read_before_wrap;
    uint32_t frame_read_after_wrap;
    uint32_t page_index_before_wrap;
    uint32_t page_index_after_wrap;
    uint32_t playback_start_boundary_id;
    uint32_t nearest_boundary_id;
    uint64_t playback_start_sample_abs;
    uint64_t playback_start_boundary_sample_abs;
    uint64_t sample_abs_at_wrap;
    int64_t delta_start_to_boundary;
    int64_t delta_wrap_vs_boundary;
    uint16_t frac_before_wrap;
    uint16_t frac_after_wrap;
    uint8_t logical_track;
    uint8_t raw_slot;
    uint8_t playback_start_source;
    uint8_t playback_start_cache_hit;
    uint8_t source_before_wrap;
    uint8_t source_after_wrap;
    uint8_t cache_hit_before_wrap;
    uint8_t cache_hit_after_wrap;
    uint8_t interpolation_active;
    uint8_t gain_fade_active;
    uint8_t declick_active;
    uint8_t reserved;
} looper_raw_debug_play_t;

typedef struct
{
    looper_raw_debug_event_type_t type;
    uint32_t sequence;
    uint32_t boundary_id;
    uint32_t take_id;
    uint64_t sample_abs;
    uint32_t value0;
    uint32_t value1;
    uint8_t logical_track;
    uint8_t raw_slot;
    uint8_t writer_state;
    uint8_t reserved;
} looper_raw_debug_event_t;

typedef struct
{
    looper_raw_debug_boundary_t last_boundary;
    looper_raw_debug_rec_t last_rec;
    looper_raw_debug_play_t last_play;
    looper_raw_debug_play_t first_wraps[LOOPER_RAW_DEBUG_WRAP_CAPTURE_CAP];
    uint32_t playback_watch_take_id;
    uint32_t wrap_capture_count;
    uint32_t rec_start_count;
    uint32_t rec_stop_count;
    uint32_t playback_start_count;
    uint32_t wrap_count;
    uint32_t cache_miss_count;
    uint32_t preroll_used_count;
    uint32_t preroll_underrun_count;
    uint32_t preroll_reused_after_wrap_count;
    uint32_t raw_relay_done_count;
    uint32_t raw_relay_playhead;
    uint32_t raw_relay_page_index;
    uint8_t last_writer_state;
    uint8_t last_cache_miss_track;
    uint8_t last_cache_miss_raw_slot;
    uint8_t playback_watch_active;
    uint8_t play_start_seen;
    uint8_t raw_relay_track;
    uint8_t raw_relay_raw_slot;
    uint8_t reserved;
    uint32_t event_write_index;
    uint32_t event_sequence;
    looper_raw_debug_event_t events[LOOPER_RAW_DEBUG_RING_CAP];
} looper_raw_debug_snapshot_t;

typedef struct
{
    uint32_t cache_miss_count;
    uint32_t preroll_underrun_count;
    uint32_t preroll_reused_after_wrap_count;
} looper_raw_debug_health_snapshot_t;

void looper_raw_debug_reset(void);
void looper_raw_debug_note_boundary(uint8_t logical_track,
                                    uint8_t step,
                                    uint64_t audio_sample_abs,
                                    uint16_t sample_offset_in_block,
                                    uint32_t samples_per_step_q16);
void looper_raw_debug_note_rec_start(uint8_t logical_track,
                                     uint8_t raw_slot,
                                     uint8_t len_mode,
                                     uint64_t rec_request_sample_abs,
                                     uint64_t rec_actual_start_sample_abs,
                                     uint32_t expected_frames);
void looper_raw_debug_note_rec_stop(uint8_t logical_track,
                                    uint8_t raw_slot,
                                    uint64_t rec_request_sample_abs,
                                    uint64_t rec_actual_stop_sample_abs);
void looper_raw_debug_note_rec_final(uint8_t logical_track,
                                     uint8_t raw_slot,
                                     uint32_t recorded_frames,
                                     uint8_t writer_state);
void looper_raw_debug_note_play_start(uint8_t logical_track,
                                      uint8_t raw_slot,
                                      uint32_t frames_total,
                                      uint64_t playback_start_sample_abs,
                                      uint32_t start_playhead,
                                      uint8_t source,
                                      uint8_t cache_hit);
void looper_raw_debug_note_wrap(uint8_t logical_track,
                                uint8_t raw_slot,
                                uint32_t frames_total,
                                uint64_t sample_abs_at_wrap,
                                uint32_t playhead_before_wrap,
                                uint32_t playhead_after_wrap);
void looper_raw_debug_note_wrap_ex(uint8_t logical_track,
                                   uint8_t raw_slot,
                                   uint32_t frames_total,
                                   uint64_t sample_abs_at_wrap,
                                   uint32_t playhead_before_wrap,
                                   uint32_t playhead_after_wrap,
                                   uint8_t source_before_wrap,
                                   uint8_t source_after_wrap,
                                   uint32_t frame_read_before_wrap,
                                   uint32_t frame_read_after_wrap,
                                   uint32_t page_index_before_wrap,
                                   uint32_t page_index_after_wrap,
                                   uint8_t cache_hit_before_wrap,
                                   uint8_t cache_hit_after_wrap,
                                   uint16_t frac_before_wrap,
                                   uint16_t frac_after_wrap,
                                   uint8_t interpolation_active,
                                   uint8_t gain_fade_active,
                                   uint8_t declick_active);
void looper_raw_debug_note_cache_miss(uint8_t logical_track, uint8_t raw_slot);
void looper_raw_debug_note_preroll_used(uint8_t logical_track, uint8_t raw_slot);
void looper_raw_debug_note_preroll_underrun(uint8_t logical_track, uint8_t raw_slot);
void looper_raw_debug_note_preroll_reused_after_wrap(uint8_t logical_track, uint8_t raw_slot);
void looper_raw_debug_note_raw_relay_done(uint8_t logical_track,
                                          uint8_t raw_slot,
                                          uint32_t playhead,
                                          uint32_t page_index);
void looper_raw_debug_note_writer_state(uint8_t writer_state);
void looper_raw_debug_get_snapshot(looper_raw_debug_snapshot_t *out_snapshot);
void looper_raw_debug_get_health_snapshot(
    looper_raw_debug_health_snapshot_t *out_snapshot);
void looper_raw_debug_dump_uart(void);
void looper_raw_debug_service_uart(void);

#ifdef __cplusplus
}
#endif

#endif /* LOOPER_RAW_DEBUG_H */
