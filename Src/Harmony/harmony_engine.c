#include "Harmony/harmony_engine.h"

#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Platform/memory_layout.h"
#include "Seq/seq_engine.h"

static CTRL_STATE uint8_t g_track_mode[BRICK_ENTITY_CAPACITY];
static CTRL_STATE uint8_t g_live_anchor[BRICK_ENTITY_CAPACITY];
static CTRL_STATE uint16_t g_live_anchor_valid;

uint16_t harmony_engine_chord_mask(uint8_t chord)
{return master_harmony_chord_mask(chord);}

void harmony_engine_init(void)
{for(uint8_t t=0U;t<BRICK_ENTITY_CAPACITY;++t)g_track_mode[t]=HARMONY_MODE_CHORD1;
 g_track_mode[BRICK_ENTITY_MASTER_ID]=HARMONY_MODE_OFF;g_live_anchor_valid=0U;}

void harmony_engine_set_mode(uint8_t track,harmony_mode_t mode)
{if(track<BRICK_ENTITY_CAPACITY&&mode<HARMONY_MODE_COUNT
      &&g_track_mode[track]!=(uint8_t)mode){g_track_mode[track]=(uint8_t)mode;
  seq_engine_control_mark_dirty();}}

harmony_mode_t harmony_engine_get_mode(uint8_t track)
{return(track<BRICK_ENTITY_CAPACITY)?(harmony_mode_t)g_track_mode[track]:HARMONY_MODE_OFF;}

harmony_mode_t harmony_engine_resolve_mode(uint8_t track)
{entity_topology_descriptor_t e;track_runtime_descriptor_t runtime;
 if(entity_topology_get(track,&e)==0U)return HARMONY_MODE_OFF;
 if(track_runtime_get_descriptor(track,&runtime)!=0U
      &&runtime.family==TRACK_RUNTIME_FAMILY_DRUM)return HARMONY_MODE_OFF;
 if(e.role==ENTITY_ROLE_GROUP_CHILD)return harmony_engine_get_mode(e.parent_entity_id);
 return harmony_engine_get_mode(track);}

void harmony_engine_live_reset(void){g_live_anchor_valid=0U;}

static uint8_t fold_midi(int16_t note)
{while(note<0)note+=12;while(note>127)note-=12;return(uint8_t)note;}

static uint8_t nearest_pc(uint8_t note,uint16_t target_mask,uint8_t target_root,
    uint8_t prefer_up)
{int8_t best_delta=0;uint8_t best_distance=UINT8_MAX;
 for(int8_t delta=-6;delta<=6;++delta){const uint8_t pc=(uint8_t)((note+12+delta)%12);
  const uint8_t relative=(uint8_t)((pc+12U-target_root)%12U);
  if((target_mask&(uint16_t)(1U<<relative))==0U)continue;
  const uint8_t distance=(uint8_t)((delta<0)?-delta:delta);
  if(distance<best_distance||(distance==best_distance&&prefer_up&&delta>best_delta))
   {best_distance=distance;best_delta=delta;}}
 return fold_midi((int16_t)note+best_delta);}

static uint8_t chord_rank_map(uint8_t note,const harmony_zone_t *zone,
    const master_harmony_state_t *target,uint8_t mode)
{const uint16_t source_mask=harmony_engine_chord_mask(zone->source_chord);
 const uint16_t target_mask=harmony_engine_chord_mask(target->type);
 const uint8_t source_pc=(uint8_t)((note+12U-zone->source_root)%12U);
 uint8_t source_rank=0U,source_count=0U,target_pc[12],target_count=0U;
 for(uint8_t pc=0U;pc<12U;++pc){if((source_mask&(1U<<pc))!=0U){
   if(pc<=source_pc)source_rank=source_count;
   ++source_count;}
  if((target_mask&(1U<<pc))!=0U)target_pc[target_count++]=pc;}
 if(source_count==0U||target_count==0U)return note;
 if((source_mask&(1U<<source_pc))==0U)
  return nearest_pc(fold_midi((int16_t)note+(int16_t)target->root-zone->source_root),
      target_mask,target->root,(uint8_t)(mode==HARMONY_MODE_CHORD1));
 const uint8_t mapped=target_pc[(uint8_t)((uint16_t)source_rank*target_count/source_count)];
 const uint8_t source_abs=(uint8_t)((zone->source_root+source_pc)%12U);
 const uint8_t target_abs=(uint8_t)((target->root+mapped)%12U);
 int8_t delta=(int8_t)target_abs-(int8_t)source_abs;
 if(delta>6)delta-=12;
 if(delta<-6)delta+=12;
 return fold_midi((int16_t)note+delta);}

