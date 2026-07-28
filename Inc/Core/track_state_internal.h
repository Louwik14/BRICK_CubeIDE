#ifndef TRACK_STATE_INTERNAL_H
#define TRACK_STATE_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "UI/ui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

bool track_state_set_voice_group_seq_link_raw(uint8_t master_track, uint8_t seq_link);
bool track_state_apply_voice_group_seq_link_bulk_raw(const uint8_t seq_link[UI_TRACK_COUNT]);

#ifdef __cplusplus
}
#endif

#endif /* TRACK_STATE_INTERNAL_H */
