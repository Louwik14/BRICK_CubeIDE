#include "NoteFx/note_fx_engine.h"
#include <string.h>
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_euclid.h"
#include "NoteFx/note_fx_context.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_capacity_contract.h"
#include "Platform/memory_layout.h"
#include "Keyboard/kbd_chords_dict.h"

typedef struct {uint32_t source_token,generation,group_id,lifetime_start,lifetime_end;
 uint8_t note,velocity,released;} note_fx_held_pitch_t;
typedef struct {uint8_t p1,p2,p3,mode;} note_fx_generator_runtime_t;
typedef struct {uint8_t held_count;} note_fx_held_bank_runtime_t;

static const uint8_t g_harmony[NOTE_FX_VOICER_TYPE_COUNT][4]={
 {0,4,7,255},{0,3,7,255},{0,5,7,255},{0,2,7,255},
 {0,4,7,10},{0,4,7,11},{0,3,6,10},{0,4,8,255}};
_Static_assert(sizeof(note_fx_generator_runtime_t)==4U,"Note FX generator runtime budget");
_Static_assert(sizeof(note_fx_held_pitch_t)==24U,"Note FX held-state budget");
_Static_assert(sizeof(g_harmony)==32U,"Harmony table proof");

typedef struct { note_fx_generator_runtime_t generator[NOTE_FX_TRACK_COUNT];
 uint64_t work_track_mask;uint32_t token,samples_per_step_q16;
 uint64_t transport_position_q16,block_start;uint32_t pattern_position_q16[NOTE_FX_TRACK_COUNT];
 uint8_t pattern_length[NOTE_FX_TRACK_COUNT];
} note_fx_engine_context_t;
static CONTROL_M4_SRAM2 note_fx_engine_context_t g_seq_context;
static CONTROL_M4_SRAM2 note_fx_held_pitch_t
    g_held[2][SEQ_PRODUCT_HELD_STATE_CAPACITY];
static CONTROL_M4_SRAM2 uint64_t
    g_held_active_mask[2][SEQ_PRODUCT_HARMONY_FANOUT_MAX];
static CONTROL_M4_SRAM2 note_fx_held_bank_runtime_t
    g_family[NOTE_FX_TRACK_COUNT][2];
static SEQ_STATE_D2 uint64_t
    g_generated_until[NOTE_FX_TRACK_COUNT];
typedef struct {uint8_t phase,climb_octave,walk_phase;} note_fx_chain_runtime_t;
static SEQ_STATE_SDRAM note_fx_chain_state_t
    g_chain[NOTE_FX_TRACK_COUNT];
static SEQ_STATE_D2 note_fx_chain_runtime_t
    g_chain_runtime[NOTE_FX_TRACK_COUNT];
