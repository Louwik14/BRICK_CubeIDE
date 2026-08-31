#pragma once

#include <stdint.h>
#include "Param/param_ids.h"

uint8_t param_filter_audio_is_param(param_id_t id);
uint8_t param_filter_apply_value_audio(param_id_t id, uint8_t track, float value);
float param_filter_audio_attack_s(float value);
float param_filter_audio_decay_s(float value);
float param_filter_audio_sustain(float value);
float param_filter_audio_release_s(float value);
float param_filter_audio_cutoff_hz(float value);
float param_filter_audio_resonance(float value);
float param_filter_audio_eg_amount(float value);
float param_filter_audio_keytrack(float value);
