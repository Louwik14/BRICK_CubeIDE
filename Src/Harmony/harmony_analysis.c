#include "Harmony/harmony_analysis.h"

#include <string.h>
#include "Seq/seq_model.h"
#include "Seq/seq_engine.h"
#include "Track/entity_topology.h"
#include "Track/track_runtime.h"
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

#define ANALYSIS_DEBOUNCE_MS 100U
typedef struct {uint32_t revision,analyzed_revision,dirty_ms;
 harmony_track_plan_t plan;} analysis_track_t;
static CONTROL_STATE_SRAM2 analysis_track_t g_analysis[BRICK_ENTITY_CAPACITY];

static uint8_t popcount12(uint16_t x)
{uint8_t n=0U;for(;x!=0U;x&=(uint16_t)(x-1U))++n;return n;}

static uint16_t rotated(uint16_t mask,uint8_t root)
{root%=12U;return(uint16_t)(((mask<<root)|(mask>>(12U-root)))&0x0FFFU);}

static uint16_t step_weight(uint8_t step,int16_t length)
{uint16_t strong=(step%16U==0U)?4U:((step%4U==0U)?2U:1U);
 if(length<1)length=1;
 if(length>16)length=16;
 return(uint16_t)(strong*length);}

static uint8_t is_drum(uint8_t track)
{track_runtime_descriptor_t d;return(uint8_t)(track_runtime_get_descriptor(track,&d)!=0U
 &&d.family==TRACK_RUNTIME_FAMILY_DRUM);}

static uint8_t analysis_track_eligible(uint8_t track)
{entity_topology_descriptor_t e;
 return(uint8_t)(track<BRICK_ENTITY_CAPACITY&&track!=BRICK_ENTITY_MASTER_ID
   &&is_drum(track)==0U&&entity_topology_get(track,&e)!=0U&&e.active!=0U
   &&e.role!=ENTITY_ROLE_GROUP_MASTER);}

static const harmony_zone_t *plan_zone_at(const harmony_track_plan_t *plan,
    uint8_t step)
{if(plan==0||plan->valid==0U||plan->zone_count==0U)return 0;
 for(uint8_t i=0U;i<plan->zone_count;++i)
  if(step>=plan->zone[i].start_step&&step<=plan->zone[i].end_step)
   return &plan->zone[i];
 return &plan->zone[plan->zone_count-1U];}

static uint8_t context_root_bonus(uint8_t track,uint8_t root,uint8_t step)
{uint8_t bonus=0U;for(uint8_t other=0U;other<BRICK_ENTITY_CAPACITY;++other){
 if(other==track||analysis_track_eligible(other)==0U)continue;
 const harmony_zone_t *const zone=plan_zone_at(&g_analysis[other].plan,step);
 if(zone==0||(zone->source_kind!=HARMONY_SOURCE_CHORD
      &&zone->source_kind!=HARMONY_SOURCE_SCALE))continue;
 const uint8_t other_root=(zone->source_kind==HARMONY_SOURCE_CHORD)
     ?zone->source_root:zone->scale_root;
 if(other_root==root&&bonus<4U)++bonus;}
 return bonus;}

static int32_t chord_score(const uint16_t weight[12],uint16_t mask,
    uint8_t root,uint8_t type,const harmony_zone_t *previous)
{const uint16_t cm=rotated(harmony_engine_chord_mask(type),root);int32_t score=0;
 for(uint8_t pc=0U;pc<12U;++pc)score+=((cm&(1U<<pc))!=0U)
   ?(int32_t)weight[pc]*3:-(int32_t)weight[pc]*4;
 score+=(int32_t)popcount12((uint16_t)(mask&cm))*6;
 score-=(int32_t)popcount12((uint16_t)(cm&~mask))*4;
 if(previous!=0&&previous->source_kind==HARMONY_SOURCE_CHORD
    &&previous->source_root==root&&previous->source_chord==type)score+=3;
 return score;}