static uint8_t diatonic_map(uint8_t note,const harmony_zone_t *zone,
    const master_harmony_state_t *target)
{static const uint8_t major[7]={0U,2U,4U,5U,7U,9U,11U};
 static const uint8_t minor[7]={0U,2U,3U,5U,7U,8U,10U};
 const uint8_t *scale=zone->scale_minor?minor:major;
 const uint8_t relative=(uint8_t)((note+12U-zone->scale_root)%12U);
 uint8_t degree=0U,best=12U;
 for(uint8_t i=0U;i<7U;++i){uint8_t d=(relative>scale[i])?(relative-scale[i]):(scale[i]-relative);
  if(d<best){best=d;degree=i;}}
 const uint8_t target_pc=(uint8_t)((target->root+scale[degree])%12U);
 int8_t delta=(int8_t)target_pc-(int8_t)(note%12U);
 if(delta>6)delta-=12;
 if(delta<-6)delta+=12;
 return fold_midi((int16_t)note+delta);}

static const harmony_zone_t *zone_at(const harmony_track_plan_t *plan,uint8_t step)
{if(plan==0||plan->valid==0U||plan->zone_count==0U)return 0;
 for(uint8_t i=0U;i<plan->zone_count;++i)
  if(step>=plan->zone[i].start_step&&step<=plan->zone[i].end_step)return &plan->zone[i];
 return &plan->zone[plan->zone_count-1U];}

uint8_t harmony_engine_transform(const harmony_track_plan_t *plan,
    harmony_mode_t requested,uint8_t track,uint8_t step,uint8_t input,
    const master_harmony_state_t *target,uint8_t *out)
{if(out==0||target==0||target->valid==0U||requested==HARMONY_MODE_OFF)return 0U;
 const harmony_zone_t *zone=zone_at(plan,step);harmony_mode_t mode=requested;
 uint8_t anchor=(uint8_t)(input%12U);
 if(zone!=0){anchor=zone->anchor_pc;
  if(zone->source_kind==HARMONY_SOURCE_CHORD){if(mode==HARMONY_MODE_DIATONIC)mode=HARMONY_MODE_DIATONIC;}
  else if(zone->source_kind==HARMONY_SOURCE_SCALE)mode=HARMONY_MODE_DIATONIC;
  else mode=HARMONY_MODE_PARALLEL;}
 else {if(track>=BRICK_ENTITY_CAPACITY)return 0U;const uint16_t bit=(uint16_t)(1U<<track);
  if((g_live_anchor_valid&bit)==0U){g_live_anchor[track]=input;g_live_anchor_valid|=bit;}
  anchor=(uint8_t)(g_live_anchor[track]%12U);mode=HARMONY_MODE_PARALLEL;}
 if(mode==HARMONY_MODE_PARALLEL){*out=fold_midi((int16_t)input+target->root-anchor);return 1U;}
 if(mode==HARMONY_MODE_DIATONIC&&zone!=0){*out=diatonic_map(input,zone,target);return 1U;}
 if(mode==HARMONY_MODE_BASS){const uint8_t bass=(target->bass<12U)?target->bass:target->root;
  *out=fold_midi((int16_t)input+bass-anchor);return 1U;}
 if(zone!=0&&zone->source_kind==HARMONY_SOURCE_CHORD){
  *out=chord_rank_map(input,zone,target,(uint8_t)mode);return 1U;}
 *out=fold_midi((int16_t)input+target->root-anchor);return 1U;}
