#ifndef TRACK_STATE_H
#define TRACK_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "UI/ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TRACK_CONFIG_CAPACITY BRICK_ENTITY_CAPACITY

void track_state_init(void);

const ui_track_config_t *track_state_get_configs(void);
ui_track_config_t track_state_get_config(uint8_t track);
ui_track_family_t track_state_get_family(uint8_t track);
ui_track_type_t track_state_get_type(uint8_t track);
uint8_t track_state_get_midi_channel(uint8_t track);
ui_track_midi_source_t track_state_get_midi_source(uint8_t track);

bool track_state_set_track_family(uint8_t track, ui_track_family_t family);
bool track_state_set_track_type(uint8_t track, ui_track_type_t type);
bool track_state_set_track_midi_channel(uint8_t track, uint8_t channel_1_16);
bool track_state_set_track_midi_source(uint8_t track, ui_track_midi_source_t source);
uint8_t track_state_get_external_input(uint8_t track);
bool track_state_set_external_input(uint8_t track, uint8_t input);
bool track_state_apply_bulk(const uint8_t family[UI_TRACK_COUNT],
                            const uint8_t type[UI_TRACK_COUNT],
                            const uint8_t midi_channel[UI_TRACK_COUNT],
                            const uint8_t midi_source[UI_TRACK_COUNT]);
bool track_state_apply_bulk_with_inputs(const uint8_t family[UI_TRACK_COUNT],
                                        const uint8_t type[UI_TRACK_COUNT],
                                        const uint8_t midi_channel[UI_TRACK_COUNT],
                                        const uint8_t midi_source[UI_TRACK_COUNT],
                                        const uint8_t external_input[UI_TRACK_COUNT]);
bool track_state_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[UI_TRACK_COUNT]);

/* Sole CONTROL commit for a final structural state: validate/commit state,
 * then rebuild/publish the changed runtime PROGRAM descriptors. */
bool track_structure_apply_entity_bulk_with_inputs(
    const uint8_t family[BRICK_ENTITY_CAPACITY],
    const uint8_t type[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_channel[BRICK_ENTITY_CAPACITY],
    const uint8_t midi_source[BRICK_ENTITY_CAPACITY],
    const uint8_t external_input[UI_TRACK_COUNT]);

uint8_t track_state_count_tracks_with_family(ui_track_family_t family);
uint32_t track_state_get_revision(uint8_t track);
uint32_t track_state_get_global_revision(void);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_STATE_H */
