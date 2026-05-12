#ifndef BRICK6_MASTER_BUFFER_H
#define BRICK6_MASTER_BUFFER_H

#include <stdint.h>

#include "Audio/live_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BRICK6_MASTER_BUFFER_STATE_IDLE = 0,
    BRICK6_MASTER_BUFFER_STATE_ARMED,
    BRICK6_MASTER_BUFFER_STATE_RECORDING
} brick6_master_buffer_state_t;

typedef struct
{
    uint16_t grain_size;
    uint8_t preserve_pitch;
} brick6_master_buffer_shifter_config_t;

void brick6_master_buffer_init(live_recorder_t *rec,
                               float *storage,
                               uint32_t max_frames);
void brick6_master_buffer_reset(void);
void brick6_master_buffer_clear(void);

void brick6_master_buffer_set_record_len(uint32_t blocks);
void brick6_master_buffer_set_quantize_record(uint8_t enabled);
void brick6_master_buffer_set_quantize_play(uint8_t enabled);
void brick6_master_buffer_set_rate(float rate);
void brick6_master_buffer_set_fade_in(uint32_t frames);
void brick6_master_buffer_set_fade_out(uint32_t frames);
void brick6_master_buffer_set_xfade(float xfade);
float brick6_master_buffer_get_xfade(void);
void brick6_master_buffer_set_shifter_config(const brick6_master_buffer_shifter_config_t *config);
void brick6_master_buffer_get_shifter_config(brick6_master_buffer_shifter_config_t *out_config);

void brick6_master_buffer_set_source_enabled(uint8_t track, uint8_t enabled);
void brick6_master_buffer_set_all_sources(uint8_t enabled);
uint8_t brick6_master_buffer_get_source_enabled(uint8_t track);

void brick6_master_buffer_request_record(void);
void brick6_master_buffer_request_play(void);
void brick6_master_buffer_request_clear(void);
void brick6_master_buffer_on_transport_stop(void);

brick6_master_buffer_state_t brick6_master_buffer_get_state(void);
uint8_t brick6_master_buffer_is_recording(void);
uint8_t brick6_master_buffer_is_armed(void);
uint8_t brick6_master_buffer_is_waiting_start(void);
uint8_t brick6_master_buffer_has_take(void);
uint8_t brick6_master_buffer_is_playing(void);
uint32_t brick6_master_buffer_get_recorded_frames(void);
uint32_t brick6_master_buffer_get_record_target_frames(void);

void brick6_master_buffer_begin_block(uint32_t frames);
void brick6_master_buffer_on_boundary_edge(uint8_t track);
void brick6_master_buffer_submit_track_post_fader(uint32_t track_id,
                                                  const float *left,
                                                  const float *right,
                                                  uint32_t frames);
void brick6_master_buffer_commit_block(uint32_t frames);
void brick6_master_buffer_read_playback(float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_MASTER_BUFFER_H */
