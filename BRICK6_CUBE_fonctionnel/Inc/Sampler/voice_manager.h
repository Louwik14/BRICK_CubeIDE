#pragma once

#include <stdint.h>

#include "Sampler/sample_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    VOICE_OFF = 0,
    VOICE_ATTACK,
    VOICE_STREAM
} voice_state_t;

typedef struct
{
    uint16_t sample_id;
    const sample_desc_t *sample;

    uint32_t position;
    uint32_t stream_pos_frames;
    uint8_t streamer_id;

    float gain_l;
    float gain_r;

    uint8_t loop_enabled;
    uint32_t loop_start_frame;
    uint32_t loop_end_frame;

    uint8_t seek_pending;
    uint32_t seek_target_frame;

    voice_state_t state;
    uint8_t active;
} voice_t;

extern voice_t voices[24];

void voice_manager_init(void);
void voice_manager_trigger(uint16_t sample_id, float gain_l, float gain_r);
void voice_manager_service(void);
void voice_manager_process(float *out_l, float *out_r, uint32_t frames);

#ifdef __cplusplus
}
#endif