static void collect_histogram(uint8_t track,uint8_t first,uint8_t last,
    uint16_t weight[12],uint16_t *out_mask,uint16_t *out_total,
    uint8_t *out_anchor)
{memset(weight,0,12U*sizeof(weight[0]));*out_mask=0U;*out_total=0U;
 uint8_t first_seen[12];memset(first_seen,0xFF,sizeof(first_seen));
 const uint8_t voices=seq_model_play_capacity(track);
 for(uint8_t step=first;step<=last&&step<SEQ_MAX_STEPS;++step){
  if(seq_model_get_trig(track,step)==0U)continue;
  for(uint8_t voice=0U;voice<voices;++voice){int16_t note,vel,length;
   if(seq_model_play_get(track,step,voice,SEQ_STEP_PLAY_FIELD_NOTE,&note)==0U)
    (void)seq_model_play_base_get(track,voice,SEQ_STEP_PLAY_FIELD_NOTE,&note);
   if(seq_model_play_get(track,step,voice,SEQ_STEP_PLAY_FIELD_VELOCITY,&vel)==0U)
    (void)seq_model_play_base_get(track,voice,SEQ_STEP_PLAY_FIELD_VELOCITY,&vel);
   if(seq_model_play_get(track,step,voice,SEQ_STEP_PLAY_FIELD_LENGTH,&length)==0U)
    (void)seq_model_play_base_get(track,voice,SEQ_STEP_PLAY_FIELD_LENGTH,&length);
   if(note<0||note>=128||vel<=0)continue;
   const uint8_t pc=(uint8_t)(note%12);
   const uint16_t w=step_weight(step,length);weight[pc]=(uint16_t)(weight[pc]+w);
   *out_total=(uint16_t)(*out_total+w);*out_mask|=(uint16_t)(1U<<pc);
   if(first_seen[pc]==0xFFU)first_seen[pc]=step;}}
 uint16_t best=0U;uint8_t anchor=0U;
 for(uint8_t pc=0U;pc<12U;++pc)if(weight[pc]>best
   ||(weight[pc]==best&&first_seen[pc]<first_seen[anchor])){best=weight[pc];anchor=pc;}
 *out_anchor=anchor;}

static void detect_zone(uint8_t track,uint8_t first,uint8_t last,
    const harmony_zone_t *previous,harmony_zone_t *out)
{uint16_t weight[12],mask,total;uint8_t anchor;
 collect_histogram(track,first,last,weight,&mask,&total,&anchor);
 *out=(harmony_zone_t){.start_step=first,.end_step=last,.anchor_pc=anchor,
  .source_kind=HARMONY_SOURCE_NONE};if(total==0U)return;
 out->source_kind=HARMONY_SOURCE_ANCHOR;
 int32_t best=-32768,second=-32768;uint8_t best_root=0U,best_type=0U;
 for(uint8_t root=0U;root<12U;++root)for(uint8_t type=0U;
      type<MASTER_CHORD_TYPE_COUNT;++type){const int32_t score=
      chord_score(weight,mask,root,type,previous);
   if(score>best){second=best;best=score;best_root=root;best_type=type;}
   else if(score>second)second=score;}
 if(best>=(int32_t)total*2&&(best-second)<3){
  int32_t resolved=-32768,resolved_second=-32768;
  uint8_t resolved_root=best_root,resolved_type=best_type;
  const uint8_t midpoint=(uint8_t)(first+(last-first)/2U);
  for(uint8_t root=0U;root<12U;++root)for(uint8_t type=0U;
       type<MASTER_CHORD_TYPE_COUNT;++type){const int32_t score=
       chord_score(weight,mask,root,type,previous)
       +context_root_bonus(track,root,midpoint);
   if(score>resolved){resolved_second=resolved;resolved=score;
      resolved_root=root;resolved_type=type;}
   else if(score>resolved_second)resolved_second=score;}
  if((resolved-resolved_second)>=3){best=resolved;second=resolved_second;
    best_root=resolved_root;best_type=resolved_type;}}
 if(best>=(int32_t)total*2&&(best-second)>=3){out->source_root=best_root;
  out->source_chord=best_type;out->source_kind=HARMONY_SOURCE_CHORD;
  out->confidence=(uint8_t)((best>255)?255:best);}
 static const uint16_t scale_mask[2]={0xAB5U,0x5ADU};
 int32_t scale_best=-32768,scale_second=-32768;uint8_t scale_root=0U,minor=0U;
 for(uint8_t root=0U;root<12U;++root)for(uint8_t m=0U;m<2U;++m){
  const uint16_t sm=rotated(scale_mask[m],root);int32_t score=0;
  for(uint8_t pc=0U;pc<12U;++pc)score+=((sm&(1U<<pc))!=0U)
      ?(int32_t)weight[pc]:-(int32_t)weight[pc]*2;
  if(score>scale_best){scale_second=scale_best;scale_best=score;scale_root=root;minor=m;}
  else if(score>scale_second)scale_second=score;}
 out->scale_root=scale_root;out->scale_minor=minor;
 if(out->source_kind!=HARMONY_SOURCE_CHORD&&scale_best>=(int32_t)total/2
      &&scale_best-scale_second>=2){out->source_kind=HARMONY_SOURCE_SCALE;
  out->confidence=(uint8_t)((scale_best>255)?255:scale_best);}}

