#include "NoteFx/note_fx_engine.h"
#include <string.h>
#include "NoteFx/note_fx_euclid.h"
#include "NoteFx/note_fx_context.h"
#include "NoteFx/note_fx_plan.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_capacity_contract.h"
#include "Platform/memory_layout.h"
#include "Keyboard/kbd_chords_dict.h"


typedef struct {uint32_t source_token,generation,group_id,lifetime_start,lifetime_end;
 uint8_t note,velocity,order,released;} note_fx_held_pitch_t;
typedef struct {uint8_t model,p1,p2,p3,p4;} note_fx_slot_runtime_t;
typedef struct {uint8_t owner_slot,held_count;} note_fx_family_runtime_t;

static const uint8_t g_harmony[NOTE_FX_VOICER_TYPE_COUNT][4]={
 {0,4,7,255},{0,3,7,255},{0,5,7,255},{0,2,7,255},
 {0,4,7,10},{0,4,7,11},{0,3,6,10},{0,4,8,255}};
_Static_assert(sizeof(note_fx_slot_runtime_t)==5U,"Note FX slot runtime budget");
_Static_assert(sizeof(note_fx_held_pitch_t)==24U,"Note FX held-state budget");
_Static_assert(sizeof(g_harmony)==32U,"Harmony table proof");

typedef struct { note_fx_slot_runtime_t slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
 uint64_t work_slot_mask;uint32_t token,samples_per_step_q16;
 uint64_t transport_position_q16,block_start;uint32_t pattern_position_q16[NOTE_FX_TRACK_COUNT];
 uint8_t pattern_length[NOTE_FX_TRACK_COUNT];
 uint8_t active_slot_mask[NOTE_FX_TRACK_COUNT];
 uint8_t temporal_slot_mask[NOTE_FX_TRACK_COUNT];
 uint8_t scale_index,root_index;
} note_fx_engine_context_t;
static CONTROL_M4_SRAM2 note_fx_engine_context_t g_seq_context;
static CONTROL_M4_SRAM2 note_fx_held_pitch_t
    g_held[2][SEQ_PRODUCT_HELD_STATE_CAPACITY];
static CONTROL_M4_SRAM2 uint64_t
    g_held_active_mask[2][SEQ_PRODUCT_HARMONY_FANOUT_MAX];
static CONTROL_M4_SRAM2 note_fx_family_runtime_t
    g_family[NOTE_FX_TRACK_COUNT][2];
