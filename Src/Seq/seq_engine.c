#include "Seq/seq_engine.h"
#include "Seq/seq_runtime.h"
#include "NoteFx/note_fx_engine.h"
#include "Param/param_ids.h"
#include "Param/param_registry.h"
#include "Param/param_value_policy.h"
#include "IPC/live_parameter_event.h"
#include "Platform/memory_layout.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static SEQ_STATE_D2 note_event_t g_seq_fx_a[NOTE_FX_BATCH_CAPACITY];
static SEQ_HOT_D1 note_event_t g_seq_fx_b[NOTE_FX_BATCH_CAPACITY];
static SEQ_STATE_SDRAM note_event_t g_seq_fx_cohort[NOTE_FX_BATCH_CAPACITY];
static SEQ_STATE_SDRAM note_event_t g_seq_source_cohort[NOTE_FX_BATCH_CAPACITY];
typedef struct {uint8_t bank;uint16_t lane;uint32_t order;} seq_source_due_ref_t;
static seq_source_due_ref_t g_seq_source_due_ref[NOTE_FX_BATCH_CAPACITY];
static seq_engine_core_t *g_seq_fx_core;
static seq_terminal_block_t *g_seq_fx_block;
static uint64_t g_seq_fx_start;
static uint64_t g_seq_fx_end;
typedef struct {uint32_t source_id;uint8_t track,lane,active;} seq_live_lane_t;
static SEQ_STATE_D2 seq_live_lane_t g_seq_live_lane[SEQ_ENGINE_LEDGER_CAPACITY];
static void seq_drop(seq_engine_core_t*core)
{++core->dropped_events;}

static void terminal_reset(seq_terminal_block_t *block,uint64_t start,
    uint16_t frames,uint32_t generation)
{block->start_sample=start;block->active_offsets=0U;block->block_id=0U;
 block->generation=generation;block->emitter_tracks=0U;block->lock_tracks=0U;
 block->event_count=0U;block->frames=frames;
 memset(block->head,0xFF,sizeof(block->head));
 memset(block->tail,0xFF,sizeof(block->tail));}

static uint8_t terminal_push(seq_terminal_block_t *block,uint16_t offset,
    uint8_t kind,const seq_terminal_event_t *event)
{
 if(block==0||event==0||offset>=block->frames
      ||offset>=SEQ_ENGINE_H743_PERIOD_SAMPLES
      ||kind>=SEQ_ENGINE_TERMINAL_CLASS_COUNT
      ||block->event_count>=SEQ_ENGINE_TERMINAL_CAPACITY){return 0U;}
 const uint16_t index=block->event_count++;
 block->events[index]=*event;block->next[index]=SEQ_ENGINE_TERMINAL_INDEX_NONE;
 const uint16_t tail=block->tail[offset][kind];
 if(tail==SEQ_ENGINE_TERMINAL_INDEX_NONE)block->head[offset][kind]=index;
 else block->next[tail]=index;
 block->tail[offset][kind]=index;block->active_offsets|=UINT64_C(1)<<offset;
 return 1U;}

static uint32_t terminal_param_value32(uint16_t param_id,uint16_t value16)
{const float value=param_value_policy_decode_u16(&param_registry[param_id],value16);
 return(uint32_t)live_parameter_event_encode_float(value);}
typedef struct {uint32_t occurrence_id,duration_samples,source_id;
 uint8_t note,velocity,flags,provenance;} seq_deferred_event_t;
typedef struct {uint64_t due_sample;seq_deferred_event_t event[SEQ_PRODUCT_HARMONY_FANOUT_MAX];
 uint8_t count;} seq_terminal_deferred_t;
static SEQ_STATE_SDRAM seq_terminal_deferred_t
    g_terminal_deferred[SEQ_PRODUCT_MAX_EMITTING_VOICES]
                       [SEQ_PRODUCT_DEFERRED_BATCHES_PER_LANE];
static uint64_t g_deferred_active[SEQ_PRODUCT_DEFERRED_BATCHES_PER_LANE];
_Static_assert(sizeof(seq_terminal_deferred_t)<=80U,"deferred batch budget");

