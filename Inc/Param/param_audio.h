#pragma once

#include <stdint.h>
#include "Param/param_spec.h"

typedef struct
{
    param_id_t id;
    float value;
} param_audio_value_t;

uint8_t param_audio_apply_track(const param_audio_value_t *value,
                                uint8_t track);
uint8_t param_audio_apply_track_rt(param_id_t id, uint8_t track, float value);
uint8_t param_audio_apply_track_temp(param_id_t id, uint8_t track, float value);
uint8_t param_audio_clear_track_temp(param_id_t id, uint8_t track);
