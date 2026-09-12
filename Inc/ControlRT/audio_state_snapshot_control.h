#ifndef AUDIO_STATE_SNAPSHOT_CONTROL_H
#define AUDIO_STATE_SNAPSHOT_CONTROL_H

#include <stdint.h>

#include "IPC/control_audio_command.h"

void audio_state_snapshot_control_init(void);
uint8_t audio_state_snapshot_control_begin(
    control_audio_state_transition_kind_t transition);
uint8_t audio_state_snapshot_control_commit(void);
void audio_state_snapshot_control_abort(void);
uint8_t audio_state_snapshot_control_active(void);
uint8_t audio_state_snapshot_control_preflight(void);
uint8_t audio_state_snapshot_control_absorb(
    const control_audio_command_t *commands, uint16_t count);
uint8_t audio_state_snapshot_control_batch_is_projectable(
    const control_audio_command_t *commands, uint16_t count);

#endif
