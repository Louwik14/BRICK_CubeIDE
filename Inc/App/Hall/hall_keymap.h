#ifndef APP_HALL_HALL_KEYMAP_H
#define APP_HALL_HALL_KEYMAP_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

typedef enum
{
    HALL_KEY_KIND_WHITE = 0,
    HALL_KEY_KIND_BLACK = 1
} hall_key_kind_t;

typedef struct
{
    uint8_t logical_key_id;
    hall_key_kind_t kind;
    uint8_t white_index;
    uint8_t black_index;
    uint8_t chromatic_position;
    uint8_t valid;
} hall_key_metadata_t;

uint8_t hall_keymap_key_for_mux_channel(uint8_t mux_index, uint8_t channel, uint8_t *out_key);
uint8_t hall_keymap_metadata(uint8_t key, hall_key_metadata_t *out_meta);

#endif /* APP_HALL_HALL_KEYMAP_H */
