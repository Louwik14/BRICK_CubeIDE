#include "Storage/persistence_debug.h"

#define PERSIST_DEBUG_MAGIC UINT32_C(0x50444247) /* "PDBG" */

#if defined(__GNUC__)
#define PERSIST_DEBUG_ATTR __attribute__((section(".data.persist_debug"), used, aligned(4)))
#else
#define PERSIST_DEBUG_ATTR
#endif

PERSIST_DEBUG_ATTR volatile persist_debug_block_t g_persist_dbg = {
    .magic = PERSIST_DEBUG_MAGIC,
    .version = 4U,
    .word_count = sizeof(persist_debug_block_t) / sizeof(uint32_t)
};

void persist_debug_begin(persist_dbg_op_t op, uint32_t bank, uint32_t slot)
{
    const uint32_t next = g_persist_dbg.sequence + 1U;
    g_persist_dbg.op = (uint32_t)op;
    g_persist_dbg.stage = PERSIST_DBG_STAGE_ENTER;
    g_persist_dbg.status = 0;
    g_persist_dbg.first_error_stage = PERSIST_DBG_STAGE_NONE;
    g_persist_dbg.first_error_code = PERSIST_DBG_ERROR_NONE;
    g_persist_dbg.requested_pattern = (bank << 16U) | slot;
    g_persist_dbg.candidate_pattern = 0U;
    g_persist_dbg.candidate_phase = PERSIST_DBG_PATTERN_PHASE_EMPTY;
    g_persist_dbg.request_generation = 0U;
    g_persist_dbg.io_generation = 0U;
    g_persist_dbg.boundary_track = 0U;
    g_persist_dbg.boundary_armed = 0U;
    g_persist_dbg.boundary_generation = 0U;
    g_persist_dbg.boundary_observed_generation = 0U;
    g_persist_dbg.boundary_due = 0U;
    g_persist_dbg.commit_done = 0U;
    g_persist_dbg.project_phase = PERSIST_DBG_PROJECT_PHASE_NONE;
    g_persist_dbg.project_progress = 0U;
    g_persist_dbg.detail = 0U;
    g_persist_dbg.detail0 = 0U;
    g_persist_dbg.detail1 = 0U;
    g_persist_dbg.detail2 = 0U;
    g_persist_dbg.detail3 = 0U;
    g_persist_dbg.transport_running = 0U;
    g_persist_dbg.apply_attempted = 0U;
    g_persist_dbg.apply_result = 0U;
    g_persist_dbg.audio_publish = 0U;
    g_persist_dbg.seq_publish = 0U;
    g_persist_dbg.ui_sync = 0U;
    g_persist_dbg.decision_reason = PERSIST_DBG_DECISION_NONE;
    g_persist_dbg.validation_step = PERSIST_DBG_VALIDATION_NONE;
    g_persist_dbg.validation_result = 0;
    g_persist_dbg.entity_id = UINT32_MAX;
    g_persist_dbg.entity_type = 0U;
    g_persist_dbg.current_runtime_type = 0U;
    g_persist_dbg.target_runtime_type = 0U;
    g_persist_dbg.current_engine = 0U;
    g_persist_dbg.target_engine = 0U;
    g_persist_dbg.current_voice_count = 0U;
    g_persist_dbg.target_voice_count = 0U;
    g_persist_dbg.candidate_generation = 0U;
    g_persist_dbg.cancel_reason = PERSIST_DBG_CANCEL_NONE;
    g_persist_dbg.object_kind = PERSIST_DBG_OBJECT_NONE;
    g_persist_dbg.object_index = 0U;
    g_persist_dbg.object_type = 0U;
    g_persist_dbg.last_fresult = 0;
    g_persist_dbg.codec_offset = 0U;
    g_persist_dbg.audio_publish_result = 0U;
    g_persist_dbg.seq_publish_result = 0U;
    g_persist_dbg.ui_sync_reason = PERSIST_DBG_UI_SYNC_NONE;
    g_persist_dbg.active_track_before = 0U;
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
void persist_debug_pattern_state(uint32_t phase,uint32_t current,uint32_t candidate,uint32_t request_generation,uint32_t io_generation,uint32_t boundary_track,uint32_t boundary_armed,uint32_t boundary_generation)
{g_persist_dbg.candidate_phase=phase;g_persist_dbg.current_pattern=current;g_persist_dbg.candidate_pattern=candidate;g_persist_dbg.request_generation=request_generation;g_persist_dbg.io_generation=io_generation;g_persist_dbg.boundary_track=boundary_track;g_persist_dbg.boundary_armed=boundary_armed;g_persist_dbg.boundary_generation=boundary_generation;}
void persist_debug_publication(uint8_t audio,uint8_t seq)
{if(audio!=0U)++g_persist_dbg.audio_publish;if(seq!=0U)++g_persist_dbg.seq_publish;}
void persist_debug_project(persist_dbg_project_phase_t phase,uint32_t progress,uint32_t detail)
{g_persist_dbg.project_phase=(uint32_t)phase;g_persist_dbg.project_progress=progress;g_persist_dbg.detail=detail;}
void persist_debug_validation_fail(persist_dbg_validation_step_t step,int32_t result,uint32_t entity,uint32_t entity_type,uint32_t current_runtime_type,uint32_t target_runtime_type,uint32_t current_engine,uint32_t target_engine,uint32_t current_voice_count,uint32_t target_voice_count)
{if(g_persist_dbg.validation_step!=PERSIST_DBG_VALIDATION_NONE)return;g_persist_dbg.validation_step=(uint32_t)step;g_persist_dbg.validation_result=result;g_persist_dbg.entity_id=entity;g_persist_dbg.entity_type=entity_type;g_persist_dbg.current_runtime_type=current_runtime_type;g_persist_dbg.target_runtime_type=target_runtime_type;g_persist_dbg.current_engine=current_engine;g_persist_dbg.target_engine=target_engine;g_persist_dbg.current_voice_count=current_voice_count;g_persist_dbg.target_voice_count=target_voice_count;}
void persist_debug_object(persist_dbg_object_kind_t kind,uint32_t index,uint32_t type)
{g_persist_dbg.object_kind=(uint32_t)kind;g_persist_dbg.object_index=index;g_persist_dbg.object_type=type;}
void persist_debug_filesystem(int32_t fresult,uint32_t codec_offset)
{g_persist_dbg.last_fresult=fresult;g_persist_dbg.codec_offset=codec_offset;}
void persist_debug_ui_sync(uint32_t a,uint32_t s,uint32_t r)
{g_persist_dbg.active_track=a;g_persist_dbg.selected_track=s;g_persist_dbg.ui_revision=r;++g_persist_dbg.ui_sync;persist_debug_stage(PERSIST_DBG_STAGE_UI_SYNC,0);}