static uint16_t product_lane_from_track_slot(uint8_t track,uint8_t slot)
{if(track<BRICK_ENTITY_GROUP_MASTER_ID&&slot<SEQ_LOGICAL_CAPACITY_MAX)
    return(uint16_t)((uint16_t)track*SEQ_LOGICAL_CAPACITY_MAX+slot);
 if(track>=BRICK_ENTITY_FIRST_GROUP_CHILD_ID&&track<SEQ_LANE_CAPACITY&&slot==0U)
    return(uint16_t)(((BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX)
        +(track-BRICK_ENTITY_FIRST_GROUP_CHILD_ID));
 return UINT16_MAX;}
static uint8_t product_track_from_lane(uint16_t lane)
{return(lane<(BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX)
 ?(uint8_t)(lane/SEQ_LOGICAL_CAPACITY_MAX)
 :(uint8_t)(BRICK_ENTITY_FIRST_GROUP_CHILD_ID+lane
 -(BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX);}
static uint8_t product_slot_from_lane(uint16_t lane)
{return(lane<(BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX)
 ?(uint8_t)(lane%SEQ_LOGICAL_CAPACITY_MAX):0U;}

static int16_t ledger_find(const seq_engine_core_t *core,uint32_t occurrence)
{for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i)
    if(((core->ledger_active>>i)&UINT64_C(1))!=0U
            &&core->ledger[i].occurrence_id==occurrence)return(int16_t)i;
 return -1;}

static void ledger_release(seq_engine_core_t *core,uint8_t index)
{if(index>=SEQ_ENGINE_LEDGER_CAPACITY
        ||((core->ledger_active>>index)&UINT64_C(1))==0U)return;
 const uint8_t track=product_track_from_lane(index);
 core->ledger_active&=~(UINT64_C(1)<<index);
 if(core->ledger_count)--core->ledger_count;
 if(core->ledger_track_count[track])--core->ledger_track_count[track];}

static uint8_t terminal_deferred_store(seq_engine_core_t *core,const note_event_t *event)
{(void)core;const uint16_t lane=product_lane_from_track_slot(event->track,event->temporal_index);
 if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES)return 0U;
 int8_t target=-1,victim=0,generated_victim=-1;
 for(uint8_t i=0;i<SEQ_PRODUCT_DEFERRED_BATCHES_PER_LANE;++i){
  seq_terminal_deferred_t*x=&g_terminal_deferred[lane][i];
  if(((g_deferred_active[i]>>lane)&1U)==0U){target=(int8_t)i;break;}
  if(x->due_sample==event->sample_abs&&x->count<SEQ_PRODUCT_HARMONY_FANOUT_MAX){target=(int8_t)i;break;}
  if(x->due_sample<g_terminal_deferred[lane][(uint8_t)victim].due_sample)victim=(int8_t)i;
  uint8_t generated=1U;for(uint8_t n=0;n<x->count;++n)
   if((x->event[n].flags&NOTE_EVENT_FLAG_GENERATED)==0U)generated=0U;
  if(generated)generated_victim=(int8_t)i;}
 if(target<0)target=(generated_victim>=0)?generated_victim:victim;
 seq_terminal_deferred_t*x=&g_terminal_deferred[lane][(uint8_t)target];
 if(((g_deferred_active[(uint8_t)target]>>lane)&1U)==0U||x->due_sample!=event->sample_abs)
  {*x=(seq_terminal_deferred_t){.due_sample=event->sample_abs};
   g_deferred_active[(uint8_t)target]|=UINT64_C(1)<<lane;}
 if(x->count>=SEQ_PRODUCT_HARMONY_FANOUT_MAX)return 0U;
 x->event[x->count++]=(seq_deferred_event_t){.occurrence_id=event->occurrence_id,
  .duration_samples=event->duration_samples,.source_id=event->source_id,
  .note=event->note,.velocity=event->velocity,.flags=event->flags,
  .provenance=event->provenance};return 1U;}

typedef struct {int16_t target,victim;uint8_t logical_slot;} seq_ledger_plan_t;
static uint8_t ledger_plan(const seq_engine_core_t *core,const note_event_t *event,
    seq_ledger_plan_t *plan)
{
    const uint8_t quota=core->logical_capacity[event->track];
    if(quota==0U||plan==0)return 0U;
    int16_t free_index=-1,victim=-1;uint8_t logical_slot=0U;
    for(;logical_slot<quota;++logical_slot){const uint16_t lane=
      product_lane_from_track_slot(event->track,logical_slot);
     if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES)continue;
     if(((core->ledger_active>>lane)&UINT64_C(1))==0U){free_index=(int16_t)lane;break;}
     const seq_ledger_entry_t*entry=&core->ledger[lane];
     if(victim<0||((core->ledger[(uint8_t)victim].original!=0U)&&(entry->original==0U))
       ||((entry->original==core->ledger[(uint8_t)victim].original)
       &&entry->admitted_sample<core->ledger[(uint8_t)victim].admitted_sample))victim=(int16_t)lane;}
    if(free_index<0){if(victim<0)return 0U;
     if((event->flags&NOTE_EVENT_FLAG_GENERATED)!=0U
       &&core->ledger[(uint8_t)victim].original!=0U)return 2U;
     free_index=victim;logical_slot=product_slot_from_lane((uint16_t)victim);}
    else victim=-1;
    *plan=(seq_ledger_plan_t){.target=free_index,.victim=victim,.logical_slot=logical_slot};return 1U;
}

static void ledger_commit(seq_engine_core_t *core,const note_event_t *event,
    const seq_ledger_plan_t *plan)
{
    if(plan->victim>=0)ledger_release(core,(uint8_t)plan->victim);
    core->ledger[(uint8_t)plan->target]=(seq_ledger_entry_t){
        .admitted_sample=event->sample_abs,
        .due_off=((event->flags&NOTE_EVENT_FLAG_HELD)!=0U)
            ?UINT64_MAX:event->sample_abs
                +(event->duration_samples?event->duration_samples:1U),
        .occurrence_id=event->occurrence_id,.note=event->note,
        .original=(uint8_t)(((event->flags&NOTE_EVENT_FLAG_GENERATED)==0U)?1U:0U)};
    core->ledger_active|=UINT64_C(1)<<(uint8_t)plan->target;
    ++core->ledger_count;++core->ledger_track_count[event->track];
}

static void ledger_retire_outside_capacity(seq_engine_core_t *core,
    seq_terminal_block_t *out)
{
    for(uint8_t lane=0U;lane<SEQ_ENGINE_LEDGER_CAPACITY;++lane){
        if(((core->ledger_active>>lane)&UINT64_C(1))==0U)continue;
        const uint8_t track=product_track_from_lane(lane);
        if(product_slot_from_lane(lane)<core->logical_capacity[track])continue;
        const seq_terminal_event_t terminal={.note={
            .occurrence_id=core->ledger[lane].occurrence_id,
            .track=track,.note=core->ledger[lane].note,
            .logical_slot=product_slot_from_lane(lane)}};
        if(terminal_push(out,0U,SEQ_ENGINE_EVENT_NOTE_OFF,&terminal)!=0U)
            out->emitter_tracks|=(uint16_t)(1U<<track);
        ledger_release(core,lane);
    }
}

static void fx_terminal(const note_event_t *e)
{
    uint64_t due=e->sample_abs;
    if(due<g_seq_fx_start)due=g_seq_fx_start;
    if(due>=g_seq_fx_end){note_event_t resume=*e;resume.sample_abs=due;
        if(!terminal_deferred_store(g_seq_fx_core,&resume))
            seq_drop(g_seq_fx_core);
        return;}
    if(e->kind==NOTE_EVENT_KIND_OFF){const int16_t found=ledger_find(g_seq_fx_core,e->occurrence_id);
        if(found<0){return;}
        const seq_terminal_event_t terminal={.note={
            .occurrence_id=e->occurrence_id,.track=e->track,.note=e->note,
            .logical_slot=product_slot_from_lane((uint16_t)found)}};
        if(!terminal_push(g_seq_fx_block,(uint16_t)(due-g_seq_fx_start),
                SEQ_ENGINE_EVENT_NOTE_OFF,&terminal)){
            seq_drop(g_seq_fx_core);return;}
        ledger_release(g_seq_fx_core,(uint8_t)found);
        const uint32_t ns=e->source_id&~NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
        if(ns==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY||ns==NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI)
            (void)seq_runtime_live_rec_submit_effective(
                (ns==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)?SEQ_LIVE_REC_SRC_INTERNAL:SEQ_LIVE_REC_SRC_EXTERNAL,
                0U,0U,e->note,e->velocity,due,e->occurrence_id,e->occurrence_id);
        return;}
    seq_ledger_plan_t plan;const uint8_t admission=ledger_plan(g_seq_fx_core,e,&plan);
    if(admission==2U){return;}
    if(admission==0U){
        seq_drop(g_seq_fx_core);return;}
    const uint16_t required=(uint16_t)(1U+(plan.victim>=0?1U:0U));
    if((uint32_t)g_seq_fx_block->event_count+required>SEQ_ENGINE_TERMINAL_CAPACITY){
        seq_drop(g_seq_fx_core);return;}
    if(e->duration_samples==0U){seq_drop(g_seq_fx_core);return;}
    if(plan.victim>=0){const seq_ledger_entry_t old=g_seq_fx_core->ledger[(uint8_t)plan.victim];
        const seq_terminal_event_t terminal={.note={.occurrence_id=old.occurrence_id,
            .track=product_track_from_lane((uint16_t)plan.victim),.note=old.note,
            .logical_slot=product_slot_from_lane((uint16_t)plan.victim)}};
        (void)terminal_push(g_seq_fx_block,(uint16_t)(due-g_seq_fx_start),
            SEQ_ENGINE_EVENT_NOTE_OFF,&terminal);}
    ledger_commit(g_seq_fx_core,e,&plan);
    const seq_terminal_event_t terminal={.note={.occurrence_id=e->occurrence_id,
        .track=e->track,.note=e->note,.velocity=e->velocity,
        .logical_slot=plan.logical_slot}};
    (void)terminal_push(g_seq_fx_block,(uint16_t)(due-g_seq_fx_start),
        SEQ_ENGINE_EVENT_NOTE_ON,&terminal);
    const uint32_t source_namespace=e->source_id&~NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if(source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY
            ||source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI)
        (void)seq_runtime_live_rec_submit_effective(
            (source_namespace==NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY)
                ?SEQ_LIVE_REC_SRC_INTERNAL:SEQ_LIVE_REC_SRC_EXTERNAL,
            (uint8_t)(e->kind==NOTE_EVENT_KIND_ON),0U,e->note,e->velocity,due,
            e->occurrence_id,e->occurrence_id);
}

static uint8_t ledger_generated_admission_possible(const seq_engine_core_t *core,
    uint8_t track)
{const uint8_t quota=core->logical_capacity[track];
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);
  if(lane>=SEQ_ENGINE_LEDGER_CAPACITY)continue;
  if(((core->ledger_active>>lane)&UINT64_C(1))==0U
       ||core->ledger[lane].original==0U)return 1U;}
 return 0U;}

