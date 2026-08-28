#ifndef AUDIO_MOD_MATRIX_H
#define AUDIO_MOD_MATRIX_H

#include <stdint.h>

#include "Mod/mod_matrix.h"

void audio_mod_matrix_init(void);
uint8_t audio_mod_matrix_apply_param(uint8_t track, uint8_t slot,
                                     param_id_t id, float value);
void audio_mod_matrix_rebuild_track(uint8_t track);
void audio_mod_matrix_update_context_param(uint8_t track, param_id_t id,
                                           float value);
void audio_mod_matrix_finalize_dirty(void);
void audio_mod_matrix_base_update(uint8_t track, param_id_t id, float value);

#endif /* AUDIO_MOD_MATRIX_H */
