#pragma once

#include <stdint.h>

#include "Param/param_registry.h"
#include "Track/track_types.h"

uint8_t tone_param_codec_slot_to_param(track_runtime_type_t type,
                                       uint8_t slot, param_id_t *out_param);
uint8_t tone_param_codec_param_to_slot(track_runtime_type_t type,
                                       param_id_t param, uint8_t *out_slot);
uint8_t tone_param_codec_count(track_runtime_type_t type);
