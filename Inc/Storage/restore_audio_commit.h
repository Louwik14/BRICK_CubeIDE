#ifndef RESTORE_AUDIO_COMMIT_H
#define RESTORE_AUDIO_COMMIT_H

#include <stdint.h>

#include "Storage/restore_plan_contract.h"

/* AUDIO-side terminal. A false result denotes a violated PREPARE/ABI
 * invariant; it is not a recoverable restore outcome. */
uint8_t restore_audio_commit_validate(const restore_audio_plan_t *plan);
uint8_t restore_audio_commit_apply(const restore_audio_plan_t *plan);

#endif
