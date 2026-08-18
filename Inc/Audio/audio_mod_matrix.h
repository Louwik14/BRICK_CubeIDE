#ifndef AUDIO_MOD_MATRIX_H
#define AUDIO_MOD_MATRIX_H

#include <stdint.h>

#include "Mod/mod_matrix.h"

/* AUDIO-owned consumer for complete immutable CONTROL snapshots. */
void audio_mod_matrix_init(void);
void audio_mod_matrix_apply_snapshot(uint8_t track,
                                     const mod_matrix_control_snapshot_t *snapshot);
void audio_mod_matrix_consume_snapshots(void);
void audio_mod_matrix_rebuild_track(uint8_t track);
void audio_mod_matrix_base_update(uint8_t track, param_id_t id, float value);
void audio_mod_matrix_set_base_override(uint8_t track, param_id_t id, float value);
void audio_mod_matrix_clear_base_override(uint8_t track, param_id_t id, float base_value);

#endif /* AUDIO_MOD_MATRIX_H */
