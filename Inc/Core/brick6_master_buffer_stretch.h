#ifndef BRICK6_MASTER_BUFFER_STRETCH_H
#define BRICK6_MASTER_BUFFER_STRETCH_H

#include <stdint.h>

#include "Core/brick6_master_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    BRICK6_MASTER_BUFFER_STRETCH_STATUS_BYPASS = 0,
    BRICK6_MASTER_BUFFER_STRETCH_STATUS_NEEDS_ANALYSIS,
    BRICK6_MASTER_BUFFER_STRETCH_STATUS_READY
} brick6_master_buffer_stretch_status_t;

typedef struct
{
    uint32_t source_generation;
    uint32_t config_generation;
    uint32_t source_frames;
    uint32_t max_frames;
    uint32_t analysis_cursor;
    uint16_t transient_count;
    uint8_t analysis_pending;
    uint8_t analysis_ready;
    uint8_t mode_active;
    brick6_master_buffer_stretch_status_t status;
} brick6_master_buffer_stretch_state_t;

void brick6_master_buffer_stretch_init(uint32_t max_frames);
void brick6_master_buffer_stretch_reset(void);
void brick6_master_buffer_stretch_clear(void);
void brick6_master_buffer_stretch_set_config(const brick6_master_buffer_stretch_config_t *config);
void brick6_master_buffer_stretch_notify_record_started(uint32_t source_generation);
void brick6_master_buffer_stretch_notify_record_finished(const float *interleaved_stereo,
                                                         uint32_t recorded_frames,
                                                         uint32_t max_frames,
                                                         uint32_t source_generation);
void brick6_master_buffer_stretch_notify_record_stopped(const float *interleaved_stereo,
                                                        uint32_t recorded_frames,
                                                        uint32_t max_frames,
                                                        uint32_t source_generation);
void brick6_master_buffer_stretch_set_source(const float *interleaved_stereo,
                                             uint32_t recorded_frames,
                                             uint32_t max_frames,
                                             uint32_t source_generation);
void brick6_master_buffer_stretch_mark_analysis_ready(uint32_t source_generation);
void brick6_master_buffer_stretch_get_state(brick6_master_buffer_stretch_state_t *out_state);
uint8_t brick6_master_buffer_stretch_is_ready(void);
uint8_t brick6_master_buffer_stretch_is_active(void);
void brick6_master_buffer_stretch_service_analysis(void);
void brick6_master_buffer_stretch_render_block(float *left, float *right, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* BRICK6_MASTER_BUFFER_STRETCH_H */
