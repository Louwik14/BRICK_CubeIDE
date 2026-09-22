#ifndef PERSISTENCE_DEBUG_H
#define PERSISTENCE_DEBUG_H

#include <stdint.h>

typedef enum {
    PERSIST_DBG_OP_NONE = 0,
    PERSIST_DBG_OP_PATTERN_SAVE,
    PERSIST_DBG_OP_PATTERN_LOAD,
    PERSIST_DBG_OP_PATTERN_APPLY,
    PERSIST_DBG_OP_PATTERN_QUEUE,
    PERSIST_DBG_OP_PROJECT_SAVE,
    PERSIST_DBG_OP_PROJECT_LOAD,
    PERSIST_DBG_OP_PROJECT_BLANK
} persist_dbg_op_t;

typedef enum {
    PERSIST_DBG_STAGE_NONE = 0,
    PERSIST_DBG_STAGE_ENTER,
    PERSIST_DBG_STAGE_POLICY,
    PERSIST_DBG_STAGE_LEASE,
    PERSIST_DBG_STAGE_WORKSPACE,
    PERSIST_DBG_STAGE_PATH,
    PERSIST_DBG_STAGE_MOUNT,
    PERSIST_DBG_STAGE_OPEN,
    PERSIST_DBG_STAGE_SIZE,
    PERSIST_DBG_STAGE_READ,
    PERSIST_DBG_STAGE_WRITE,
    PERSIST_DBG_STAGE_ENCODE,
    PERSIST_DBG_STAGE_DECODE,
    PERSIST_DBG_STAGE_VALIDATE,
    PERSIST_DBG_STAGE_READY,
    PERSIST_DBG_STAGE_QUEUE,
    PERSIST_DBG_STAGE_APPLY,
    PERSIST_DBG_STAGE_BANK_STAGE,
    PERSIST_DBG_STAGE_BANK_COMMIT,
    PERSIST_DBG_STAGE_PUBLISH,
    PERSIST_DBG_STAGE_SEQ_SYNC,
    PERSIST_DBG_STAGE_UI_SYNC,
    PERSIST_DBG_STAGE_CLOSE,
    PERSIST_DBG_STAGE_SUCCESS,
    PERSIST_DBG_STAGE_FAIL
} persist_dbg_stage_t;

typedef enum {
    PERSIST_DBG_ERROR_NONE = 0,
    PERSIST_DBG_ERROR_POLICY = 1,
    PERSIST_DBG_ERROR_LEASE = 2,
    PERSIST_DBG_ERROR_WORKSPACE = 3,
    PERSIST_DBG_ERROR_PATH = 4,
    PERSIST_DBG_ERROR_MOUNT = 5,
    PERSIST_DBG_ERROR_FILESYSTEM = 6,
    PERSIST_DBG_ERROR_CODEC = 7,
    PERSIST_DBG_ERROR_VALIDATE = 8,
    PERSIST_DBG_ERROR_BANK = 9,
    PERSIST_DBG_ERROR_APPLY = 10,
    PERSIST_DBG_ERROR_MEDIA = 11,
    PERSIST_DBG_ERROR_INTERNAL = 12
} persist_dbg_error_t;

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t sequence;
    volatile uint32_t op;
    volatile uint32_t stage;
    volatile int32_t status;
    volatile uint32_t first_error_stage;
    volatile int32_t first_error_code;
    volatile uint32_t bank;
    volatile uint32_t slot;
    volatile uint32_t lease_owner;
    volatile uint32_t workspace_owner;
    volatile uint32_t ready;
    volatile uint32_t queue;
    volatile uint32_t publish;
    volatile uint32_t current_pattern;
    volatile uint32_t prepared_pattern;
    volatile uint32_t pattern_revision;
    volatile uint32_t active_track;
    volatile uint32_t selected_track;
    volatile uint32_t ui_revision;
    volatile uint32_t ui_sync;
    volatile uint32_t commit_done;
    volatile uint32_t detail0;
    volatile uint32_t detail1;
    volatile uint32_t detail2;
    volatile uint32_t detail3;
} persist_debug_block_t;

extern volatile persist_debug_block_t g_persist_dbg;

void persist_debug_begin(persist_dbg_op_t op, uint32_t bank, uint32_t slot);
void persist_debug_stage(persist_dbg_stage_t stage, int32_t status);
void persist_debug_error(persist_dbg_stage_t stage, int32_t code);
void persist_debug_owners(uint32_t lease_owner, uint32_t workspace_owner);
void persist_debug_details(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3);
void persist_debug_pattern_state(uint32_t ready, uint32_t queue, uint32_t publish,
                                 uint32_t current, uint32_t prepared,
                                 uint32_t revision);
void persist_debug_ui_sync(uint32_t active_track, uint32_t selected_track,
                           uint32_t revision);

#endif