static uint8_t walker_harm_capacity(const note_event_t *events,uint8_t count,
    uint8_t slot,uint8_t frontier_exact)
{const uint8_t track=events[0].track;const uint8_t quota=g_seq_fx_core->logical_capacity[track];
 if(quota==0U)return 0U;
 if(frontier_exact==0U||note_fx_engine_suffix_is_temporal(track,(uint8_t)(slot+1U))!=0U)return quota;
 uint8_t generated_live=0U;
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);
  if(lane<SEQ_ENGINE_LEDGER_CAPACITY&&((g_seq_fx_core->ledger_active>>lane)&1U)!=0U
       &&g_seq_fx_core->ledger[lane].original==0U)++generated_live;}
 const uint8_t free_slots=(g_seq_fx_core->ledger_track_count[track]<quota)
   ?(uint8_t)(quota-g_seq_fx_core->ledger_track_count[track]):0U;
 uint8_t original_roots=0U,generated_roots=0U;
 for(uint8_t i=0U;i<count;++i){uint8_t duplicate=0U;
  for(uint8_t j=0U;j<i;++j)if(events[j].group_id==events[i].group_id
       &&events[j].note==events[i].note)duplicate=1U;
  if(duplicate)continue;
  if((events[i].flags&NOTE_EVENT_FLAG_GENERATED)!=0U)++generated_roots;
  else ++original_roots;}
 const uint8_t generated_room=(uint8_t)(free_slots+generated_live);
 const uint8_t after_original=(generated_room>original_roots)
   ?(uint8_t)(generated_room-original_roots):0U;
 const uint8_t admitted_generated_roots=(generated_roots<after_original)
   ?generated_roots:after_original;
 const uint8_t remaining_generated=(uint8_t)(after_original-admitted_generated_roots);
 uint16_t limit=(uint16_t)original_roots+admitted_generated_roots+remaining_generated;
 if(limit>quota)limit=quota;
 return(uint8_t)limit;}

static note_event_result_t walker_resume_batch(const note_event_t *source,
    uint8_t source_count,uint8_t stage,uint8_t frontier_exact)
{
    if(source==0||source_count==0U||source_count>NOTE_FX_BATCH_CAPACITY
          ||!note_event_is_valid(&source[0])||source[0].track>=NOTE_FX_TRACK_COUNT
          ||source[0].stage!=stage)return NOTE_EVENT_RESULT_DROPPED_POLICY;
    for(uint8_t i=1U;i<source_count;++i)if(!note_event_is_valid(&source[i])
          ||source[i].track!=source[0].track||source[i].stage!=stage
          ||source[i].kind!=source[0].kind)return NOTE_EVENT_RESULT_DROPPED_POLICY;
    uint8_t count=source_count;const note_event_t *in=source;
    note_event_t *out=(source==g_seq_fx_a)?g_seq_fx_b:g_seq_fx_a;
    for(uint8_t slot=note_fx_engine_next_active_slot(source[0].track,stage);
          slot<NOTE_FX_SLOT_COUNT;
          slot=note_fx_engine_next_active_slot(source[0].track,(uint8_t)(slot+1U))){
        uint8_t out_count=0U;
        uint8_t transform_capacity=NOTE_FX_BATCH_CAPACITY;
        if(note_fx_plan_model(g_seq_fx_core->fx_effective[in[0].track][slot])
              ==NOTE_FX_MODEL_HARMONIZER)
            transform_capacity=walker_harm_capacity(in,count,slot,frontier_exact);
        const note_event_result_t r=note_fx_engine_transform_prepared(slot,in,count,
            out,transform_capacity,&out_count);
        if(r!=NOTE_EVENT_RESULT_ACCEPTED){return r;}
        count=out_count;in=out;out=(out==g_seq_fx_a)?g_seq_fx_b:g_seq_fx_a;
        if(count==0U){return NOTE_EVENT_RESULT_ACCEPTED;}}
    for(uint8_t i=0U;i<count;++i)fx_terminal(&in[i]);
    return NOTE_EVENT_RESULT_ACCEPTED;
}

static note_event_result_t walker_resume(const note_event_t *source,uint8_t stage,
    uint8_t frontier_exact)
{return walker_resume_batch(source,1U,stage,frontier_exact);}

static note_event_result_t walker_prefix(const note_event_t *source,uint8_t stage,
    uint8_t stop_slot,note_event_t *destination,uint8_t *destination_count)
{uint8_t count=1U;const note_event_t*in=source;note_event_t*out=g_seq_fx_a;
 for(uint8_t slot=note_fx_engine_next_active_slot(source->track,stage);
      slot<stop_slot;
      slot=note_fx_engine_next_active_slot(source->track,(uint8_t)(slot+1U))){uint8_t out_count=0U;
  const note_event_result_t result=note_fx_engine_transform_prepared(slot,in,count,out,
      NOTE_FX_BATCH_CAPACITY,&out_count);
  if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;
  count=out_count;in=out;out=(out==g_seq_fx_a)?g_seq_fx_b:g_seq_fx_a;if(count==0U)break;}
 if((uint16_t)*destination_count+count>NOTE_FX_BATCH_CAPACITY)
  return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
 memcpy(&destination[*destination_count],in,(size_t)count*sizeof(*in));
 *destination_count=(uint8_t)(*destination_count+count);
 return NOTE_EVENT_RESULT_ACCEPTED;}