static note_fx_engine_context_t *const g_context=&g_seq_context;
#define g_generator (g_context->generator)
#define g_work_track_mask (g_context->work_track_mask)
#define g_token (g_context->token)
#define g_samples_per_step_q16 (g_context->samples_per_step_q16)
static uint64_t track_bit(uint8_t t){return UINT64_C(1)<<t;}
static uint8_t mode_is_arp(uint8_t mode){return(uint8_t)(mode==NOTE_FX_GENERATOR_ARP||mode==NOTE_FX_GENERATOR_HOLD);}
static uint8_t mode_is_generator(uint8_t mode){return(mode!=NOTE_FX_GENERATOR_OFF)?1U:0U;}
static uint8_t held_family(uint8_t mode){return(mode==NOTE_FX_GENERATOR_EUCLID)?1U:0U;}
static uint8_t held_has_deadline(const note_fx_held_pitch_t*h){return(uint8_t)(h->lifetime_end!=0U);}
static uint8_t held_is_active_at(const note_fx_held_pitch_t*h,uint64_t sample){return(uint8_t)(h->source_token!=0U&&((int32_t)((uint32_t)sample-h->lifetime_start)>=0)&&(!held_has_deadline(h)||((int32_t)((uint32_t)sample-h->lifetime_end)<0)));}
static uint8_t held_is_active_for(const note_fx_generator_runtime_t*r,const note_fx_held_pitch_t*h,uint64_t sample){if(r->mode==NOTE_FX_GENERATOR_HOLD)return(uint8_t)(h->source_token!=0U&&((int32_t)((uint32_t)sample-h->lifetime_start)>=0));return held_is_active_at(h,sample);}
static uint8_t held_deadline_reached(const note_fx_held_pitch_t*h,uint64_t sample){return(uint8_t)(held_has_deadline(h)&&((int32_t)((uint32_t)sample-h->lifetime_end)>=0));}
static uint32_t held_remaining(const note_fx_held_pitch_t*h,uint64_t sample,uint32_t fallback){if(!held_has_deadline(h))return fallback;const uint32_t remaining=h->lifetime_end-(uint32_t)sample;return remaining<fallback?remaining:fallback;}
static note_fx_held_bank_runtime_t*family_state(uint8_t t)
{return &g_family[t][held_family(g_generator[t].mode)];}
static void refresh_work(uint8_t t){const uint64_t b=track_bit(t);if(mode_is_generator(g_generator[t].mode)&&family_state(t)->held_count)g_work_track_mask|=b;else g_work_track_mask&=~b;}
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
static void held_remove(uint8_t family,note_fx_held_bank_runtime_t*f,uint16_t lane,uint8_t branch){note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(!h->source_token)return;memset(h,0,sizeof(*h));g_held_active_mask[family][branch]&=~(UINT64_C(1)<<lane);if(f->held_count)--f->held_count;}
static uint64_t held_active_lanes(uint8_t family,uint64_t lanes){uint64_t active=0U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)active|=g_held_active_mask[family][branch];return active&lanes;}
static uint8_t arp_has_physical_held(uint8_t t,uint8_t family,uint64_t sample){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;const note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(!h->released&&held_is_active_at(h,sample))return 1U;}}return 0U;}
static void arp_begin_capture(uint8_t t,uint8_t family,uint64_t sample){note_fx_held_bank_runtime_t*f=&g_family[t][family];if(arp_has_physical_held(t,family,sample))return;const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;held_remove(family,f,lane,branch);}}}
static note_event_result_t held_ingest(note_fx_generator_runtime_t*r,const note_event_t*e){const uint16_t lane=product_lane(e);const uint8_t branch=note_event_branch(e);const uint8_t family=held_family(r->mode);note_fx_held_bank_runtime_t*f=family_state(e->track);if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES||branch>=SEQ_PRODUCT_HARMONY_FANOUT_MAX)return NOTE_EVENT_RESULT_REJECTED_DESTINATION;const uint8_t arp_hold=(r->mode==NOTE_FX_GENERATOR_HOLD)?1U:0U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(e->kind==NOTE_EVENT_KIND_OFF){if(h->source_token==e->source_id&&h->generation==e->source_generation){if(arp_hold)h->released=1U;else held_remove(family,f,lane,branch);}return NOTE_EVENT_RESULT_ACCEPTED;}if(e->duration_samples==0U)return NOTE_EVENT_RESULT_DROPPED_POLICY;if(arp_hold){arp_begin_capture(e->track,family,e->sample_abs);h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];}if(!h->source_token)++f->held_count;const uint32_t lifetime_end=((e->flags&NOTE_EVENT_FLAG_HELD)!=0U)?0U:(uint32_t)(e->sample_abs+e->duration_samples);*h=(note_fx_held_pitch_t){.source_token=e->source_id,.generation=e->source_generation,.group_id=e->group_id,.lifetime_start=(uint32_t)e->sample_abs,.lifetime_end=lifetime_end,.note=e->note,.velocity=e->velocity,.released=0U};g_held_active_mask[family][branch]|=UINT64_C(1)<<lane;return NOTE_EVENT_RESULT_ACCEPTED;}
static void held_expire(uint8_t t,uint64_t sample){note_fx_generator_runtime_t*r=&g_generator[t];const uint8_t family=held_family(r->mode);note_fx_held_bank_runtime_t*f=family_state(t);const uint8_t arp_hold=(r->mode==NOTE_FX_GENERATOR_HOLD)?1U:0U;const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(held_deadline_reached(h,sample)){if(arp_hold)h->released=1U;else held_remove(family,f,lane,branch);}}}}
static uint8_t held_count_at(uint8_t t,note_fx_generator_runtime_t*r,uint64_t sample){uint8_t count=0U;const uint8_t family=held_family(r->mode);uint64_t active=held_active_lanes(family,track_lane_mask(t));while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))!=0U&&held_is_active_for(r,&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],sample))++count;}return count;}
static note_fx_held_pitch_t*held_rank(uint8_t t,note_fx_generator_runtime_t*r,uint8_t rank,uint8_t sorted,uint64_t sample){note_fx_held_pitch_t*items[SEQ_LOGICAL_CAPACITY_MAX*SEQ_PRODUCT_HARMONY_FANOUT_MAX];uint8_t count=0;const uint8_t family=held_family(r->mode);uint64_t active=held_active_lanes(family,track_lane_mask(t));while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))!=0U&&held_is_active_for(r,&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],sample))items[count++]=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];}if(rank>=count)return NULL;if(sorted)for(uint8_t i=1;i<count;++i){note_fx_held_pitch_t*x=items[i];uint8_t j=i;while(j&&items[j-1U]->note>x->note){items[j]=items[j-1U];--j;}items[j]=x;}return items[rank];}
static uint8_t arp_select(note_fx_generator_runtime_t*r,uint32_t step,uint8_t track,note_fx_held_pitch_t**oh,uint8_t*on,uint64_t sample){const uint8_t held_count=held_count_at(track,r,sample);if(!held_count||!oh||!on)return 0;const note_fx_arp_style_t style=(note_fx_arp_style_t)r->p1;uint8_t rank;if(style==NOTE_FX_ARP_RANDOM){rank=(uint8_t)(mix32(step^((uint32_t)track<<24)^UINT32_C(0x9E3779B9))%held_count);}else if(style==NOTE_FX_ARP_UP_DOWN&&held_count>1U){const uint8_t cycle=(uint8_t)(2U*held_count-2U),pos=(uint8_t)(step%cycle);rank=pos<held_count?pos:(uint8_t)(cycle-pos);}else{rank=(uint8_t)(step%held_count);if(style==NOTE_FX_ARP_DOWN)rank=(uint8_t)(held_count-1U-rank);}note_fx_held_pitch_t*h=held_rank(track,r,rank,(uint8_t)(style!=NOTE_FX_ARP_ORDER),sample);if(!h)return 0;const uint8_t range=r->p3>=1&&r->p3<=4?r->p3:1;const uint32_t cycle=style==NOTE_FX_ARP_UP_DOWN&&held_count>1?(uint32_t)(2U*held_count-2U):held_count;const uint16_t raised=(uint16_t)h->note+(uint16_t)(12U*((step/cycle)%range));*on=raised<128U?(uint8_t)raised:h->note;*oh=h;return 1;}
static note_event_result_t emit_generated(uint8_t t,const note_fx_held_pitch_t*h,uint8_t n,uint64_t sample,uint64_t duration,note_fx_emit_fn emit,void*ctx)
{
 if(!emit)return NOTE_EVENT_RESULT_ACCEPTED;
 const uint32_t token=next_token();const uint32_t fallback=duration>UINT32_MAX?UINT32_MAX:(uint32_t)duration;const uint32_t bounded_duration=(g_generator[t].mode==NOTE_FX_GENERATOR_HOLD)?fallback:held_remaining(h,sample,fallback);const note_fx_held_pitch_t*base=&g_held[held_family(g_generator[t].mode)][0];const uint16_t held_index=(uint16_t)(h-base);const uint16_t lane=(uint16_t)(held_index/SEQ_PRODUCT_HARMONY_FANOUT_MAX);const uint8_t branch=(uint8_t)(held_index%SEQ_PRODUCT_HARMONY_FANOUT_MAX);uint32_t group=mix32(h->group_id^(uint32_t)sample^(uint32_t)(sample>>32U));if(!group)group=1U;note_event_t e={.sample_abs=sample,.duration_samples=bounded_duration?bounded_duration:1U,.source_id=h->source_token,.occurrence_id=token,.source_generation=h->generation,.group_id=group,.track=t,.note=n,.velocity=h->velocity,.kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_FX,.stage=1U,.flags=NOTE_EVENT_FLAG_GENERATED,.temporal_index=lane_temporal(t,lane),.branch=branch,.timing_class=NOTE_EVENT_TIMING_SCHEDULED};
 return emit(&e,ctx);
}
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
static uint32_t event_pattern_position_q16(const note_event_t*e){const uint64_t loop=(uint64_t)(g_context->pattern_length[e->track]?g_context->pattern_length[e->track]:1U)<<16U;const uint64_t now=event_transport_position_q16(e);const int64_t delta=(now>=g_context->transport_position_q16)?(int64_t)(now-g_context->transport_position_q16):-(int64_t)(g_context->transport_position_q16-now);int64_t position=(int64_t)g_context->pattern_position_q16[e->track]+delta;position%=(int64_t)loop;if(position<0)position+=(int64_t)loop;return(uint32_t)position;}