static SEQ_STATE_D2 uint64_t
    g_generated_until[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
static note_fx_engine_context_t *const g_context=&g_seq_context;
#define g_slot (g_context->slot)
#define g_work_slot_mask (g_context->work_slot_mask)
#define g_token (g_context->token)
#define g_samples_per_step_q16 (g_context->samples_per_step_q16)
static uint64_t slot_bit(uint8_t t,uint8_t s){return UINT64_C(1)<<((uint32_t)t*NOTE_FX_SLOT_COUNT+s);}
static uint8_t model_is_arp(uint8_t model){return model==NOTE_FX_MODEL_ARP?1U:0U;}
static uint8_t model_is_generator(uint8_t model){return (model_is_arp(model)||model==NOTE_FX_MODEL_EUCLID)?1U:0U;}
static uint8_t model_needs_held(uint8_t model){return model_is_generator(model);}
static uint8_t held_family(uint8_t model){return(model==NOTE_FX_MODEL_EUCLID)?1U:0U;}
static uint8_t held_has_deadline(const note_fx_held_pitch_t*h){return(uint8_t)(h->lifetime_end!=0U);}
static uint8_t held_is_active_at(const note_fx_held_pitch_t*h,uint64_t sample){return(uint8_t)(h->source_token!=0U&&((int32_t)((uint32_t)sample-h->lifetime_start)>=0)&&(!held_has_deadline(h)||((int32_t)((uint32_t)sample-h->lifetime_end)<0)));}
static uint8_t held_is_active_for(const note_fx_slot_runtime_t*r,const note_fx_held_pitch_t*h,uint64_t sample){if(r->model==NOTE_FX_MODEL_ARP&&r->p4!=0U)return(uint8_t)(h->source_token!=0U&&((int32_t)((uint32_t)sample-h->lifetime_start)>=0));return held_is_active_at(h,sample);}
static uint8_t held_deadline_reached(const note_fx_held_pitch_t*h,uint64_t sample){return(uint8_t)(held_has_deadline(h)&&((int32_t)((uint32_t)sample-h->lifetime_end)>=0));}
static uint32_t held_remaining(const note_fx_held_pitch_t*h,uint64_t sample,uint32_t fallback){if(!held_has_deadline(h))return fallback;const uint32_t remaining=h->lifetime_end-(uint32_t)sample;return remaining<fallback?remaining:fallback;}
static note_fx_family_runtime_t*family_state(uint8_t t,uint8_t s)
{return &g_family[t][held_family(g_slot[t][s].model)];}
static uint8_t family_owns(uint8_t t,uint8_t s)
{return(uint8_t)(model_is_generator(g_slot[t][s].model)&&family_state(t,s)->owner_slot==s);}
static void refresh_work(uint8_t t,uint8_t s){const uint64_t b=slot_bit(t,s);if(family_owns(t,s)&&family_state(t,s)->held_count)g_work_slot_mask|=b;else g_work_slot_mask&=~b;}
static uint32_t next_token(void){g_token=(g_token+1U)&NOTE_EVENT_OCCURRENCE_COUNTER_MASK;if(!g_token)g_token=1U;return NOTE_EVENT_OCCURRENCE_NAMESPACE_FX|g_token;}
static uint32_t mix32(uint32_t x){x^=x>>16;x*=UINT32_C(0x7FEB352D);x^=x>>15;x*=UINT32_C(0x846CA68B);return x^(x>>16);}
static uint32_t child_id(uint32_t p,uint8_t s,uint8_t v,uint8_t r){uint32_t x=mix32(p^((uint32_t)(s+1U)<<24)^((uint32_t)(v+1U)<<12)^((uint32_t)(r+1U)<<4))&NOTE_EVENT_OCCURRENCE_COUNTER_MASK;if(!x)x=1U;return NOTE_EVENT_OCCURRENCE_NAMESPACE_FX|x;}
static uint16_t product_lane(const note_event_t*e){
 if(e->track<BRICK_ENTITY_TOP_LEVEL_COUNT&&e->temporal_index<SEQ_LOGICAL_CAPACITY_MAX)
  {if(e->track==BRICK_ENTITY_GROUP_MASTER_ID)return UINT16_MAX;
   return(uint16_t)((uint16_t)e->track*SEQ_LOGICAL_CAPACITY_MAX+e->temporal_index);
  }
 if(e->track>=BRICK_ENTITY_FIRST_GROUP_CHILD_ID&&e->track<NOTE_FX_TRACK_COUNT
      &&e->temporal_index==0U)
  return(uint16_t)(((BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX)
      +(e->track-BRICK_ENTITY_FIRST_GROUP_CHILD_ID));
 return UINT16_MAX;}
static uint64_t track_lane_mask(uint8_t track){
 if(track<BRICK_ENTITY_GROUP_MASTER_ID)
  return UINT64_C(0xFF)<<((uint32_t)track*SEQ_LOGICAL_CAPACITY_MAX);
 if(track>=BRICK_ENTITY_FIRST_GROUP_CHILD_ID&&track<NOTE_FX_TRACK_COUNT)
  return UINT64_C(1)<<((BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX
      +(track-BRICK_ENTITY_FIRST_GROUP_CHILD_ID));
 return 0U;}
static uint8_t lane_temporal(uint8_t track,uint16_t lane){return(track<BRICK_ENTITY_TOP_LEVEL_COUNT)?(uint8_t)(lane%SEQ_LOGICAL_CAPACITY_MAX):0U;}
static void held_remove(uint8_t family,note_fx_family_runtime_t*f,uint16_t lane,uint8_t branch){note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(!h->source_token)return;memset(h,0,sizeof(*h));g_held_active_mask[family][branch]&=~(UINT64_C(1)<<lane);if(f->held_count)--f->held_count;}
static uint64_t held_active_lanes(uint8_t family,uint64_t lanes){uint64_t active=0U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)active|=g_held_active_mask[family][branch];return active&lanes;}
static uint8_t arp_has_physical_held(uint8_t t,uint8_t family,uint64_t sample){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;const note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(!h->released&&held_is_active_at(h,sample))return 1U;}}return 0U;}
static void arp_begin_capture(uint8_t t,uint8_t family,uint64_t sample){note_fx_family_runtime_t*f=&g_family[t][family];if(arp_has_physical_held(t,family,sample))return;const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;held_remove(family,f,lane,branch);}}}
static note_event_result_t held_ingest(uint8_t slot,note_fx_slot_runtime_t*r,const note_event_t*e){const uint16_t lane=product_lane(e);const uint8_t branch=note_event_branch(e);const uint8_t family=held_family(r->model);note_fx_family_runtime_t*f=family_state(e->track,slot);if(!family_owns(e->track,slot))return NOTE_EVENT_RESULT_ACCEPTED;if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES||branch>=SEQ_PRODUCT_HARMONY_FANOUT_MAX)return NOTE_EVENT_RESULT_REJECTED_DESTINATION;const uint8_t arp_hold=(uint8_t)(r->model==NOTE_FX_MODEL_ARP&&r->p4!=0U);note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(e->kind==NOTE_EVENT_KIND_OFF){if(h->source_token==e->source_id&&h->generation==e->source_generation){if(arp_hold)h->released=1U;else held_remove(family,f,lane,branch);}return NOTE_EVENT_RESULT_ACCEPTED;}if(e->duration_samples==0U)return NOTE_EVENT_RESULT_DROPPED_POLICY;if(arp_hold){arp_begin_capture(e->track,family,e->sample_abs);h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];}if(!h->source_token)++f->held_count;const uint32_t lifetime_end=((e->flags&NOTE_EVENT_FLAG_HELD)!=0U)?0U:(uint32_t)(e->sample_abs+e->duration_samples);*h=(note_fx_held_pitch_t){.source_token=e->source_id,.generation=e->source_generation,.group_id=e->group_id,.lifetime_start=(uint32_t)e->sample_abs,.lifetime_end=lifetime_end,.note=e->note,.velocity=e->velocity,.order=note_event_order(e),.released=0U};g_held_active_mask[family][branch]|=UINT64_C(1)<<lane;return NOTE_EVENT_RESULT_ACCEPTED;}
static void held_expire(uint8_t t,uint8_t s,uint64_t sample){note_fx_slot_runtime_t*r=&g_slot[t][s];const uint8_t family=held_family(r->model);note_fx_family_runtime_t*f=family_state(t,s);const uint8_t arp_hold=(uint8_t)(r->model==NOTE_FX_MODEL_ARP&&r->p4!=0U);const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(held_deadline_reached(h,sample)){if(arp_hold)h->released=1U;else held_remove(family,f,lane,branch);}}}}
static uint8_t held_count_at(uint8_t t,note_fx_slot_runtime_t*r,uint64_t sample){uint8_t count=0U;const uint8_t family=held_family(r->model);uint64_t active=held_active_lanes(family,track_lane_mask(t));while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))!=0U&&held_is_active_for(r,&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],sample))++count;}return count;}
static note_fx_held_pitch_t*held_rank(uint8_t t,uint8_t s,note_fx_slot_runtime_t*r,uint8_t rank,uint8_t sorted,uint64_t sample){note_fx_held_pitch_t*items[SEQ_LOGICAL_CAPACITY_MAX*SEQ_PRODUCT_HARMONY_FANOUT_MAX];uint8_t count=0;const uint8_t family=held_family(r->model);uint64_t active=held_active_lanes(family,track_lane_mask(t));while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))!=0U&&held_is_active_for(r,&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],sample))items[count++]=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];}if(rank>=count)return NULL;if(sorted)for(uint8_t i=1;i<count;++i){note_fx_held_pitch_t*x=items[i];uint8_t j=i;while(j&&items[j-1U]->note>x->note){items[j]=items[j-1U];--j;}items[j]=x;}return items[rank];}
static uint8_t arp_select(note_fx_slot_runtime_t*r,uint32_t step,uint8_t track,uint8_t slot,note_fx_held_pitch_t**oh,uint8_t*on,uint64_t sample){const uint8_t held_count=held_count_at(track,r,sample);if(!held_count||!oh||!on)return 0;const note_fx_arp_style_t style=(note_fx_arp_style_t)r->p1;uint8_t rank;if(style==NOTE_FX_ARP_RANDOM){rank=(uint8_t)(mix32(step^((uint32_t)track<<24)^((uint32_t)slot<<16)^UINT32_C(0x9E3779B9))%held_count);}else if(style==NOTE_FX_ARP_UP_DOWN&&held_count>1U){const uint8_t cycle=(uint8_t)(2U*held_count-2U),pos=(uint8_t)(step%cycle);rank=pos<held_count?pos:(uint8_t)(cycle-pos);}else{rank=(uint8_t)(step%held_count);if(style==NOTE_FX_ARP_DOWN)rank=(uint8_t)(held_count-1U-rank);}note_fx_held_pitch_t*h=held_rank(track,slot,r,rank,(uint8_t)(style!=NOTE_FX_ARP_ORDER),sample);if(!h)return 0;const uint8_t range=r->p3>=1&&r->p3<=4?r->p3:1;const uint32_t cycle=style==NOTE_FX_ARP_UP_DOWN&&held_count>1?(uint32_t)(2U*held_count-2U):held_count;const uint16_t raised=(uint16_t)h->note+(uint16_t)(12U*((step/cycle)%range));*on=raised<128U?(uint8_t)raised:h->note;*oh=h;return 1;}
static note_event_result_t emit_generated(uint8_t t,uint8_t s,const note_fx_held_pitch_t*h,uint8_t n,uint64_t sample,uint64_t duration,note_fx_emit_fn emit,void*ctx)
{
 if(!emit)return NOTE_EVENT_RESULT_ACCEPTED;
 const uint32_t token=next_token();const uint32_t fallback=duration>UINT32_MAX?UINT32_MAX:(uint32_t)duration;const uint32_t bounded_duration=(g_slot[t][s].model==NOTE_FX_MODEL_ARP&&g_slot[t][s].p4!=0U)?fallback:held_remaining(h,sample,fallback);const note_fx_held_pitch_t*base=&g_held[held_family(g_slot[t][s].model)][0];const uint16_t held_index=(uint16_t)(h-base);const uint16_t lane=(uint16_t)(held_index/SEQ_PRODUCT_HARMONY_FANOUT_MAX);const uint8_t branch=(uint8_t)(held_index%SEQ_PRODUCT_HARMONY_FANOUT_MAX);uint32_t group=mix32(h->group_id^(uint32_t)sample^(uint32_t)(sample>>32U)^((uint32_t)(s+1U)<<24U));if(!group)group=1U;note_event_t e={.sample_abs=sample,.duration_samples=bounded_duration?bounded_duration:1U,.source_id=h->source_token,.occurrence_id=token,.source_generation=h->generation,.group_id=group,.track=t,.note=n,.velocity=h->velocity,.kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_FX,.stage=(uint8_t)(note_fx_plan_position_of(h->order,s)+1U),.flags=NOTE_EVENT_FLAG_GENERATED,.temporal_index=lane_temporal(t,lane),.branch=branch,.timing_class=NOTE_EVENT_TIMING_SCHEDULED};note_event_set_order(&e,h->order);
 return emit(&e,ctx);
}
static uint8_t append(note_event_t*out,uint8_t cap,uint8_t*count,const note_event_t*e,uint8_t stage){if(*count>=cap)return 0;out[*count]=*e;out[*count].stage=stage;++*count;return 1;}
static uint64_t step_samples(void){const uint64_t x=((uint64_t)g_samples_per_step_q16+0x8000ULL)>>16;return x?x:1U;}
static uint64_t position_sample(uint64_t reference_sample,
    uint64_t reference_position,uint64_t target_position,uint32_t sps)
{const int64_t delta=(target_position>=reference_position)
    ?(int64_t)(target_position-reference_position)
    :-(int64_t)(reference_position-target_position);
 const int64_t product=delta*(int64_t)sps;
 const int64_t offset=(product>=0)
    ?(product+INT64_C(0x80000000))/INT64_C(0x100000000)
    :-((-product+INT64_C(0x80000000))/INT64_C(0x100000000));
 return(offset>=0)?reference_sample+(uint64_t)offset
    :(((uint64_t)(-offset)>reference_sample)?0U
        :reference_sample-(uint64_t)(-offset));}
