#ifndef AUDIO_MOD_MATRIX_H
#define AUDIO_MOD_MATRIX_H

#include <stdint.h>

#include "Mod/mod_matrix.h"

void audio_mod_matrix_init(void);
uint8_t audio_mod_matrix_set_route_source(uint8_t track, uint8_t slot, uint8_t source);
uint8_t audio_mod_matrix_set_route_destination(uint8_t track, uint8_t slot,
                                               mod_destination_address_t destination);
uint8_t audio_mod_matrix_set_route_depth(uint8_t track, uint8_t slot, float depth);
uint8_t audio_mod_matrix_set_route_enabled(uint8_t track, uint8_t slot, uint8_t enabled);
uint8_t audio_mod_matrix_set_multi_source(uint8_t track, uint8_t op,
                                          uint8_t input, uint8_t source);
uint8_t audio_mod_matrix_set_slew_source(uint8_t track, uint8_t op, uint8_t source);
uint8_t audio_mod_matrix_set_slew_amount(uint8_t track, uint8_t op, float amount);
void audio_mod_matrix_rebuild_track(uint8_t track);
void audio_mod_matrix_finalize_dirty(void);
void audio_mod_matrix_base_update(uint8_t track, param_id_t id, float value);

#endif /* AUDIO_MOD_MATRIX_H */