static note_event_result_t walker_harm_cohort(const note_event_t *source,
    uint8_t source_count,uint8_t harm_slot,uint8_t frontier_exact)
{uint8_t cohort_count=0U;
 for(uint8_t i=0U;i<source_count;++i){const note_event_result_t result=
   walker_prefix(&source[i],source[i].stage,harm_slot,g_seq_fx_cohort,&cohort_count);
  if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}
 if(cohort_count==0U)return NOTE_EVENT_RESULT_ACCEPTED;
 const uint8_t cap=walker_harm_capacity(g_seq_fx_cohort,cohort_count,
     harm_slot,frontier_exact);
 uint8_t harm_count=0U;
 const note_event_result_t harm_result=note_fx_engine_transform_prepared(harm_slot,
     g_seq_fx_cohort,cohort_count,g_seq_fx_a,cap,&harm_count);
 if(harm_result!=NOTE_EVENT_RESULT_ACCEPTED)return harm_result;
 memcpy(g_seq_fx_cohort,g_seq_fx_a,(size_t)harm_count*sizeof(g_seq_fx_a[0]));
 uint8_t consumed[NOTE_FX_BATCH_CAPACITY]={0U};
 for(uint8_t i=0U;i<harm_count;++i){if(consumed[i])continue;
  note_event_t group[SEQ_PRODUCT_HARMONY_FANOUT_MAX];uint8_t group_count=0U;
  for(uint8_t j=i;j<harm_count;++j)if(!consumed[j]
       &&g_seq_fx_cohort[j].group_id==g_seq_fx_cohort[i].group_id){
    if(group_count<SEQ_PRODUCT_HARMONY_FANOUT_MAX)group[group_count++]=g_seq_fx_cohort[j];
    consumed[j]=1U;}
  const note_event_result_t result=walker_resume_batch(group,group_count,
      (uint8_t)(harm_slot+1U),frontier_exact);
  if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}
 return NOTE_EVENT_RESULT_ACCEPTED;}

static note_event_result_t fx_generated(const note_event_t *event,void *ctx)
{(void)ctx;
 if((event->flags&NOTE_EVENT_FLAG_GENERATED)!=0U
      &&(event->flags&NOTE_EVENT_FLAG_ECHO)==0U
      &&note_fx_engine_suffix_is_temporal(event->track,event->stage)==0U
      &&ledger_generated_admission_possible(g_seq_fx_core,event->track)==0U)
  return NOTE_EVENT_RESULT_ACCEPTED;
 return walker_resume(event,event->stage,0U);}

static uint8_t live_lane_bind(const seq_engine_core_t *core,note_event_t *event,
    int16_t *binding,uint8_t *created)
{
    *binding=-1;*created=0U;
    const uint32_t source_namespace=event->source_id
        &~NOTE_EVENT_OCCURRENCE_COUNTER_MASK;
    if(source_namespace!=NOTE_EVENT_OCCURRENCE_NAMESPACE_KEY
            &&source_namespace!=NOTE_EVENT_OCCURRENCE_NAMESPACE_MIDI)return 1U;
    for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i)
        if(g_seq_live_lane[i].active&&g_seq_live_lane[i].source_id==event->source_id){
            event->temporal_index=g_seq_live_lane[i].lane;*binding=(int16_t)i;return 1U;}
    if(event->kind==NOTE_EVENT_KIND_OFF)return 1U;
    const uint8_t quota=core->logical_capacity[event->track];
    uint8_t used=0U;int16_t free_index=-1;
    for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i){
        if(!g_seq_live_lane[i].active){if(free_index<0)free_index=(int16_t)i;continue;}
        if(g_seq_live_lane[i].track==event->track)
            used|=(uint8_t)(1U<<g_seq_live_lane[i].lane);}
    uint8_t lane=0U;while(lane<quota&&(used&(uint8_t)(1U<<lane)))++lane;
    if(free_index<0||lane>=quota)return 0U;
    g_seq_live_lane[(uint8_t)free_index]=(seq_live_lane_t){
        .source_id=event->source_id,.track=event->track,.lane=lane,.active=1U};
    event->temporal_index=lane;*binding=free_index;*created=1U;return 1U;
}

uint8_t seq_engine_core_submit_live(seq_engine_core_t *core,
    const note_event_t *event,const seq_pattern_t *pattern,
    uint64_t window_start,uint64_t window_end,
    seq_terminal_block_t *out_block)
{
    if((core==0)||(event==0)||(pattern==0)||(out_block==0)||(window_end<=window_start))
        return 0U;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
        const uint8_t capacity=pattern->track_exec[track].logical_capacity;
        core->logical_capacity[track]=(capacity<=SEQ_LOGICAL_CAPACITY_MAX)
            ?capacity:SEQ_LOGICAL_CAPACITY_MAX;}
    note_event_t admitted=*event;int16_t binding=-1;uint8_t created=0U;
    if(!live_lane_bind(core,&admitted,&binding,&created)){
        seq_drop(core);return 0U;}
    if(admitted.kind==NOTE_EVENT_KIND_ON){
        if(admitted.track>=SEQ_LANE_CAPACITY||admitted.temporal_index>=SEQ_PLAY_MAX_CAPACITY){
            if(created&&binding>=0)g_seq_live_lane[(uint8_t)binding].active=0U;
            return 0U;}
        admitted.duration_samples=UINT32_MAX;
        admitted.flags|=NOTE_EVENT_FLAG_HELD;}
    g_seq_fx_core=core;g_seq_fx_block=out_block;
    g_seq_fx_start=window_start;g_seq_fx_end=window_end;
    const uint8_t accepted=(uint8_t)(walker_resume(&admitted,0U,0U)
        ==NOTE_EVENT_RESULT_ACCEPTED);
    if(accepted!=0U){const uint16_t bit=(uint16_t)(1U<<admitted.track);
        core->emitter_tracks|=bit;out_block->emitter_tracks|=bit;}
    if(admitted.kind==NOTE_EVENT_KIND_OFF&&binding>=0)
        g_seq_live_lane[(uint8_t)binding].active=0U;
    else if(!accepted&&created&&binding>=0)
        g_seq_live_lane[(uint8_t)binding].active=0U;
    return accepted;
}

static void sources_clear(seq_engine_core_t *core)
{core->source_count=0U;core->ledger_count=0U;
 memset(core->sources,0,sizeof(core->sources));
 memset(core->source_active,0,sizeof(core->source_active));
 core->ledger_active=0U;
 memset(g_seq_live_lane,0,sizeof(g_seq_live_lane));
 memset(core->ledger,0,sizeof(core->ledger));
 memset(core->ledger_track_count,0,sizeof(core->ledger_track_count));}