static uint64_t event_transport_position_q16(const note_event_t*e){
 const uint32_t sps=g_samples_per_step_q16?g_samples_per_step_q16:1U;
 if(e->sample_abs>=g_context->block_start){const uint64_t delta=e->sample_abs-g_context->block_start;
  return g_context->transport_position_q16+(delta/sps<<32U)+((delta%sps)<<32U)/sps;}
 const uint64_t delta=g_context->block_start-e->sample_abs;
 const uint64_t offset=(delta/sps<<32U)+((delta%sps)<<32U)/sps;
 return offset<g_context->transport_position_q16
     ?g_context->transport_position_q16-offset:0U;}
static uint64_t probability_lot_index(uint64_t position_q16,uint8_t division){
 const seq_division_desc_t*d=seq_division_get(division);
 if(!d)return position_q16;
 const uint64_t lot_q16=(uint64_t)d->numerator<<16U;
 return(position_q16/lot_q16)*d->denominator
     +((position_q16%lot_q16)*d->denominator)/lot_q16;}
static uint8_t probability_keep_match(uint64_t position_q16,uint8_t division){
 const seq_division_desc_t*d=seq_division_get(division);if(d==NULL)return 0U;
 const uint64_t grid=(uint64_t)d->numerator<<16U;
 const uint64_t phase=((position_q16%grid)*d->denominator)%grid;
 const uint64_t sample_tolerance=((UINT64_C(1)<<31U)+(g_samples_per_step_q16-1U))/g_samples_per_step_q16+1U;
 const uint64_t tolerance=sample_tolerance*d->denominator;
 return(uint8_t)(phase<=tolerance||grid-phase<=tolerance);}
