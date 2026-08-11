#pragma once

#include <stdint.h>

enum
{
    BRICK6_FM_PICO_OPERATOR_COUNT = 6,
    BRICK6_FM_PICO_ENV_COUNT = 4
};

typedef struct
{
    uint8_t algorithm;
    uint8_t feedback;
    uint8_t sync;
    uint8_t note;
    /* Resolved BRICK/MSFA phase increment, Q24; renderer converts to Q32. */
    int32_t phase_inc[BRICK6_FM_PICO_OPERATOR_COUNT];
    uint8_t rates[BRICK6_FM_PICO_OPERATOR_COUNT][BRICK6_FM_PICO_ENV_COUNT];
    uint8_t levels[BRICK6_FM_PICO_OPERATOR_COUNT][BRICK6_FM_PICO_ENV_COUNT];
    uint8_t output_level[BRICK6_FM_PICO_OPERATOR_COUNT];
    uint8_t operator_on[BRICK6_FM_PICO_OPERATOR_COUNT];
    uint8_t rate_scale[BRICK6_FM_PICO_OPERATOR_COUNT];
} brick6_fm_pico_config_t;

void brick6_fm_pico_renderer_init(void);
void brick6_fm_pico_renderer_reset(uint8_t instance_id);
void brick6_fm_pico_renderer_note_on(uint8_t instance_id,
                                     const brick6_fm_pico_config_t *config);
void brick6_fm_pico_renderer_note_off(uint8_t instance_id);
void brick6_fm_pico_renderer_render(uint8_t instance_id,
                                    const brick6_fm_pico_config_t *config,
                                    float *out_mono,
                                    uint32_t frames);
uint8_t brick6_fm_pico_renderer_is_complete(uint8_t instance_id,
                                            uint8_t carrier_mask);
