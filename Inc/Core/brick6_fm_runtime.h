#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BRICK6_FM_VOICE_COUNT 16U
#define BRICK6_FM_RENDER_BLOCK 64U

typedef enum
{
    BRICK6_FM_MODE_MODERN = 0,
    BRICK6_FM_MODE_MARK_I,
    BRICK6_FM_MODE_OPL,
    BRICK6_FM_MODE_COUNT
} brick6_fm_mode_t;

void brick6_fm_runtime_init(void);
void brick6_fm_runtime_reset_instance(uint8_t instance_id);
void brick6_fm_runtime_all_notes_off(uint8_t instance_id);
void brick6_fm_runtime_note_on(uint8_t instance_id, uint8_t note, uint8_t velocity);
void brick6_fm_runtime_note_off(uint8_t instance_id, uint8_t note);
void brick6_fm_runtime_set_mode(uint8_t instance_id, brick6_fm_mode_t mode);
void brick6_fm_runtime_set_algorithm(uint8_t instance_id, uint8_t algorithm);
void brick6_fm_runtime_set_feedback(uint8_t instance_id, uint8_t feedback);
void brick6_fm_runtime_set_sync(uint8_t instance_id, uint8_t enabled);
uint8_t brick6_fm_runtime_render_instance(uint8_t instance_id,
                                          float *out_mono,
                                          uint32_t frames);

#ifdef __cplusplus
}
#endif
