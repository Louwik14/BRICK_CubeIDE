#ifndef UNDO_V2_H
#define UNDO_V2_H

#include <stdint.h>

#include "Seq/seq_step_snapshot.h"

#define UNDO_V2_MAX_SEQUENCE_TRANSACTIONS 8U
#define UNDO_V2_MAX_TRANSACTIONS (UNDO_V2_MAX_SEQUENCE_TRANSACTIONS + 1U)

typedef enum
{
    UNDO_V2_STATUS_OK = 0,
    UNDO_V2_STATUS_ERR_NO_TX,
    UNDO_V2_STATUS_ERR_OVERFLOW,
    UNDO_V2_STATUS_ERR_UNSUPPORTED,
    UNDO_V2_STATUS_ERR_CAPTURE_BLOCKED,
    UNDO_V2_STATUS_ERR_APPLY_FAILED,
    UNDO_V2_STATUS_ERR_INVALID_ARG
} undo_v2_status_t;

void undo_v2_init(void);
void undo_v2_clear_all(void);
void undo_v2_invalidate_history(void);

undo_v2_status_t undo_v2_begin_sequence_transaction(seq_track_id_t track,
                                                    const seq_step_id_t *steps,
                                                    uint8_t step_count);
undo_v2_status_t undo_v2_commit_sequence_transaction(void);
undo_v2_status_t undo_v2_commit_audio_transition(uint32_t before_generation,
                                                 uint32_t after_generation);
uint8_t undo_v2_audio_transition_can_commit(uint32_t before_generation,
                                            uint32_t after_generation);
void undo_v2_expire_audio(void);
void undo_v2_cancel_transaction(void);

undo_v2_status_t undo_v2_undo(void);
undo_v2_status_t undo_v2_redo(void);

void undo_v2_set_capture_suspended(uint8_t suspended);
void undo_v2_service(void);

#endif /* UNDO_V2_H */