static void analyze(uint8_t track,uint32_t revision,harmony_track_plan_t *out)
{memset(out,0,sizeof(*out));out->revision=revision;
 if(analysis_track_eligible(track)==0U)return;
 const uint8_t length=seq_model_get_track_playback_length(track);
 if(length==0U)return;
 harmony_zone_t candidate[4];uint8_t count=0U;
 const uint8_t chunk=(uint8_t)((length+3U)/4U);
 for(uint8_t first=0U;first<length&&count<4U;first=(uint8_t)(first+chunk)){
  uint8_t last=(uint8_t)(first+chunk-1U);if(last>=length)last=(uint8_t)(length-1U);
  detect_zone(track,first,last,(count!=0U)?&candidate[count-1U]:0,&candidate[count]);++count;}
 uint8_t write=0U;for(uint8_t i=0U;i<count;++i){
  if(candidate[i].source_kind==HARMONY_SOURCE_NONE)continue;
  if(write!=0U&&candidate[i].source_kind==candidate[write-1U].source_kind
    &&candidate[i].source_root==candidate[write-1U].source_root
    &&candidate[i].source_chord==candidate[write-1U].source_chord
    &&candidate[i].scale_root==candidate[write-1U].scale_root
    &&candidate[i].scale_minor==candidate[write-1U].scale_minor)
   {candidate[write-1U].end_step=candidate[i].end_step;continue;}
  if(candidate[i].source_kind==HARMONY_SOURCE_ANCHOR&&write!=0U){
   candidate[write-1U].end_step=candidate[i].end_step;continue;}
  out->zone[write++]=candidate[i];}
 out->zone_count=write;out->valid=(write!=0U)?1U:0U;}

static uint8_t plan_has_inference(const harmony_track_plan_t *plan)
{if(plan==0||plan->valid==0U)return 0U;
 for(uint8_t i=0U;i<plan->zone_count;++i)
  if(plan->zone[i].source_kind==HARMONY_SOURCE_CHORD
       ||plan->zone[i].source_kind==HARMONY_SOURCE_SCALE)return 1U;
 return 0U;}

void harmony_analysis_init(void)
{memset(g_analysis,0,sizeof(g_analysis));for(uint8_t t=0U;t<BRICK_ENTITY_CAPACITY;++t)
 harmony_analysis_mark_dirty(t);}

void harmony_analysis_mark_dirty(uint8_t track)
{if(track>=BRICK_ENTITY_CAPACITY)return;++g_analysis[track].revision;
 g_analysis[track].dirty_ms=HAL_GetTick();}

void harmony_analysis_service(void)
{const uint32_t now=HAL_GetTick();for(uint8_t track=0U;track<BRICK_ENTITY_CAPACITY;++track){
 analysis_track_t *const s=&g_analysis[track];if(s->revision==s->analyzed_revision
   ||(uint32_t)(now-s->dirty_ms)<ANALYSIS_DEBOUNCE_MS)continue;
 const uint32_t revision=s->revision;harmony_track_plan_t next;
 analyze(track,revision,&next);if(s->revision!=revision)return;
 if(next.valid!=0U&&plan_has_inference(&next)==0U
       &&plan_has_inference(&s->plan)!=0U){
  next=s->plan;next.revision=revision;
 }
 s->plan=next;
 s->analyzed_revision=revision;seq_engine_control_mark_dirty();return;}}

uint8_t harmony_analysis_get_plan(uint8_t track,harmony_track_plan_t *out)
{if(track>=BRICK_ENTITY_CAPACITY||out==0)return 0U;*out=g_analysis[track].plan;
 return out->valid;}
