#include "Storage/persistence_debug.h"

#define PERSIST_DEBUG_MAGIC UINT32_C(0x50444247) /* "PDBG" */

#if defined(__GNUC__)
#define PERSIST_DEBUG_ATTR __attribute__((section(".data.persist_debug"), used, aligned(4)))
#else
#define PERSIST_DEBUG_ATTR
#endif

PERSIST_DEBUG_ATTR volatile persist_debug_block_t g_persist_dbg = {
    .magic = PERSIST_DEBUG_MAGIC,
    .version = 3U
};

void persist_debug_begin(persist_dbg_op_t op, uint32_t bank, uint32_t slot)
{
    const uint32_t next = g_persist_dbg.sequence + 1U;
    g_persist_dbg.op = (uint32_t)op;
    g_persist_dbg.stage = PERSIST_DBG_STAGE_ENTER;
    g_persist_dbg.status = 0;
    g_persist_dbg.first_error_stage = PERSIST_DBG_STAGE_NONE;
    g_persist_dbg.first_error_code = PERSIST_DBG_ERROR_NONE;
    g_persist_dbg.bank = bank;
    g_persist_dbg.slot = slot;
    g_persist_dbg.candidate_phase = 0U;
    g_persist_dbg.publish = 0U;
    g_persist_dbg.pending_pattern = 0U;
    g_persist_dbg.request_generation = 0U;
    g_persist_dbg.boundary_generation = 0U;
    g_persist_dbg.commit_done = 0U;
    g_persist_dbg.detail0 = 0U;
    g_persist_dbg.detail1 = 0U;
    g_persist_dbg.detail2 = 0U;
    g_persist_dbg.detail3 = 0U;
    g_persist_dbg.transport_running = 0U;
    g_persist_dbg.apply_attempted = 0U;
    g_persist_dbg.apply_result = 0U;
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_NONE;
    g_persist_dbg.sequence = next;
}

void persist_debug_stage(persist_dbg_stage_t stage, int32_t status)
{ g_persist_dbg.stage=(uint32_t)stage;g_persist_dbg.status=status; }

void persist_debug_error(persist_dbg_stage_t stage, int32_t code)
{
    g_persist_dbg.stage=PERSIST_DBG_STAGE_FAIL;g_persist_dbg.status=code;
    if(g_persist_dbg.first_error_stage==PERSIST_DBG_STAGE_NONE){g_persist_dbg.first_error_stage=(uint32_t)stage;g_persist_dbg.first_error_code=code;}
}

void persist_debug_workspace_owner(uint32_t workspace_owner)
{g_persist_dbg.workspace_owner=workspace_owner;}
void persist_debug_details(uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{g_persist_dbg.detail0=a;g_persist_dbg.detail1=b;g_persist_dbg.detail2=c;g_persist_dbg.detail3=d;}
void persist_debug_pattern_state(uint32_t phase,uint32_t publish,uint32_t current,uint32_t pending,uint32_t request_generation,uint32_t boundary_generation)
{g_persist_dbg.candidate_phase=phase;g_persist_dbg.publish=publish;g_persist_dbg.current_pattern=current;g_persist_dbg.pending_pattern=pending;g_persist_dbg.request_generation=request_generation;g_persist_dbg.boundary_generation=boundary_generation;}
void persist_debug_ui_sync(uint32_t a,uint32_t s,uint32_t r)
{g_persist_dbg.active_track=a;g_persist_dbg.selected_track=s;g_persist_dbg.ui_revision=r;++g_persist_dbg.ui_sync;persist_debug_stage(PERSIST_DBG_STAGE_UI_SYNC,0);}