static const seq_play_item_t *step_item(const seq_pattern_t *p,
    uint8_t track, uint8_t step, uint8_t voice)
{
    if (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
        return &p->top_play[track][step].items[voice];
    if ((track < SEQ_LANE_CAPACITY) && (voice == 0U))
        return &p->child_play[track - BRICK_ENTITY_TOP_LEVEL_COUNT][step];
    return 0;
}

static int16_t play_value(const seq_pattern_t *p, uint8_t track,
    uint8_t step, uint8_t voice, seq_step_play_field_t field)
{
    const seq_play_item_t *item = step_item(p, track, step, voice);
    const seq_play_item_t *base = &p->play_base[track].items[voice];
    const uint8_t mask = (uint8_t)(1U << (uint8_t)field);
    const seq_play_item_t *source = ((item != 0)
        && ((item->present_mask & mask) != 0U)) ? item : base;
    switch (field) {
        case SEQ_STEP_PLAY_FIELD_NOTE: return source->note;
        case SEQ_STEP_PLAY_FIELD_VELOCITY: return source->velocity;
        case SEQ_STEP_PLAY_FIELD_LENGTH: return source->length;
        case SEQ_STEP_PLAY_FIELD_MICROTIMING: return source->microtiming;
        default: return 0;
    }
}

static uint16_t roll_divisor(uint8_t roll)
{
    static const uint16_t values[SEQ_STEP_ROLL_COUNT] =
        {0U,20U,24U,32U,40U,48U,64U,80U};
    return (roll < SEQ_STEP_ROLL_COUNT) ? values[roll] : 0U;
}

static uint64_t first_on(const seq_pattern_t *p, uint8_t track,
    uint8_t swing_phase, uint64_t nominal, uint64_t span_q16, int16_t mictim)
{
    uint64_t swing = 0U;
    if (((swing_phase & 1U) != 0U) && (p->track_swing[track] != 0U))
        swing = ((span_q16 * p->track_swing[track] + 100ULL) / 200ULL
            + 0x8000ULL) >> 16;
    int64_t micro = ((int64_t)mictim * p->samples_per_step_q16)
        / (96LL * 65536LL);
    const int64_t scaled = micro * (100 - p->track_quant[track]);
    micro = (scaled + ((scaled >= 0LL) ? 50LL : -50LL)) / 100LL;
    const uint64_t swung = nominal + swing;
    if (micro >= 0) return swung + (uint64_t)micro;
    return (swung > (uint64_t)(-micro)) ? swung - (uint64_t)(-micro) : 0U;
}

static ITCM_TEXT void source_add(seq_engine_core_t *core, uint64_t first_on,
    uint64_t span_q16, uint64_t interval_q16, uint32_t gate_samples,
    uint8_t track, uint8_t logical_slot, uint8_t note, uint8_t velocity,
    uint8_t playback_stage,uint32_t source_epoch)
{
    const uint16_t lane=product_lane_from_track_slot(track,logical_slot);
    if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES){seq_drop(core);return;}
    const uint32_t serial=++core->occurrence_serial;
    const uint8_t bank=(uint8_t)(source_epoch%SEQ_PRODUCT_MAX_SOURCE_GENERATIONS);
    seq_source_cursor_t *const source=&core->sources[bank][lane];
    if(((core->source_active[bank]>>lane)&UINT64_C(1))!=0U){
        seq_drop(core);return;}
    uint8_t ordinal_count=1U;
    if(interval_q16!=0U){const uint64_t count=(span_q16+interval_q16-1U)/interval_q16;
        ordinal_count=(uint8_t)((count>UINT8_MAX)?UINT8_MAX:count);}
    ++core->source_count;core->source_active[bank]|=UINT64_C(1)<<lane;
    *source=(seq_source_cursor_t){.first_on_sample=first_on,
        .interval_q16=interval_q16,.gate_samples=gate_samples,.serial=serial,
        .note=note,.velocity=velocity,.playback_stage=playback_stage,
        .ordinal_count=ordinal_count,.active=1U};
}

static uint8_t next_step(const seq_pattern_t *p, uint8_t track,
    uint8_t current)
{
    const uint8_t length = p->track_length[track] ? p->track_length[track] : 1U;
    return ((uint8_t)(current + 1U) < length) ? (uint8_t)(current + 1U) : 0U;
}

static uint8_t live_track_active(uint8_t track)
{
    for(uint8_t i=0U;i<SEQ_ENGINE_LEDGER_CAPACITY;++i)
        if(g_seq_live_lane[i].active&&g_seq_live_lane[i].track==track)return 1U;
    return 0U;
}

static void configure_fx_step(const seq_pattern_t *p,uint8_t track,
    uint8_t step)
{
    note_fx_slot_plan_word_t effective[NOTE_FX_SLOT_COUNT];
    memcpy(effective,p->fx_base_plan[track].slot,sizeof(effective));
    const uint16_t first=p->lock_first[track][step];
    const uint8_t count=p->steps[track][step].lock_count;
    for(uint8_t n=0U;n<count;++n){
        const seq_lock_pattern_t *const lock=&p->lock_pool[track][first+n];
        if((lock->param_flags&SEQ_ENGINE_PARAM_FLAG_NOTE_FX)==0U)continue;
        const uint8_t slot=(uint8_t)(lock->param_flags
            &SEQ_ENGINE_FX_PLAN_SLOT_MASK);
        if(slot<NOTE_FX_SLOT_COUNT)effective[slot]=(uint32_t)lock->value16
            |((uint32_t)lock->base_value16<<16U);
    }
    if(g_seq_fx_core!=0&&live_track_active(track)!=0U){
        for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
            if(g_seq_fx_core->fx_effective[track][slot]!=effective[slot])return;}
    for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
        if(g_seq_fx_core==0||g_seq_fx_core->fx_effective[track][slot]!=effective[slot]){
         if(note_fx_engine_configure(track,slot,note_fx_plan_model(effective[slot]),
            note_fx_plan_param(effective[slot],0U),note_fx_plan_param(effective[slot],1U),
            note_fx_plan_param(effective[slot],2U))!=NOTE_EVENT_RESULT_ACCEPTED)return;
         if(g_seq_fx_core!=0)g_seq_fx_core->fx_effective[track][slot]=effective[slot];}
}

static void schedule_step(seq_engine_core_t *core, const seq_pattern_t *p,
    uint8_t track, uint8_t step, uint8_t swing_phase, uint32_t serial,
    uint64_t nominal, uint8_t negative_only)
{
    if ((p->track_can_emit[track] == 0U) || (p->track_muted[track] != 0U)
            || (core->logical_capacity[track] == 0U)
            || ((p->steps[track][step].trig_roll & 1U) == 0U)) return;
    const uint8_t voices = (track < BRICK_ENTITY_TOP_LEVEL_COUNT)
        ? SEQ_PLAY_MAX_CAPACITY : 1U;
    const uint64_t span_q16 =
        (uint64_t)p->samples_per_step_q16 * p->track_div[track];
    const uint16_t divisor = roll_divisor(
        (uint8_t)(p->steps[track][step].trig_roll >> 1U));
    uint64_t interval_q16 = divisor ? (span_q16 * 16ULL) / divisor : span_q16;
    if (interval_q16 == 0U) interval_q16 = span_q16;
    for (uint8_t voice = 0U; voice < voices; ++voice) {
        if (core->voice_scheduled_serial[track][voice] == serial) continue;
        const int16_t note = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_NOTE);
        const int16_t vel = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_VELOCITY);
        int16_t length = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_LENGTH);
        const int16_t mictim = play_value(p,track,step,voice,SEQ_STEP_PLAY_FIELD_MICROTIMING);
        if ((note < 0) || (note >= 128) || (vel <= 0)) continue;
        if (length < 1) length = 1;
        if (length > 64) length = 64;
        const uint64_t first = first_on(p,track,swing_phase,nominal,span_q16,mictim);
        if ((negative_only != 0U) && (first >= nominal)) continue;
        core->voice_scheduled_serial[track][voice] = serial;
        const uint64_t gate = ((uint64_t)(uint16_t)length
            * p->samples_per_step_q16 + 0x8000ULL) >> 16;
        source_add(core,first,span_q16,interval_q16,
        (uint32_t)(gate ? gate : 1U),track,voice,(uint8_t)note,(uint8_t)vel,
            (uint8_t)((step_item(p,track,step,voice)!=0
                &&(step_item(p,track,step,voice)->present_mask&SEQ_STEP_PLAY_TERMINAL))
                ?NOTE_EVENT_STAGE_TERMINAL:NOTE_EVENT_STAGE_SOURCE),serial);
    }
}

