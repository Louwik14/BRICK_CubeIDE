#ifndef PERSISTENCE_DEBUG_H
#define PERSISTENCE_DEBUG_H

#include <stdint.h>

typedef enum {
    PERSIST_DBG_OP_NONE = 0,
    PERSIST_DBG_OP_PATTERN_SAVE,
    PERSIST_DBG_OP_PATTERN_LOAD,
    PERSIST_DBG_OP_PATTERN_APPLY,
    PERSIST_DBG_OP_PROJECT_SAVE,
    PERSIST_DBG_OP_PROJECT_LOAD,
    PERSIST_DBG_OP_PROJECT_BLANK
} persist_dbg_op_t;

typedef enum {
    PERSIST_DBG_STAGE_NONE = 0,
    PERSIST_DBG_STAGE_ENTER,
    PERSIST_DBG_STAGE_POLICY,
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
    PERSIST_DBG_STAGE_CANDIDATE,
    PERSIST_DBG_STAGE_ASYNC,
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
    PERSIST_DBG_ERROR_WORKSPACE = 2,
    PERSIST_DBG_ERROR_PATH = 3,
    PERSIST_DBG_ERROR_MOUNT = 4,
    PERSIST_DBG_ERROR_FILESYSTEM = 5,
    PERSIST_DBG_ERROR_CODEC = 6,
    PERSIST_DBG_ERROR_VALIDATE = 7,
    PERSIST_DBG_ERROR_BANK = 8,
    PERSIST_DBG_ERROR_APPLY = 9,
    PERSIST_DBG_ERROR_MEDIA = 10,
    PERSIST_DBG_ERROR_INTERNAL = 11
} persist_dbg_error_t;

typedef enum {
    PERSIST_DBG_DECISION_NONE = 0,
    PERSIST_DBG_DECISION_PREFLIGHT_BLOCKED,
    PERSIST_DBG_DECISION_TRANSPORT_STOPPED_APPLY,
    PERSIST_DBG_DECISION_APPLY_FAILED,
    PERSIST_DBG_DECISION_TRANSPORT_RUNNING_PENDING,
    PERSIST_DBG_DECISION_APPLY_SUCCEEDED,
    PERSIST_DBG_DECISION_WAIT_BOUNDARY
} persist_dbg_decision_reason_t;

typedef enum {
    PERSIST_DBG_PATTERN_PHASE_EMPTY = 0,
    PERSIST_DBG_PATTERN_PHASE_REQUESTED,
    PERSIST_DBG_PATTERN_PHASE_LOADING,
    PERSIST_DBG_PATTERN_PHASE_PENDING
} persist_dbg_pattern_phase_t;

typedef enum {
    PERSIST_DBG_PROJECT_PHASE_NONE = 0,
    PERSIST_DBG_PROJECT_PHASE_SAVE_SNAPSHOT,
    PERSIST_DBG_PROJECT_PHASE_SAVE_MOUNT,
    PERSIST_DBG_PROJECT_PHASE_SAVE_OPEN,
    PERSIST_DBG_PROJECT_PHASE_SAVE_ENCODE,
    PERSIST_DBG_PROJECT_PHASE_SAVE_WRITE,
    PERSIST_DBG_PROJECT_PHASE_SAVE_SYNC,
    PERSIST_DBG_PROJECT_PHASE_SAVE_REPLACE,
    PERSIST_DBG_PROJECT_PHASE_SAVE_CLEANUP,
    PERSIST_DBG_PROJECT_PHASE_LOAD_DECODE,
    PERSIST_DBG_PROJECT_PHASE_LOAD_STAGED,
    PERSIST_DBG_PROJECT_PHASE_LOAD_WAIT_QUIESCE,
    PERSIST_DBG_PROJECT_PHASE_LOAD_ASSETS,
    PERSIST_DBG_PROJECT_PHASE_LOAD_APPLY,
    PERSIST_DBG_PROJECT_PHASE_BLANK_BUILD,
    PERSIST_DBG_PROJECT_PHASE_DONE
} persist_dbg_project_phase_t;

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t word_count;
    volatile uint32_t sequence;
    volatile uint32_t op;
    volatile uint32_t stage;
    volatile int32_t status;
    volatile uint32_t first_error_stage;
    volatile int32_t first_error_code;
    volatile uint32_t requested_pattern;
    volatile uint32_t candidate_pattern;
    volatile uint32_t current_pattern;
    volatile uint32_t candidate_phase;
    volatile uint32_t request_generation;
    volatile uint32_t io_generation;
    volatile uint32_t boundary_track;
    volatile uint32_t boundary_armed;
    volatile uint32_t boundary_generation;
    volatile uint32_t boundary_observed_generation;
    volatile uint32_t boundary_due;
    volatile uint32_t transport_running;
    volatile uint32_t decision_reason;
    volatile uint32_t apply_attempted;
    volatile int32_t apply_result;
    volatile uint32_t audio_publish;
    volatile uint32_t seq_publish;
    volatile uint32_t ui_sync;
    volatile uint32_t active_track;
    volatile uint32_t selected_track;
    volatile uint32_t ui_revision;
    volatile uint32_t workspace_owner;
    volatile uint32_t project_phase;
    volatile uint32_t commit_done;
    volatile uint32_t project_progress;
    volatile uint32_t detail;
    volatile uint32_t detail0;
    volatile uint32_t detail1;
    volatile uint32_t detail2;
    volatile uint32_t detail3;
} persist_debug_block_t;

_Static_assert(sizeof(persist_debug_block_t) == 156U,
               "Persistence debug v3 layout changed");

extern volatile persist_debug_block_t g_persist_dbg;

void persist_debug_begin(persist_dbg_op_t op, uint32_t bank, uint32_t slot);
void persist_debug_stage(persist_dbg_stage_t stage, int32_t status);
void persist_debug_error(persist_dbg_stage_t stage, int32_t code);
void persist_debug_workspace_owner(uint32_t workspace_owner);
void persist_debug_details(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3);
void persist_debug_pattern_state(uint32_t candidate_phase,
                                 uint32_t current, uint32_t candidate,
                                 uint32_t request_generation,
                                 uint32_t io_generation,
                                 uint32_t boundary_track,
                                 uint32_t boundary_armed,
                                 uint32_t boundary_generation);
void persist_debug_publication(uint8_t audio, uint8_t seq);
void persist_debug_project(persist_dbg_project_phase_t phase,
                           uint32_t progress, uint32_t detail);
void persist_debug_ui_sync(uint32_t active_track, uint32_t selected_track,
                           uint32_t revision);

#endif
