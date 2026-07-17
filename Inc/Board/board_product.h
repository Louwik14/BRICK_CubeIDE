#pragma once

#include <stdint.h>

typedef struct
{
    uint8_t has_physical_cue;
    uint8_t physical_input_stereo_count;
    uint8_t audio_tdm_slots;
    uint8_t has_analog_hall_lanes;
    uint8_t has_macro_pots;
    uint8_t led_count;
} board_product_capabilities_t;

const board_product_capabilities_t *board_product_capabilities(void);