static void schedule_boundary(seq_engine_core_t *core,
    const seq_pattern_t *p, uint64_t sample, uint16_t hit_mask,
    uint64_t block_start, seq_terminal_block_t *terminal)
{
    for (uint8_t track=0U; track<SEQ_LANE_CAPACITY; ++track) {
        if ((hit_mask & (uint16_t)(1U << track)) == 0U) continue;
        core->emitter_tracks |= (uint16_t)(1U << track);
        const uint8_t step=core->play_step[track];
        if(p->track_fx_enabled[track]!=0U)configure_fx_step(p,track,step);
        if ((p->track_lock_enabled[track] != 0U)
                && ((core->plock_fault_tracks & (uint16_t)(1U << track)) == 0U))
        {
            const uint16_t first=p->lock_first[track][step];
            const uint8_t count=p->steps[track][step].lock_count;
            const uint8_t old_count=core->active_lock_count[track];
            if ((uint32_t)terminal->event_count+old_count+count
                    > SEQ_ENGINE_TERMINAL_CAPACITY){
                core->plock_fault_tracks|=(uint16_t)(1U<<track);
                seq_drop(core);}
            else {
                uint8_t old_index=0U,new_index=0U,active_write=0U;
                while ((old_index<old_count)||(new_index<count)) {
                    while ((new_index<count)&&((p->lock_pool[track][first+new_index].param_flags
                            &SEQ_ENGINE_PARAM_FLAG_NOTE_FX)!=0U)) ++new_index;
                    const uint16_t old_key=(old_index<old_count)
                        ?(uint16_t)(core->active_locks[track][old_index].param_flags
                            &SEQ_ENGINE_PARAM_ID_MASK):UINT16_MAX;
                    const uint16_t new_key=(new_index<count)
                        ?(uint16_t)(p->lock_pool[track][first+new_index].param_flags
                            &SEQ_ENGINE_PARAM_ID_MASK):UINT16_MAX;
                    if ((old_key==UINT16_MAX)&&(new_key==UINT16_MAX)) break;
                    if (old_key<new_key) {
                        const seq_active_lock_t active=
                            core->active_locks[track][old_index++];
                        const uint8_t semantic=
                            ((active.param_flags&SEQ_ENGINE_PARAM_FLAG_CLEARABLE)!=0U)
                                ?SEQ_ENGINE_PARAM_CLEAR_TEMP:SEQ_ENGINE_PARAM_RESTORE_BASE;
                        const seq_terminal_event_t event={.param={
                            .value32=terminal_param_value32(old_key,active.base_value16),
                            .param_id=old_key,.track=track,.semantic=semantic}};
                        (void)terminal_push(terminal,(uint16_t)(sample-block_start),
                            SEQ_ENGINE_EVENT_PARAM,&event);
                        continue;
                    }
                    const seq_lock_pattern_t *lock=&p->lock_pool[track][first+new_index++];
                    if(old_key!=new_key
                            ||core->active_locks[track][old_index].value16!=lock->value16){
                        const uint16_t param_id=(uint16_t)(lock->param_flags
                            &SEQ_ENGINE_PARAM_ID_MASK);
                        const seq_terminal_event_t event={.param={
                            .value32=terminal_param_value32(param_id,lock->value16),
                            .param_id=param_id,.track=track,
                            .semantic=SEQ_ENGINE_PARAM_TEMP}};
                        (void)terminal_push(terminal,(uint16_t)(sample-block_start),
                            SEQ_ENGINE_EVENT_PARAM,&event);}
                    core->active_locks[track][active_write++]=(seq_active_lock_t){
                        .param_flags=lock->param_flags,.value16=lock->value16,
                        .base_value16=lock->base_value16};
                    if (old_key==new_key) ++old_index;
                }
                core->active_lock_count[track]=active_write;
            }
        }
        const uint32_t serial=core->step_serial[track];
        schedule_step(core,p,track,step,core->track_swing_phase[track],serial,
            sample,0U);
        const uint64_t next_sample=sample+(((uint64_t)p->samples_per_step_q16
            * p->track_div[track]+0x8000ULL)>>16);
        schedule_step(core,p,track,next_step(p,track,step),
            (uint8_t)(core->track_swing_phase[track]^1U),serial+1U,
            next_sample,1U);
    }
}

static uint16_t advance(seq_engine_core_t *core,const seq_pattern_t *p)
{
    uint16_t hits = 0U;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
        uint8_t div=p->track_div[track];
        if((div!=1U)&&(div!=2U)&&(div!=4U)&&(div!=8U))div=1U;
        if(core->track_div_phase[track]<(uint8_t)(div-1U)){
            ++core->track_div_phase[track];continue;}
        core->track_div_phase[track]=0U;
        core->track_swing_phase[track]^=1U;
        core->play_step[track]=next_step(p,track,core->play_step[track]);
        ++core->step_serial[track];
        hits |= (uint16_t)(1U << track);
    }
    return hits;
}

static uint64_t source_cursor_due(const seq_source_cursor_t *source)
{return source->first_on_sample
 +((((uint64_t)source->next_ordinal*source->interval_q16)+0x8000ULL)>>16U);}

static note_event_result_t process_source_cohort(seq_engine_core_t *core,
    const seq_pattern_t *p,uint8_t track,uint64_t due,uint64_t start)
{uint8_t source_count=0U;const uint64_t cohort_sample=due<start?start:due;
 const uint8_t quota=core->logical_capacity[track];
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);
  if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES)continue;
  for(uint8_t bank=0U;bank<SEQ_PRODUCT_MAX_SOURCE_GENERATIONS;++bank){
   if(((core->source_active[bank]>>lane)&1U)==0U)continue;
   seq_source_cursor_t*source=&core->sources[bank][lane];
   const uint64_t source_due=source_cursor_due(source);
   if((source_due<start?start:source_due)!=cohort_sample
        ||source_count>=NOTE_FX_BATCH_CAPACITY)continue;
   g_seq_source_due_ref[source_count++]=(seq_source_due_ref_t){
      .bank=bank,.lane=lane,.order=source->serial};}}
 for(uint8_t i=1U;i<source_count;++i){const seq_source_due_ref_t x=g_seq_source_due_ref[i];
  uint8_t j=i;while(j&&g_seq_source_due_ref[j-1U].order>x.order){
   g_seq_source_due_ref[j]=g_seq_source_due_ref[j-1U];--j;}g_seq_source_due_ref[j]=x;}
 uint8_t event_count=0U;
 for(uint8_t i=0U;i<source_count;++i){const seq_source_due_ref_t ref=g_seq_source_due_ref[i];
  seq_source_cursor_t*s=&core->sources[ref.bank][ref.lane];const uint32_t occurrence=++core->occurrence_serial;
  g_seq_source_cohort[event_count++]=(note_event_t){.sample_abs=cohort_sample,
   .duration_samples=s->gate_samples,.source_id=occurrence,.occurrence_id=occurrence,
   .source_generation=p->generation?p->generation:1U,.group_id=occurrence,
   .track=track,.note=s->note,.velocity=s->velocity,.kind=NOTE_EVENT_KIND_ON,
   .provenance=NOTE_EVENT_SOURCE_STEP,.stage=s->playback_stage,
   .temporal_index=product_slot_from_lane(ref.lane),
   .flags=(uint8_t)((s->playback_stage==NOTE_EVENT_STAGE_TERMINAL)
      ?NOTE_EVENT_FLAG_TERMINAL:0U)};
  if(++s->next_ordinal>=s->ordinal_count){s->active=0U;
   core->source_active[ref.bank]&=~(UINT64_C(1)<<ref.lane);
   if(core->source_count)--core->source_count;}}
 uint8_t harm_slot=NOTE_FX_SLOT_COUNT;
 for(uint8_t slot=0U;slot<NOTE_FX_SLOT_COUNT;++slot)
  if(note_fx_plan_model(core->fx_effective[track][slot])==NOTE_FX_MODEL_HARMONIZER)
   {harm_slot=slot;break;}
 if(harm_slot==NOTE_FX_SLOT_COUNT){for(uint8_t i=0U;i<event_count;++i){
   const note_event_result_t result=(g_seq_source_cohort[i].stage==NOTE_EVENT_STAGE_TERMINAL)
    ?(fx_terminal(&g_seq_source_cohort[i]),NOTE_EVENT_RESULT_ACCEPTED)
    :walker_resume(&g_seq_source_cohort[i],g_seq_source_cohort[i].stage,1U);
   if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}return NOTE_EVENT_RESULT_ACCEPTED;}
 uint8_t cohort_first=0U;
 for(uint8_t i=0U;i<event_count;++i){if(g_seq_source_cohort[i].stage>harm_slot){
   const note_event_result_t result=walker_resume(&g_seq_source_cohort[i],
      g_seq_source_cohort[i].stage,1U);if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}
  else g_seq_source_cohort[cohort_first++]=g_seq_source_cohort[i];}
 return cohort_first?walker_harm_cohort(g_seq_source_cohort,cohort_first,harm_slot,1U)
   :NOTE_EVENT_RESULT_ACCEPTED;}

