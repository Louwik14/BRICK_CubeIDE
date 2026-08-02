#ifndef UNDO_V2_H
#define UNDO_V2_H

#include <stdint.h>

#include "Seq/seq_step_snapshot.h"

#define UNDO_V2_MAX_TRANSACTIONS 8U

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

typedef enum
{
    UNDO_V2_SOURCE_NONE = 0,
    UNDO_V2_SOURCE_ENCODER,
    UNDO_V2_SOURCE_MACRO,
    UNDO_V2_SOURCE_BUTTON,
    UNDO_V2_SOURCE_CLIPBOARD,
    UNDO_V2_SOURCE_SYSTEM
} undo_v2_source_t;

void undo_v2_init(void);
void undo_v2_clear_all(void);

undo_v2_status_t undo_v2_begin_sequence_transaction(undo_v2_source_t source,
                                                    uint32_t gesture_key,
                                                    seq_track_id_t track,
                                                    const seq_step_id_t *steps,
                                                    uint8_t step_count);
undo_v2_status_t undo_v2_commit_sequence_transaction(void);
void undo_v2_cancel_transaction(void);

undo_v2_status_t undo_v2_undo(void);
undo_v2_status_t undo_v2_redo(void);

undo_v2_status_t undo_v2_get_last_status(void);
uint8_t undo_v2_is_apply_in_progress(void);
uint8_t undo_v2_is_transaction_open(void);
uint8_t undo_v2_is_undo_available(void);
uint8_t undo_v2_is_redo_available(void);
void undo_v2_set_capture_suspended(uint8_t suspended);

#endif /* UNDO_V2_H */