void note_fx_engine_init(void){memset(&g_seq_context,0,sizeof(g_seq_context));memset(g_held,0,sizeof(g_held));memset(g_held_active_mask,0,sizeof(g_held_active_mask));memset(g_family,0,sizeof(g_family));memset(g_generated_until,0,sizeof(g_generated_until));memset(g_chain,0,sizeof(g_chain));memset(g_chain_runtime,0,sizeof(g_chain_runtime));g_seq_context.samples_per_step_q16=UINT32_C(65536);}
void note_fx_engine_set_samples_per_step_q16(uint32_t v){g_samples_per_step_q16=v?v:1U;}
void note_fx_engine_set_time_reference(uint64_t sample,uint64_t transport,uint32_t sps){g_context->block_start=sample;g_context->transport_position_q16=transport;note_fx_engine_set_samples_per_step_q16(sps);}
static void held_clear_family(uint8_t t,uint8_t family){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;memset(&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],0,sizeof(note_fx_held_pitch_t));}g_held_active_mask[family][branch]&=~lanes;}g_family[t][family].held_count=0U;}
static void held_release_latched(uint8_t t,uint8_t family,uint64_t sample){const uint64_t lanes=track_lane_mask(t);for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t active=g_held_active_mask[family][branch]&lanes;while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);active&=active-1U;note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(h->released||held_deadline_reached(h,sample))held_remove(family,&g_family[t][family],lane,branch);}}}
static note_event_result_t generator_configure(uint8_t t,const note_fx_generator_state_t*state){if(t>=NOTE_FX_TRACK_COUNT||state==NULL)return NOTE_EVENT_RESULT_DROPPED_POLICY;note_fx_generator_runtime_t*r=&g_generator[t];const uint8_t old_mode=r->mode;if(old_mode!=state->mode){if(mode_is_generator(old_mode))held_clear_family(t,held_family(old_mode));if(mode_is_generator(state->mode))held_clear_family(t,held_family(state->mode));}r->p1=state->p1;r->p2=state->p2;r->p3=state->p3;r->mode=state->mode;if(old_mode==NOTE_FX_GENERATOR_HOLD&&state->mode==NOTE_FX_GENERATOR_ARP)held_release_latched(t,0U,g_context->block_start);refresh_work(t);return NOTE_EVENT_RESULT_ACCEPTED;}
static note_event_result_t generator_ingest(const note_event_t*in,uint8_t n)
{
 if(!in||!n||in[0].track>=NOTE_FX_TRACK_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;
 note_fx_generator_runtime_t*r=&g_generator[in[0].track];
 if(!mode_is_generator(r->mode))return NOTE_EVENT_RESULT_DROPPED_POLICY;
 for(uint8_t i=0U;i<n;++i){
  if(!note_event_is_valid(&in[i])||in[i].track!=in[0].track)
   return NOTE_EVENT_RESULT_DROPPED_POLICY;
  const note_event_result_t result=held_ingest(r,&in[i]);
  if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}
 refresh_work(in[0].track);
 return NOTE_EVENT_RESULT_ACCEPTED;
}
static note_event_result_t note_fx_engine_process_due(uint8_t track,uint64_t start,
    uint32_t horizon_samples,uint32_t sps,note_fx_emit_fn emit,void*ctx)
{
 if(track>=NOTE_FX_TRACK_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;
 note_fx_engine_set_samples_per_step_q16(sps);
 const uint64_t end=start+horizon_samples;
 if((g_work_track_mask&track_bit(track))==0U)return NOTE_EVENT_RESULT_ACCEPTED;
 const uint8_t t=track;
 note_fx_generator_runtime_t*r=&g_generator[t];
 note_fx_held_bank_runtime_t*f=family_state(t);
 uint64_t from=g_generated_until[t];if(from<start)from=start;
 held_expire(t,from);refresh_work(t);
 if(!f->held_count||from>=end)return NOTE_EVENT_RESULT_ACCEPTED;
   const uint8_t division=r->mode==NOTE_FX_GENERATOR_EUCLID?r->p3:r->p2;
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
   held_expire(t,sample);if(!f->held_count){covered_until=sample;break;}
   if(mode_is_arp(r->mode)){
    note_fx_held_pitch_t*h=NULL;uint8_t note=0U;
    if(arp_select(r,(uint32_t)ordinal,t,&h,&note,sample)){
     const note_event_result_t result=emit_generated(t,h,note,sample,
         period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}}
   else{const uint8_t length=r->p1?r->p1:NOTE_FX_EUCLID_LENGTH_DEFAULT;
    const uint8_t pulse=r->p2<=length?r->p2:length;
    const uint64_t mask=euclid_build_mask(length,pulse);
     const uint8_t pattern_step=(uint8_t)(ordinal%length);
     if((mask>>pattern_step)&UINT64_C(1)){
     const uint8_t family=held_family(r->mode);
     uint64_t active=held_active_lanes(family,track_lane_mask(t));
     while(active){const uint16_t lane=(uint16_t)__builtin_ctzll(active);
      active&=active-1U;
      for(uint8_t branch=0U;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){
       if((g_held_active_mask[family][branch]&(UINT64_C(1)<<lane))==0U)continue;
       const note_fx_held_pitch_t*h=&g_held[family]
           [lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];
       if(!held_is_active_at(h,sample))continue;
       const note_event_result_t result=emit_generated(t,h,h->note,sample,
           period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;}}}}
   ++ordinal;
   if(policy==NOTE_FX_PHASE_PATTERN_SYNC){
    uint64_t target_phase=ordinal*period_q16;
    if(target_phase>=loop_q16){++loop;ordinal=0U;target_phase=0U;}
    target_position=loop*loop_q16+target_phase;
   }else target_position=ordinal*period_q16;
   sample=position_sample(start,reference_position,target_position,sps);}
  g_generated_until[t]=covered_until;refresh_work(t);
 return NOTE_EVENT_RESULT_ACCEPTED;
}
static note_event_result_t note_fx_engine_process(uint8_t track,uint64_t start,uint32_t horizon_samples,uint32_t sps,uint64_t transport,const uint32_t pattern[NOTE_FX_TRACK_COUNT],const uint8_t pattern_length[NOTE_FX_TRACK_COUNT],note_fx_emit_fn emit,void*ctx){note_fx_engine_set_time_reference(start,transport,sps);memcpy(g_seq_context.pattern_position_q16,pattern,sizeof(g_seq_context.pattern_position_q16));memcpy(g_seq_context.pattern_length,pattern_length,sizeof(g_seq_context.pattern_length));return note_fx_engine_process_due(track,start,horizon_samples,sps,emit,ctx);}

static uint8_t chain_voice_count(uint8_t mode)
{
 if(mode>=NOTE_FX_VOICER_MODE_1&&mode<=NOTE_FX_VOICER_MODE_4)
  return mode;
 if(mode>=NOTE_FX_VOICER_MODE_SEQ2&&mode<=NOTE_FX_VOICER_MODE_SEQ4)
  return(uint8_t)(mode-NOTE_FX_VOICER_MODE_SEQ2+2U);
 if(mode>=NOTE_FX_VOICER_MODE_UPDN2&&mode<=NOTE_FX_VOICER_MODE_UPDN4)
  return(uint8_t)(mode-NOTE_FX_VOICER_MODE_UPDN2+2U);
 if(mode>=NOTE_FX_VOICER_MODE_CLIMB2&&mode<=NOTE_FX_VOICER_MODE_CLIMB4)
  return(uint8_t)(mode-NOTE_FX_VOICER_MODE_CLIMB2+2U);
 return 0U;
}

static uint8_t chain_mode_is_poly(uint8_t mode)
{return(uint8_t)(mode>=NOTE_FX_VOICER_MODE_1&&mode<=NOTE_FX_VOICER_MODE_4);}
static uint8_t chain_mode_is_updn(uint8_t mode)
{return(uint8_t)(mode>=NOTE_FX_VOICER_MODE_UPDN2&&mode<=NOTE_FX_VOICER_MODE_UPDN4);}
static uint8_t chain_mode_is_climb(uint8_t mode)
{return(uint8_t)(mode>=NOTE_FX_VOICER_MODE_CLIMB2&&mode<=NOTE_FX_VOICER_MODE_CLIMB4);}

static uint8_t chain_selected_voice(uint8_t mode,uint8_t voices,uint8_t phase)
{
 if(chain_mode_is_updn(mode)!=0U){const uint8_t period=(uint8_t)(2U*voices-2U);
  const uint8_t p=(uint8_t)(phase%period);return(p<voices)?p:(uint8_t)(period-p);}
 return(uint8_t)(phase%voices);
}

static uint8_t chain_scale_id(uint8_t scale)
{
 static const uint8_t ids[NOTE_FX_SCALER_SCALE_COUNT]={
  KBD_SCALE_ID_CHROMATIC,KBD_SCALE_ID_CHROMATIC,KBD_SCALE_ID_MAJOR,
  KBD_SCALE_ID_NAT_MINOR,KBD_SCALE_ID_DORIAN,KBD_SCALE_ID_MIXOLYDIAN,
  KBD_SCALE_ID_PENT_MAJOR,KBD_SCALE_ID_PENT_MINOR};
 return ids[(scale<NOTE_FX_SCALER_SCALE_COUNT)?scale:NOTE_FX_SCALER_SCALE_OFF];
}

static uint8_t chain_scale_contains(const note_fx_scaler_state_t*s,int16_t note)
{
 if(note<0||note>127)return 0U;
 const uint8_t root=(uint8_t)(s->key%12U);
 const uint8_t relative=(uint8_t)(((uint8_t)note+12U-root)%12U);
 return kbd_scale_contains_pitch_class(chain_scale_id(s->scale),relative)?1U:0U;
}

static uint8_t chain_scaler_apply(uint8_t track,const note_fx_scaler_state_t*s,
 uint8_t input,uint8_t*out)
{
 int16_t note=(int16_t)input+(int16_t)s->transpose-12;
 if(note<0)note=0;
 if(note>127)note=127;
 if(s->scale==NOTE_FX_SCALER_SCALE_OFF){*out=input;return 1U;}
 if(chain_scale_contains(s,note)!=0U){*out=(uint8_t)note;return 1U;}
 if(s->stick==NOTE_FX_SCALER_STICK_FIXED_DROP)return 0U;
 if(s->stick==NOTE_FX_SCALER_STICK_NEAREST){for(uint8_t d=1U;d<12U;++d){
   const int16_t down=note-d,up=note+d;
   if(chain_scale_contains(s,down)!=0U){*out=(uint8_t)down;return 1U;}
   if(chain_scale_contains(s,up)!=0U){*out=(uint8_t)up;return 1U;}}return 0U;}
 int8_t direction=(s->stick==NOTE_FX_SCALER_STICK_FIXED_UP)?1:-1;
 if(s->stick==NOTE_FX_SCALER_STICK_WALK)
  direction=(g_chain_runtime[track].walk_phase!=0U)?1:-1;
 for(uint8_t pass=0U;pass<2U;++pass){for(uint8_t d=1U;d<12U;++d){
   const int16_t candidate=note+(int16_t)direction*d;
   if(chain_scale_contains(s,candidate)!=0U){*out=(uint8_t)candidate;
    if(s->stick==NOTE_FX_SCALER_STICK_WALK)
     g_chain_runtime[track].walk_phase^=1U;
    return 1U;}}
  if(s->stick!=NOTE_FX_SCALER_STICK_WALK)break;
  direction=(int8_t)-direction;}
 return 0U;
}

static uint8_t chain_probability_pass(const note_fx_trig_state_t*t,
 const note_event_t*e)
{
 if(e->kind==NOTE_EVENT_KIND_OFF)return 1U;
 const uint8_t chance=(t->chance==NOTE_FX_TRIG_CHANCE_OFF)?100U:
  (uint8_t)(101U-t->chance);
 const uint64_t transport=event_transport_position_q16(e);
 uint64_t identity=e->group_id;
 uint64_t cycle=transport/(UINT64_C(16)<<16U);
 if(t->keep==NOTE_FX_TRIG_KEEP_LOOP){const uint8_t track=e->track;
  const uint64_t loop=(uint64_t)(g_context->pattern_length[track]
   ?g_context->pattern_length[track]:1U)<<16U;
  const uint64_t relative=event_pattern_position_q16(e)%loop;
  identity=(t->lot>=NOTE_FX_TRIG_LOT_DIVISION_BASE)
   ?probability_lot_index(relative,
      (uint8_t)(t->lot-NOTE_FX_TRIG_LOT_DIVISION_BASE))
   :relative^((uint64_t)e->temporal_index<<48U);cycle=0U;}
 else if(t->lot>=NOTE_FX_TRIG_LOT_DIVISION_BASE)
  identity=probability_lot_index(transport,
   (uint8_t)(t->lot-NOTE_FX_TRIG_LOT_DIVISION_BASE));
 if(t->lot==NOTE_FX_TRIG_LOT_POLY)
  identity^=(uint64_t)(e->branch+1U)*UINT64_C(0x9E3779B97F4A7C15);
 const uint32_t draw=mix32((uint32_t)identity^(uint32_t)(identity>>32)
  ^((uint32_t)e->track<<24)^(uint32_t)cycle)%100U;
 uint8_t pass=(uint8_t)(chance>=100U||(chance!=0U&&draw<chance));
 if(t->keep>=NOTE_FX_TRIG_KEEP_DIVISION_BASE
      &&probability_keep_match(transport,
       (uint8_t)(t->keep-NOTE_FX_TRIG_KEEP_DIVISION_BASE))!=0U)pass=1U;
 return pass;
}

static note_event_result_t chain_suffix(const note_event_t*input,uint8_t input_count,
 note_event_t*output,uint8_t cap,uint8_t*out_count)
{
 if(!input||!input_count||!output||!out_count||input[0].track>=NOTE_FX_TRACK_COUNT)
  return NOTE_EVENT_RESULT_DROPPED_POLICY;
 const uint8_t track=input[0].track;const note_fx_chain_state_t*c=&g_chain[track];
 note_fx_chain_runtime_t*rt=&g_chain_runtime[track];uint8_t count=0U;
 for(uint8_t i=0U;i<input_count;++i){const note_event_t*source=&input[i];
  if(source->track!=track||!note_event_is_valid(source))
   return NOTE_EVENT_RESULT_DROPPED_POLICY;
  if(source->kind==NOTE_EVENT_KIND_OFF){
   for(uint8_t voice=0U;voice<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++voice){
    if(count>=cap)return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
    note_event_t x=*source;x.branch=voice;
    if(voice!=0U){x.occurrence_id=child_id(source->occurrence_id,1U,voice,0U);
     x.provenance=NOTE_EVENT_SOURCE_FX;x.flags|=NOTE_EVENT_FLAG_GENERATED;}
    output[count++]=x;}
   continue;}
  const uint8_t voices=chain_voice_count(c->voicer.mode);
  const uint8_t phase=rt->phase;uint8_t first=0U,last=0U;
  if(c->voicer.mode==NOTE_FX_VOICER_MODE_OFF){first=0U;last=0U;}
  else if(chain_mode_is_poly(c->voicer.mode)!=0U){first=0U;last=(uint8_t)(voices-1U);}
  else{first=chain_selected_voice(c->voicer.mode,voices,phase);last=first;}
  const uint8_t invert=(c->voicer.invert==NOTE_FX_VOICER_INVERT_AUTO)
   ?(uint8_t)(phase%4U):c->voicer.invert;
  const uint8_t spread=(c->voicer.spread==NOTE_FX_VOICER_SPREAD_ALT)
   ?(uint8_t)(phase&1U):c->voicer.spread;
  for(uint8_t voice=first;voice<=last;++voice){uint8_t interval=0U;
   if(c->voicer.mode!=NOTE_FX_VOICER_MODE_OFF){interval=
    g_harmony[c->voicer.type%NOTE_FX_VOICER_TYPE_COUNT][voice];
    if(interval==255U)continue;
    if(voice<invert)interval=(uint8_t)(interval+12U);
    if(voice!=0U)interval=(uint8_t)(interval+12U*spread*voice);}
   uint16_t raised=(uint16_t)source->note+interval;
   if(chain_mode_is_climb(c->voicer.mode)!=0U){if(first==0U){uint8_t top=0U;
     for(uint8_t v=0U;v<voices;++v){uint8_t x=
      g_harmony[c->voicer.type%NOTE_FX_VOICER_TYPE_COUNT][v];
      if(x==255U)continue;
      if(v<invert)x=(uint8_t)(x+12U);
      if(v!=0U)x=(uint8_t)(x+12U*spread*v);
      if(x>top)top=x;}if((uint16_t)source->note+top
       +(uint16_t)(12U*rt->climb_octave)>127U)rt->climb_octave=0U;}
    raised=(uint16_t)(raised+12U*rt->climb_octave);while(raised>127U&&raised>=12U)raised-=12U;}
   if(raised>=128U)continue;
   note_event_t x=*source;x.note=(uint8_t)raised;x.branch=voice;
   if(voice!=0U&&chain_mode_is_poly(c->voicer.mode)!=0U){x.occurrence_id=child_id(source->occurrence_id,1U,voice,0U);
    x.provenance=NOTE_EVENT_SOURCE_FX;x.flags|=NOTE_EVENT_FLAG_GENERATED;}
   if(c->scaler.scale!=NOTE_FX_SCALER_SCALE_OFF
        &&chain_scaler_apply(track,&c->scaler,x.note,&x.note)==0U)continue;
   if(c->trig.chance!=NOTE_FX_TRIG_CHANCE_OFF){if(!chain_probability_pass(&c->trig,&x))continue;
    if(x.kind!=NOTE_EVENT_KIND_OFF){x.flags=(uint8_t)(x.flags&~NOTE_EVENT_FLAG_HELD);
     x.duration_samples=(uint32_t)(((uint64_t)c->trig.gate*step_samples()+50U)/100U);
     if(x.duration_samples==0U)x.duration_samples=1U;}}
   if(c->voicer.mode==NOTE_FX_VOICER_MODE_OFF)x.branch=source->branch;
   if(count>=cap)return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
   output[count++]=x;}
  if(source->kind==NOTE_EVENT_KIND_ON&&c->voicer.mode!=NOTE_FX_VOICER_MODE_OFF){if(chain_mode_is_climb(c->voicer.mode)!=0U
       &&voices!=0U&&first==(uint8_t)(voices-1U))++rt->climb_octave;
   rt->phase=(uint8_t)((rt->phase+1U)%12U);}}
 *out_count=count;return NOTE_EVENT_RESULT_ACCEPTED;
}

note_event_result_t note_fx_chain_engine_configure(uint8_t track,
 const note_fx_chain_state_t*effective)
{
 if(track>=NOTE_FX_TRACK_COUNT||effective==NULL)return NOTE_EVENT_RESULT_DROPPED_POLICY;
 const note_fx_chain_state_t previous=g_chain[track];
 g_chain[track]=*effective;
 if(previous.voicer.mode==NOTE_FX_VOICER_MODE_OFF
      &&effective->voicer.mode!=NOTE_FX_VOICER_MODE_OFF){
  g_chain_runtime[track].phase=0U;g_chain_runtime[track].climb_octave=0U;}
 if(previous.scaler.scale==NOTE_FX_SCALER_SCALE_OFF
      &&effective->scaler.scale!=NOTE_FX_SCALER_SCALE_OFF)
  g_chain_runtime[track].walk_phase=0U;
 return generator_configure(track,&effective->generator);
}

note_event_result_t note_fx_chain_engine_transform(const note_event_t*input,
 uint8_t input_count,note_event_t*output,uint8_t output_capacity,uint8_t*output_count)
{
 if(!input||!input_count||input[0].track>=NOTE_FX_TRACK_COUNT||!output_count)
  return NOTE_EVENT_RESULT_DROPPED_POLICY;
 const uint8_t track=input[0].track;if(g_chain[track].generator.mode==NOTE_FX_GENERATOR_OFF)
  return chain_suffix(input,input_count,output,output_capacity,output_count);
 if(input_count>NOTE_FX_BATCH_CAPACITY)return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
 *output_count=0U;
 (void)output;(void)output_capacity;
 return generator_ingest(input,input_count);
}

typedef struct {note_fx_emit_fn emit;void*context;note_event_t scratch[SEQ_PRODUCT_HARMONY_FANOUT_MAX];}
 chain_emit_context_t;
static note_event_result_t chain_generated(const note_event_t*event,void*context)
{chain_emit_context_t*c=context;uint8_t count=0U;const note_event_result_t r=
 chain_suffix(event,1U,c->scratch,SEQ_PRODUCT_HARMONY_FANOUT_MAX,&count);
 if(r!=NOTE_EVENT_RESULT_ACCEPTED)return r;
 for(uint8_t i=0U;i<count;++i){const
 note_event_result_t e=c->emit(&c->scratch[i],c->context);if(e!=NOTE_EVENT_RESULT_ACCEPTED)return e;}
 return NOTE_EVENT_RESULT_ACCEPTED;}

note_event_result_t note_fx_chain_engine_process(uint8_t track,uint64_t start,
 uint32_t horizon,uint32_t sps,uint64_t transport,const uint32_t pattern[NOTE_FX_TRACK_COUNT],
 const uint8_t pattern_length[NOTE_FX_TRACK_COUNT],
 note_fx_emit_fn emit,void*context)
{if(emit==NULL)return NOTE_EVENT_RESULT_DROPPED_POLICY;chain_emit_context_t c={.emit=emit,
 .context=context};return note_fx_engine_process(track,start,horizon,sps,transport,
 pattern,pattern_length,chain_generated,&c);}