typedef struct {uint64_t due;uint32_t order;uint16_t lane;uint8_t slot,rank,valid;}
    seq_collect_head_t;

static uint8_t collect_head_before(uint64_t due,uint32_t order,uint8_t rank,
    const seq_collect_head_t *head)
{return(uint8_t)(head->valid==0U||due<head->due
   ||(due==head->due&&(order<head->order
      ||(order==head->order&&rank<head->rank))));}

static void collect_source_head(const seq_engine_core_t *core,uint8_t track,
    uint64_t end,seq_collect_head_t *head)
{*head=(seq_collect_head_t){0};const uint8_t quota=core->logical_capacity[track];
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES)continue;
  for(uint8_t bank=0U;bank<SEQ_PRODUCT_MAX_SOURCE_GENERATIONS;++bank){
   if(((core->source_active[bank]>>lane)&1U)==0U)continue;
   const seq_source_cursor_t *source=&core->sources[bank][lane];
   const uint64_t due=source_cursor_due(source);const uint8_t rank=(uint8_t)(logical*6U+bank);
   if(due<end&&collect_head_before(due,source->serial,rank,head))
    *head=(seq_collect_head_t){.due=due,.order=source->serial,.lane=lane,
      .slot=bank,.rank=rank,.valid=1U};}}}

static void collect_ledger_head(const seq_engine_core_t *core,uint8_t track,
    uint64_t end,seq_collect_head_t *head)
{*head=(seq_collect_head_t){0};const uint8_t quota=core->logical_capacity[track];
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES
      ||((core->ledger_active>>lane)&1U)==0U)continue;
  const seq_ledger_entry_t *ledger=&core->ledger[lane];const uint8_t rank=(uint8_t)(logical*6U+3U);
  if(ledger->due_off<end&&collect_head_before(ledger->due_off,ledger->occurrence_id,rank,head))
   *head=(seq_collect_head_t){.due=ledger->due_off,.order=ledger->occurrence_id,
     .lane=lane,.rank=rank,.valid=1U};}}

static void collect_deferred_head(uint8_t track,uint64_t end,seq_collect_head_t *head)
{*head=(seq_collect_head_t){0};const uint8_t quota=g_seq_fx_core->logical_capacity[track];
 for(uint8_t logical=0U;logical<quota;++logical){const uint16_t lane=
   product_lane_from_track_slot(track,logical);if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES)continue;
  for(uint8_t slot=0U;slot<SEQ_PRODUCT_DEFERRED_BATCHES_PER_LANE;++slot){
   if(((g_deferred_active[slot]>>lane)&1U)==0U)continue;
   const seq_terminal_deferred_t *deferred=&g_terminal_deferred[lane][slot];
   const uint32_t order=deferred->count?deferred->event[0].occurrence_id:UINT32_MAX;
   const uint8_t rank=(uint8_t)(logical*6U+4U+slot);
   if(deferred->due_sample<end&&collect_head_before(deferred->due_sample,order,rank,head))
    *head=(seq_collect_head_t){.due=deferred->due_sample,.order=order,.lane=lane,
      .slot=slot,.rank=rank,.valid=1U};}}}

static uint8_t collect_select_head(const seq_collect_head_t head[3])
{uint8_t selected=UINT8_MAX;
 for(uint8_t i=0U;i<3U;++i){if(head[i].valid!=0U
   &&(selected==UINT8_MAX||collect_head_before(head[i].due,head[i].order,
      head[i].rank,&head[selected])))selected=i;}
 return selected;}

