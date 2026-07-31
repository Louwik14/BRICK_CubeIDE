#ifndef TRACK_INPUT_OWNERSHIP_H
#define TRACK_INPUT_OWNERSHIP_H

#include <stdint.h>

#include "UI/ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRACK_INPUT_OWNER_NONE 0xFFU

void track_input_ownership_init(const ui_track_config_t configs[UI_TRACK_COUNT]);
uint8_t track_input_ownership_apply_configs(
    const ui_track_config_t configs[UI_TRACK_COUNT]);
uint8_t track_input_ownership_apply_bulk(
    const ui_track_config_t configs[UI_TRACK_COUNT],
    const uint8_t external_input[UI_TRACK_COUNT]);
uint8_t track_input_ownership_validate_bulk(
    const ui_track_config_t configs[UI_TRACK_COUNT],
    const uint8_t external_input[UI_TRACK_COUNT]);
uint8_t track_input_ownership_can_claim(uint8_t track, uint8_t input);
uint8_t track_input_ownership_set_external_input(
    uint8_t track,
    uint8_t input,
    const ui_track_config_t configs[UI_TRACK_COUNT]);
uint8_t track_input_ownership_get_external_input(uint8_t track);
uint8_t track_input_ownership_get_external_owner(uint8_t input, uint8_t *out_track);
uint8_t track_input_ownership_get_audible_owner(uint8_t input, uint8_t *out_track);
uint8_t track_input_ownership_track_owns_input(uint8_t track, uint8_t input);
uint8_t track_input_ownership_get_fixed_input_track(uint8_t input, uint8_t *out_track);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_INPUT_OWNERSHIP_H */
