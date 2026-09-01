#pragma once

#include "Param/param_ids.h"
#include "Track/track_runtime.h"

uint8_t param_backend_is_midi_cc_id(param_id_t id);
uint8_t param_backend_midi_cc_number_from_id(param_id_t id);
uint8_t param_backend_track_supports_midi_tone_ctx(const track_runtime_ctx_t *ctx);
uint8_t param_backend_track_supports_midi_tone_descriptor(
    const track_runtime_descriptor_t *descriptor);
uint8_t param_backend_send_midi_cc(uint8_t track, param_id_t id, float value);
uint8_t param_backend_apply_track_value_control(
    uint8_t track, param_id_t id, float value);