static uint8_t probability_pass(const note_fx_slot_runtime_t*r,const note_event_t*e,uint8_t slot){static const uint8_t num[10]={0,1,2,1,2,3,1,2,3,4},den[10]={1,2,2,3,3,3,4,4,4,4};if(e->kind==NOTE_EVENT_KIND_OFF)return 1U;const uint8_t condition=r->p2<10U?r->p2:0U;const uint64_t position=event_transport_position_q16(e);uint64_t identity=e->group_id;if(r->p3!=0U)identity=probability_lot_index(position,(uint8_t)(r->p3-1U));const uint64_t cycle=position/(UINT64_C(16)<<16U);const uint32_t draw=mix32((uint32_t)identity^(uint32_t)(identity>>32)^((uint32_t)e->track<<24)^((uint32_t)slot<<16)^(uint32_t)cycle)%100U;uint8_t pass=(uint8_t)(r->p1>=100U||(r->p1!=0U&&draw<r->p1));if(r->p4!=0U&&probability_keep_match(position,(uint8_t)(r->p4-1U)))pass=1U;if(condition!=0U&&((uint8_t)(cycle%den[condition])+1U)!=num[condition])pass=0U;return pass;}
static uint32_t event_pattern_position_q16(const note_event_t*e){const uint64_t loop=(uint64_t)(g_context->pattern_length[e->track]?g_context->pattern_length[e->track]:1U)<<16U;const uint64_t now=event_transport_position_q16(e);const int64_t delta=(now>=g_context->transport_position_q16)?(int64_t)(now-g_context->transport_position_q16):-(int64_t)(g_context->transport_position_q16-now);int64_t position=(int64_t)g_context->pattern_position_q16[e->track]+delta;position%=(int64_t)loop;if(position<0)position+=(int64_t)loop;return(uint32_t)position;}
static uint8_t scaler_map(note_fx_slot_runtime_t*r,uint8_t input,uint8_t*out){int16_t transposed=(int16_t)input+(int16_t)r->p4-12;if(transposed<0)transposed=0;if(transposed>127)transposed=127;const uint8_t scale=(r->p1<KBD_SCALE_COUNT)?r->p1:KBD_SCALE_ID_MAJOR;const uint8_t root=r->p2%12U;const uint8_t relative=(uint8_t)(((uint8_t)transposed+12U-root)%12U);if(kbd_scale_contains_pitch_class(scale,relative)){*out=(uint8_t)transposed;return 1U;}if(r->p3==NOTE_FX_SCALER_STICK_DROP)return 0U;const int8_t direction=(r->p3==NOTE_FX_SCALER_STICK_UP)?1:-1;for(uint8_t distance=1U;distance<12U;++distance){const int16_t candidate=transposed+(int16_t)direction*distance;if(candidate<0||candidate>127)continue;const uint8_t pc=(uint8_t)(((uint8_t)candidate+12U-root)%12U);if(kbd_scale_contains_pitch_class(scale,pc)){*out=(uint8_t)candidate;return 1U;}}return 0U;}

