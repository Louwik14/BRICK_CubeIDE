#ifndef UNDO_V2_H
#define UNDO_V2_H

#include <stdint.h>

#include "Storage/pattern_live_ram.h"
#include "Param/param_store.h"

#define UNDO_V2_MAX_TRANSACTIONS 32U
#define UNDO_V2_MAX_SNAPSHOTS 4U
#define UNDO_V2_MAX_PARAM_DELTAS 128U
#define UNDO_V2_MAX_PLOCK_DELTAS 128U
#define UNDO_V2_MAX_STEP_DELTAS 64U

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
    UNDO_V2_TX_MODE_NONE = 0,
    UNDO_V2_TX_MODE_DELTA,
    UNDO_V2_TX_MODE_SNAPSHOT
} undo_v2_tx_mode_t;

typedef enum
{
    UNDO_V2_TX_KIND_NONE = 0,
    UNDO_V2_TX_KIND_PARAM,
    UNDO_V2_TX_KIND_PLOCK,
    UNDO_V2_TX_KIND_STEP,
    UNDO_V2_TX_KIND_SNAPSHOT
} undo_v2_tx_kind_t;

typedef enum
{
    UNDO_V2_SOURCE_NONE = 0,
    UNDO_V2_SOURCE_ENCODER,
    UNDO_V2_SOURCE_MACRO,
    UNDO_V2_SOURCE_BUTTON,
    UNDO_V2_SOURCE_CLIPBOARD,
    UNDO_V2_SOURCE_SYSTEM
} undo_v2_source_t;

typedef struct
{
    param_id_t param_id;
    uint8_t is_track_aware;
    uint8_t track;
    float before;
    float after;
    uint8_t used;
} undo_v2_param_delta_t;

typedef struct
{
    uint8_t track;
    uint8_t step;
    uint8_t set_id;
    uint8_t param8;
    uint8_t before_present;
    uint16_t before_value16;
    uint8_t before_flags;
    uint8_t before_trig;
    uint8_t after_present;
    uint16_t after_value16;
    uint8_t after_flags;
    uint8_t after_trig;
    uint8_t used;
} undo_v2_plock_delta_t;

typedef struct
{
    uint8_t track;
    uint8_t step;
    uint8_t field_id;
    uint16_t before_value;
    uint16_t after_value;
    uint8_t used;
} undo_v2_step_delta_t;

typedef struct
{
    PatternSaveV1 before_snapshot;
    PatternSaveV1 after_snapshot;
    uint8_t before_valid;
    uint8_t after_valid;
} undo_v2_snapshot_payload_t;

typedef struct
{
    undo_v2_tx_mode_t mode;
    undo_v2_tx_kind_t kind;
    undo_v2_source_t source;
    uint32_t gesture_key;
    uint32_t begin_tick;
    uint32_t end_tick;
    uint16_t payload_index;
    uint16_t payload_count;
    uint8_t committed;
} undo_v2_tx_entry_t;

void undo_v2_init(void);
void undo_v2_clear_all(void);
uint8_t undo_v2_param_is_undoable(param_id_t param_id);

undo_v2_status_t undo_v2_begin_transaction(undo_v2_tx_kind_t kind,
                                           undo_v2_source_t source,
                                           uint32_t gesture_key,
                                           undo_v2_tx_mode_t mode);
undo_v2_status_t undo_v2_commit_transaction(void);
void undo_v2_cancel_transaction(void);

undo_v2_status_t undo_v2_record_param_change(param_id_t param_id,
                                             uint8_t is_track_aware,
                                             uint8_t track,
                                             float before,
                                             float after);
undo_v2_status_t undo_v2_record_plock_change(uint8_t track,
                                             uint8_t step,
                                             uint8_t set_id,
                                             uint8_t param8,
                                             uint8_t before_present,
                                             uint16_t before_value16,
                                             uint8_t before_flags,
                                             uint8_t before_trig,
                                             uint8_t after_present,
                                             uint16_t after_value16,
                                             uint8_t after_flags,
                                             uint8_t after_trig);
undo_v2_status_t undo_v2_record_step_change(uint8_t track,
                                            uint8_t step,
                                            uint8_t field_id,
                                            uint16_t before_value,
                                            uint16_t after_value);

undo_v2_status_t undo_v2_begin_snapshot_transaction(undo_v2_source_t source,
                                                    uint32_t gesture_key);
undo_v2_status_t undo_v2_capture_snapshot_before(void);
undo_v2_status_t undo_v2_capture_snapshot_after(void);

undo_v2_status_t undo_v2_undo(void);
undo_v2_status_t undo_v2_redo(void);

undo_v2_status_t undo_v2_get_last_status(void);
uint8_t undo_v2_is_apply_in_progress(void);
uint8_t undo_v2_is_transaction_open(void);
uint8_t undo_v2_is_undo_available(void);
uint8_t undo_v2_is_redo_available(void);
void undo_v2_set_capture_suspended(uint8_t suspended);

#endif