static ITCM_TEXT void collect(seq_engine_core_t *core,uint64_t start,uint16_t frames,
    const seq_pattern_t *p,seq_terminal_block_t *out)
{
    const uint64_t end=start+frames;
    g_seq_fx_core=core;g_seq_fx_block=out;g_seq_fx_start=start;g_seq_fx_end=end;
    const uint64_t step_samples=p->samples_per_step_q16?p->samples_per_step_q16:1U;
    const uint64_t delta_q16=((start<<16U)>=core->step_sample_q16)
        ?(((start<<16U)-core->step_sample_q16)<<16U)/step_samples:0U;
    const uint64_t transport=((uint64_t)core->transport_step_serial<<16U)+delta_q16;
    uint32_t pattern[NOTE_FX_TRACK_COUNT];
    for(uint8_t t=0U;t<NOTE_FX_TRACK_COUNT;++t){uint8_t div=p->track_div[t];
        if((div!=1U)&&(div!=2U)&&(div!=4U)&&(div!=8U))div=1U;
        const uint64_t track_phase_q16=((uint64_t)core->track_div_phase[t]<<16U)+delta_q16;
        pattern[t]=((uint32_t)core->play_step[t]<<16U)+(uint32_t)(track_phase_q16/div);}
    if(note_fx_engine_process(start,0U,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        seq_drop(core);
    enum {DUE_SOURCE=0,DUE_LEDGER,DUE_DEFERRED};
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
      seq_collect_head_t head[3];collect_source_head(core,track,end,&head[DUE_SOURCE]);
      collect_ledger_head(core,track,end,&head[DUE_LEDGER]);
      collect_deferred_head(track,end,&head[DUE_DEFERRED]);
      for(;;){
        const uint8_t kind=collect_select_head(head);
        if(kind==UINT8_MAX)break;
        const uint16_t selected_lane=head[kind].lane;
        const uint8_t slot=head[kind].slot;const uint64_t selected_due=head[kind].due;
        if(kind==DUE_SOURCE){
          if(process_source_cohort(core,p,track,selected_due,start)
                !=NOTE_EVENT_RESULT_ACCEPTED)
            seq_drop(core);
          collect_source_head(core,track,end,&head[DUE_SOURCE]);
          collect_ledger_head(core,track,end,&head[DUE_LEDGER]);
          collect_deferred_head(track,end,&head[DUE_DEFERRED]);
          continue;}
        if(kind==DUE_LEDGER){seq_ledger_entry_t l=core->ledger[selected_lane];
          const seq_terminal_event_t terminal={.note={.occurrence_id=l.occurrence_id,
           .track=track,.note=l.note,
           .logical_slot=product_slot_from_lane(selected_lane)}};
          if(!terminal_push(out,(uint16_t)((selected_due<start)?0U:selected_due-start),
                 SEQ_ENGINE_EVENT_NOTE_OFF,&terminal)){seq_drop(core);
            core->ledger[selected_lane].due_off=end;break;}
          ledger_release(core,(uint8_t)selected_lane);
          collect_ledger_head(core,track,end,&head[DUE_LEDGER]);
          continue;}
        seq_terminal_deferred_t x=g_terminal_deferred[selected_lane][slot];
        g_deferred_active[slot]&=~(UINT64_C(1)<<selected_lane);
        for(uint8_t n=0;n<x.count;++n){const seq_deferred_event_t*d=&x.event[n];
          note_event_t event={.sample_abs=x.due_sample,.duration_samples=d->duration_samples,
           .source_id=d->source_id,.occurrence_id=d->occurrence_id,
           .source_generation=p->generation?p->generation:1U,.group_id=d->occurrence_id,
           .track=track,
           .note=d->note,.velocity=d->velocity,.kind=NOTE_EVENT_KIND_ON,
           .provenance=d->provenance,.stage=NOTE_EVENT_STAGE_TERMINAL,
           .flags=d->flags,.temporal_index=product_slot_from_lane(selected_lane)};
          fx_terminal(&event);}
        collect_deferred_head(track,end,&head[DUE_DEFERRED]);
        collect_ledger_head(core,track,end,&head[DUE_LEDGER]);
        }}
    if(note_fx_engine_process(start,frames,p->samples_per_step_q16,
            transport,pattern,p->scale_index,p->root_index,
            fx_generated,0)!=NOTE_EVENT_RESULT_ACCEPTED)
        seq_drop(core);
}

void seq_engine_core_init(seq_engine_core_t *core)
{
    if(core==0)return;
    memset(core,0,sizeof(*core));
    memset(g_terminal_deferred,0,sizeof(g_terminal_deferred));
    memset(g_deferred_active,0,sizeof(g_deferred_active));
    sources_clear(core);
    note_fx_engine_init();
    for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)for(uint8_t v=0U;
        v<SEQ_PLAY_MAX_CAPACITY;++v)core->voice_scheduled_serial[t][v]=UINT32_MAX;
}

void seq_engine_core_process_block(seq_engine_core_t *core,uint64_t start,uint16_t frames,
    const seq_pattern_t *p,seq_terminal_block_t *out)
{
    if((core==0)||(out==0))return;
    g_seq_fx_core=core;
    terminal_reset(out,start,frames,p?p->generation:0U);
    out->emitter_tracks=0U;
    out->lock_tracks=0U;
    if((p==0)||(frames==0U))return;
    core->event_faulted=0U;core->plock_fault_tracks=0U;
    const uint8_t seed_frontier=(uint8_t)((core->initialized==0U)
        ||(core->transport_epoch!=p->transport_epoch));
    if(seed_frontier!=0U){
        for(uint8_t lane=0U;lane<SEQ_ENGINE_LEDGER_CAPACITY;++lane){
            if(((core->ledger_active>>lane)&UINT64_C(1))==0U)continue;
            const uint8_t track=product_track_from_lane(lane);
            const seq_terminal_event_t terminal={.note={
                .occurrence_id=core->ledger[lane].occurrence_id,
                .track=track,.note=core->ledger[lane].note,
                .logical_slot=product_slot_from_lane(lane)}};
            if(terminal_push(out,0U,SEQ_ENGINE_EVENT_NOTE_OFF,&terminal)!=0U)
                out->emitter_tracks|=(uint16_t)(1U<<track);
        }
        seq_engine_core_init(core);core->initialized=1U;core->transport_epoch=p->transport_epoch;
        core->running=p->running;core->step_sample_q16=p->seed_step_sample_q16;
        core->samples_per_step_q16=p->samples_per_step_q16;
        memcpy(core->play_step,p->seed_play_step,sizeof(core->play_step));
        memcpy(core->track_div_phase,p->seed_div_phase,sizeof(core->track_div_phase));
        memcpy(core->track_swing_phase,p->seed_swing_phase,sizeof(core->track_swing_phase));
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)
            configure_fx_step(p,t,core->play_step[t]);
        }
    core->samples_per_step_q16=p->samples_per_step_q16;
    for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t){uint8_t capacity=p->track_exec[t].logical_capacity;
        core->logical_capacity[t]=(capacity<=8U)?capacity:8U;}
    ledger_retire_outside_capacity(core,out);
    if(core->pattern_generation!=p->generation){
        core->pattern_generation=p->generation;
        for(uint8_t t=0U;t<SEQ_LANE_CAPACITY;++t)
            configure_fx_step(p,t,core->play_step[t]);}
    if((p->running==0U)||(core->samples_per_step_q16==0U)){
        core->running=0U;
        collect(core,start,frames,p,out);
        for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
            const uint16_t bit=(uint16_t)(1U<<track);
            if((p->track_note_enabled[track]!=0U)
                    &&(p->track_muted[track]==0U)
                    &&((core->emitter_tracks&bit)!=0U))
                out->emitter_tracks|=bit;}
        return;}core->running=1U;
    const uint32_t dropped_before=core->dropped_events;
    const uint64_t begin_q16=start<<16,end_q16=(start+frames)<<16;
    if(seed_frontier!=0U){
        schedule_boundary(core,p,start,UINT16_MAX,start,out);}
    uint64_t next=core->step_sample_q16+core->samples_per_step_q16;
    while(next<end_q16){const uint16_t hits=advance(core,p);++core->transport_step_serial;
        core->step_sample_q16=next;
        if(next>=begin_q16)schedule_boundary(core,p,(next+0x8000ULL)>>16,hits,
            start,out);
        next=core->step_sample_q16+core->samples_per_step_q16;}
    collect(core,start,frames,p,out);
    if(core->dropped_events!=dropped_before)core->event_faulted=1U;
    for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track)
    {
        const uint16_t bit=(uint16_t)(1U<<track);
        if((p->track_lock_enabled[track]!=0U)
                &&((core->plock_fault_tracks&bit)==0U)
                &&((core->emitter_tracks&bit)!=0U))
            out->lock_tracks|=bit;
        if((p->track_note_enabled[track]!=0U)
                &&(p->track_muted[track]==0U)
                &&((core->emitter_tracks&bit)!=0U))
            out->emitter_tracks|=bit;
    }
}