static note_event_result_t direct(uint8_t slot,uint8_t position,note_fx_slot_runtime_t*r,const note_event_t*e,note_event_t*out,uint8_t cap,uint8_t*count){const uint8_t stage=(uint8_t)(position+1U);
 if(r->model==NOTE_FX_MODEL_PROBABILITY){const uint8_t pass=probability_pass(r,e,slot);return!pass?NOTE_EVENT_RESULT_ACCEPTED:(append(out,cap,count,e,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY);}
 if(r->model==NOTE_FX_MODEL_GATE){if(e->kind==NOTE_EVENT_KIND_OFF)return NOTE_EVENT_RESULT_ACCEPTED;note_event_t on=*e;on.flags=(uint8_t)(on.flags&~NOTE_EVENT_FLAG_HELD);int32_t var=0;if(r->p2){const uint32_t pattern=event_pattern_position_q16(e);const uint32_t identity=pattern^(pattern>>16U)^((uint32_t)e->track<<27U)^((uint32_t)slot<<24U)^((uint32_t)e->temporal_index<<16U)^((uint32_t)e->branch<<8U)^r->p4;const uint32_t h=mix32(identity);var=(int32_t)(h%(2U*r->p2+1U))-(int32_t)r->p2;}int32_t pct=(int32_t)r->p1+var;if(pct<1)pct=1;if(r->p3==NOTE_FX_GATE_MODE_CLIP&&pct>100)pct=100;else if(pct>200)pct=200;on.duration_samples=(uint32_t)(((uint64_t)pct*step_samples()+50U)/100U);return append(out,cap,count,&on,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 if(r->model==NOTE_FX_MODEL_SCALER){note_event_t mapped=*e;if(!scaler_map(r,e->note,&mapped.note))return NOTE_EVENT_RESULT_ACCEPTED;return append(out,cap,count,&mapped,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 return append(out,cap,count,e,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}

static note_event_result_t voicer_group(uint8_t slot,uint8_t position,
 note_fx_slot_runtime_t*r,const note_event_t*in,uint8_t n,note_event_t*out,
 uint8_t cap,uint8_t*count)
{const uint8_t stage=(uint8_t)(position+1U);const uint8_t voices=(r->p4>=1U&&r->p4<=SEQ_PRODUCT_HARMONY_FANOUT_MAX)?r->p4:1U;*count=0U;
 if(in[0].kind==NOTE_EVENT_KIND_OFF){
  for(uint8_t i=0U;i<n;++i)for(uint8_t voice=0U;
       voice<voices;++voice){
   if(g_harmony[r->p1%NOTE_FX_VOICER_TYPE_COUNT][voice]==255U)continue;
   note_event_t x=in[i];
   x.branch=voice;
   if(voice){x.occurrence_id=child_id(in[i].occurrence_id,slot,voice,0U);
    x.provenance=NOTE_EVENT_SOURCE_FX;x.flags|=NOTE_EVENT_FLAG_GENERATED;}
   if(!append(out,cap,count,&x,stage))return NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
  return NOTE_EVENT_RESULT_ACCEPTED;}
 for(uint8_t voice=0U;voice<voices;++voice)
  for(uint8_t root_class=0U;root_class<(uint8_t)(voice?1U:2U);++root_class)
  for(uint8_t i=0U;i<n;++i){
   if(voice==0U){const uint8_t generated=(uint8_t)
     ((in[i].flags&NOTE_EVENT_FLAG_GENERATED)!=0U);
    if(generated!=root_class)continue;}
   uint8_t interval=
    g_harmony[r->p1%NOTE_FX_VOICER_TYPE_COUNT][voice];
   if(interval==255U)continue;
   if(voice<r->p3)interval=(uint8_t)(interval+12U);
   if(r->p2&&voice)interval=(uint8_t)(interval+12U*r->p2*voice);
   if((uint16_t)in[i].note+interval>=128U)continue;
   note_event_t x=in[i];x.note=(uint8_t)(in[i].note+interval);
   x.branch=voice;
   if(voice){x.occurrence_id=child_id(in[i].occurrence_id,slot,voice,0U);
    x.provenance=NOTE_EVENT_SOURCE_FX;x.flags|=NOTE_EVENT_FLAG_GENERATED;}
   uint8_t duplicate=0U;for(uint8_t j=0U;j<*count;++j)
    if(out[j].group_id==x.group_id&&out[j].note==x.note)duplicate=1U;
   if(duplicate)continue;
   if(*count>=cap)return NOTE_EVENT_RESULT_ACCEPTED;
   if(!append(out,cap,count,&x,stage))return NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 return *count?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_DROPPED_POLICY;}

void note_fx_engine_init(void){memset(&g_seq_context,0,sizeof(g_seq_context));memset(g_held,0,sizeof(g_held));memset(g_held_active_mask,0,sizeof(g_held_active_mask));memset(g_family,0,sizeof(g_family));memset(g_generated_until,0,sizeof(g_generated_until));for(uint8_t t=0;t<NOTE_FX_TRACK_COUNT;++t)for(uint8_t f=0;f<2U;++f)g_family[t][f].owner_slot=UINT8_MAX;g_seq_context.samples_per_step_q16=UINT32_C(65536);}
void note_fx_engine_set_samples_per_step_q16(uint32_t v){g_samples_per_step_q16=v?v:1U;}
void note_fx_engine_set_time_reference(uint64_t sample,uint64_t transport,uint32_t sps){g_context->block_start=sample;g_context->transport_position_q16=transport;note_fx_engine_set_samples_per_step_q16(sps);}
static void held_clear_family(uint8_t t,uint8_t family){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;memset(&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],0,sizeof(note_fx_held_pitch_t));}g_held_active_mask[family][branch]&=~lanes;}g_family[t][family].held_count=0U;}
static void held_release_latched(uint8_t t,uint8_t family,uint64_t sample){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(h->released||held_deadline_reached(h,sample))held_remove(family,&g_family[t][family],lane,branch);}}}
static void family_reselect(uint8_t t,uint8_t family){note_fx_family_runtime_t*f=&g_family[t][family];uint8_t owner=UINT8_MAX;for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)if(model_is_generator(g_slot[t][s].model)&&held_family(g_slot[t][s].model)==family){owner=s;break;}if(owner!=f->owner_slot){held_clear_family(t,family);f->owner_slot=owner;}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
note_event_result_t note_fx_engine_configure(uint8_t t,uint8_t s,uint8_t model,uint8_t p1,uint8_t p2,uint8_t p3,uint8_t p4){if(t>=NOTE_FX_TRACK_COUNT||s>=NOTE_FX_SLOT_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;note_fx_slot_runtime_t*r=&g_slot[t][s];if(model>=NOTE_FX_MODEL_COUNT)model=NOTE_FX_MODEL_OFF;const uint8_t old_model=r->model,old_hold=(uint8_t)(old_model==NOTE_FX_MODEL_ARP&&r->p4!=0U);if(old_model!=model)memset(r,0,sizeof(*r));r->model=model;r->p1=p1;r->p2=p2;r->p3=p3;r->p4=p4;const uint8_t bit=(uint8_t)(1U<<s);if(model==NOTE_FX_MODEL_OFF)g_context->active_slot_mask[t]&=(uint8_t)~bit;else g_context->active_slot_mask[t]|=bit;if(model_is_generator(model))g_context->temporal_slot_mask[t]|=bit;else g_context->temporal_slot_mask[t]&=(uint8_t)~bit;if(model_is_generator(old_model))family_reselect(t,held_family(old_model));if(model_is_generator(model))family_reselect(t,held_family(model));if(old_hold&&model==NOTE_FX_MODEL_ARP&&p4==0U)held_release_latched(t,held_family(model),g_context->block_start);refresh_work(t,s);return NOTE_EVENT_RESULT_ACCEPTED;}
uint8_t note_fx_engine_slot_at(uint8_t t,uint8_t order,uint8_t position){if(t>=NOTE_FX_TRACK_COUNT)return NOTE_FX_SLOT_COUNT;const uint8_t s=note_fx_plan_slot_at(order,position);return(s<NOTE_FX_SLOT_COUNT&&((g_context->active_slot_mask[t]&(uint8_t)(1U<<s))!=0U))?s:NOTE_FX_SLOT_COUNT;}
uint8_t note_fx_engine_suffix_is_temporal(uint8_t t,uint8_t order,uint8_t stage){if(t>=NOTE_FX_TRACK_COUNT||stage>=NOTE_FX_SLOT_COUNT)return 0U;for(uint8_t p=stage;p<NOTE_FX_SLOT_COUNT;++p){const uint8_t s=note_fx_plan_slot_at(order,p);if(s<NOTE_FX_SLOT_COUNT&&(g_context->temporal_slot_mask[t]&(uint8_t)(1U<<s))!=0U)return 1U;}return 0U;}
static note_event_result_t transform_core(uint8_t s,uint8_t position,const note_event_t*in,uint8_t n,note_event_t*out,uint8_t cap,uint8_t*count,uint8_t validate)
{
 if(!in||!out||!count||!n||s>=NOTE_FX_SLOT_COUNT){
  return NOTE_EVENT_RESULT_DROPPED_POLICY;}
 if(in[0].track>=NOTE_FX_TRACK_COUNT
      ||note_event_order(&in[0])>=NOTE_FX_ORDER_COUNT
      ||note_fx_plan_slot_at(note_event_order(&in[0]),position)!=s
      ||(validate!=0U?(in[0].stage!=position):(in[0].stage>position))
      ||(validate!=0U&&!note_event_is_valid(&in[0]))){
  return NOTE_EVENT_RESULT_DROPPED_POLICY;}
 note_fx_slot_runtime_t*r=&g_slot[in[0].track][s];
 for(uint8_t i=1U;validate!=0U&&i<n;++i)if(!note_event_is_valid(&in[i])
      ||in[i].track!=in[0].track||in[i].stage!=position||in[i].kind!=in[0].kind
      ||(r->model!=NOTE_FX_MODEL_VOICER&&in[i].group_id!=in[0].group_id)){
  return NOTE_EVENT_RESULT_DROPPED_POLICY;}
 if(model_needs_held(r->model))for(uint8_t i=0;i<n;++i){
  const note_event_result_t held_result=held_ingest(s,r,&in[i]);
  if(held_result!=NOTE_EVENT_RESULT_ACCEPTED){
   return held_result;}}
 refresh_work(in[0].track,s);
 if(r->model==NOTE_FX_MODEL_VOICER)
  return voicer_group(s,position,r,in,n,out,cap,count);
 *count=0;
 for(uint8_t i=0;i<n;++i){const note_event_t*e=&in[i];
  if(!model_is_arp(r->model)&&r->model!=NOTE_FX_MODEL_EUCLID){
   const note_event_result_t result=direct(s,position,r,e,out,cap,count);
   if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}}
 return NOTE_EVENT_RESULT_ACCEPTED;
}
note_event_result_t note_fx_engine_transform(uint8_t s,uint8_t p,const note_event_t*in,uint8_t n,note_event_t*out,uint8_t cap,uint8_t*count){return transform_core(s,p,in,n,out,cap,count,1U);}
note_event_result_t note_fx_engine_transform_prepared(uint8_t s,uint8_t p,const note_event_t*in,uint8_t n,note_event_t*out,uint8_t cap,uint8_t*count){return transform_core(s,p,in,n,out,cap,count,0U);}
void note_fx_engine_forget_causal_source(uint8_t t,uint32_t source){if(t>=NOTE_FX_TRACK_COUNT||!source)return;const uint64_t lanes=track_lane_mask(t);for(uint8_t family=0;family<2U;++family)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(h->source_token==source)held_remove(family,&g_family[t][family],lane,branch);}}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
static note_event_result_t note_fx_engine_process_due(uint8_t track,uint64_t start,
    uint32_t horizon_samples,uint32_t sps,note_fx_emit_fn emit,void*ctx)
{
 if(track>=NOTE_FX_TRACK_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;
 note_fx_engine_set_samples_per_step_q16(sps);
 const uint64_t end=start+horizon_samples;
 const uint64_t track_slots=((UINT64_C(1)<<NOTE_FX_SLOT_COUNT)-1U)
     <<((uint32_t)track*NOTE_FX_SLOT_COUNT);
 uint64_t processed=0U,work;
 while((work=(g_work_slot_mask&track_slots&~processed))!=0U){
  const uint32_t idx=(uint32_t)__builtin_ctzll(work);
  processed|=UINT64_C(1)<<idx;
  const uint8_t t=(uint8_t)(idx/NOTE_FX_SLOT_COUNT);
  const uint8_t s=(uint8_t)(idx%NOTE_FX_SLOT_COUNT);
  note_fx_slot_runtime_t*r=&g_slot[t][s];
  note_fx_family_runtime_t*f=family_state(t,s);
  uint64_t from=g_generated_until[t][s];if(from<start)from=start;
  held_expire(t,s,from);refresh_work(t,s);
  if(!f->held_count||!family_owns(t,s)||from>=end)continue;
   const uint8_t division=r->model==NOTE_FX_MODEL_EUCLID?r->p3:r->p2;
   const seq_division_desc_t*division_desc=seq_division_get(division);
   const uint64_t period_q16=division_desc?division_desc->ratio_q16:UINT64_C(65536);
   uint64_t period=((period_q16*sps)+UINT64_C(0x80000000))>>32U;
   if(!period)period=1U;
   const note_fx_phase_policy_t policy=NOTE_FX_PHASE_PATTERN_SYNC;
   const uint64_t reference_position=(policy==NOTE_FX_PHASE_TRANSPORT_FREE)
       ?g_context->transport_position_q16:g_context->pattern_position_q16[t];
   const uint64_t from_delta_q16=((from-start)<<32U)/sps;
   const uint64_t position=reference_position+from_delta_q16;
   uint64_t target_position,ordinal,loop=0U;
   const uint64_t loop_q16=(policy==NOTE_FX_PHASE_PATTERN_SYNC)
       ?(uint64_t)(g_context->pattern_length[t]
           ?g_context->pattern_length[t]:1U)<<16U:0U;
   if(policy==NOTE_FX_PHASE_TRANSPORT_FREE){
    ordinal=position/period_q16;
    target_position=ordinal*period_q16;
   }else{
    loop=position/loop_q16;
    const uint64_t phase=position%loop_q16;
    ordinal=phase/period_q16;
    uint64_t target_phase=ordinal*period_q16;
    target_position=loop*loop_q16+target_phase;
   }
   uint64_t sample=position_sample(start,reference_position,target_position,sps);
   while(sample<from){
    ++ordinal;
    if(policy==NOTE_FX_PHASE_PATTERN_SYNC){
     uint64_t target_phase=ordinal*period_q16;
     if(target_phase>=loop_q16){++loop;ordinal=0U;target_phase=0U;}
     target_position=loop*loop_q16+target_phase;
    }else target_position=ordinal*period_q16;
    sample=position_sample(start,reference_position,target_position,sps);}
   uint64_t covered_until=end;
  while(sample<end){
   held_expire(t,s,sample);if(!f->held_count){covered_until=sample;break;}
   if(model_is_arp(r->model)){
    note_fx_held_pitch_t*h=NULL;uint8_t note=0U;
    if(arp_select(r,(uint32_t)ordinal,t,s,&h,&note,sample)){
     const note_event_result_t result=emit_generated(t,s,h,note,sample,
         period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}}
   else{const uint8_t length=r->p1?r->p1:NOTE_FX_EUCLID_LENGTH_DEFAULT;
    const uint8_t pulse=r->p2<=length?r->p2:length;
    const uint64_t mask=euclid_build_mask(length,pulse);
     const uint8_t rotate=(uint8_t)(r->p4%length);
     const uint8_t pattern_step=(uint8_t)((ordinal+length-rotate)%length);
     if((mask>>pattern_step)&UINT64_C(1)){
     const uint8_t family=held_family(r->model);
     uint64_t active=held_active_lanes(family,track_lane_mask(t));
     while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);
      active&=active-1U;
      for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){
       if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))==0U)continue;
       const note_fx_held_pitch_t*h=&g_held[family]
           [lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];
       if(!held_is_active_at(h,sample))continue;
       const note_event_result_t result=emit_generated(t,s,h,h->note,sample,
           period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}}}}
   ++ordinal;
   if(policy==NOTE_FX_PHASE_PATTERN_SYNC){
    uint64_t target_phase=ordinal*period_q16;
    if(target_phase>=loop_q16){++loop;ordinal=0U;target_phase=0U;}
    target_position=loop*loop_q16+target_phase;
   }else target_position=ordinal*period_q16;
   sample=position_sample(start,reference_position,target_position,sps);}
  g_generated_until[t][s]=covered_until;refresh_work(t,s);}
 return NOTE_EVENT_RESULT_ACCEPTED;
}
void note_fx_engine_release_terminal(const note_event_t*e){if(!e||e->track>=NOTE_FX_TRACK_COUNT||e->kind!=NOTE_EVENT_KIND_OFF)return;const uint64_t lanes=track_lane_mask(e->track);for(uint8_t family=0U;family<2U;++family)for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(h->source_token!=e->source_id)continue;const uint8_t owner=g_family[e->track][family].owner_slot;if(family==0U&&owner<NOTE_FX_SLOT_COUNT&&g_slot[e->track][owner].model==NOTE_FX_MODEL_ARP&&g_slot[e->track][owner].p4!=0U)h->released=1U;else held_remove(family,&g_family[e->track][family],lane,branch);}}for(uint8_t s=0U;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(e->track,s);}
note_event_result_t note_fx_engine_cleanup(uint8_t t){if(t>=NOTE_FX_TRACK_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;for(uint8_t family=0;family<2U;++family)held_clear_family(t,family);for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);return NOTE_EVENT_RESULT_ACCEPTED;}

note_event_result_t note_fx_engine_process(uint8_t track,uint64_t start,uint32_t horizon_samples,uint32_t sps,uint64_t transport,const uint32_t pattern[NOTE_FX_TRACK_COUNT],const uint8_t pattern_length[NOTE_FX_TRACK_COUNT],uint8_t scale,uint8_t root,note_fx_emit_fn emit,void*ctx){note_fx_engine_set_time_reference(start,transport,sps);g_seq_context.scale_index=scale;g_seq_context.root_index=root;memcpy(g_seq_context.pattern_position_q16,pattern,sizeof(g_seq_context.pattern_position_q16));memcpy(g_seq_context.pattern_length,pattern_length,sizeof(g_seq_context.pattern_length));return note_fx_engine_process_due(track,start,horizon_samples,sps,emit,ctx);}
