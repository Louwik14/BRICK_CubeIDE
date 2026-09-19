#include "NoteFx/note_fx_engine.h"
#include "Seq/seq_boundary_probe.h"
#include "NoteFx/note_fx_walker_probe.h"
#include <string.h>
#include "NoteFx/note_fx_euclid.h"
#include "NoteFx/note_fx_context.h"
#include "Seq/seq_division_catalog.h"
#include "Seq/seq_capacity_contract.h"
#include "Platform/memory_layout.h"

#define SEQ_WALKER_DIAG_MAGIC UINT32_C(0x53575032)
#define SEQ_WALKER_DIAG_VERSION 1U
note_fx_walker_stat_t g_seq_walker_probe[NOTE_FX_WALKER_CATEGORY_COUNT];
CTRL_STATE __attribute__((used)) volatile uint32_t g_seq_walker_diag[158];
void note_fx_walker_probe_reset(void)
{memset(g_seq_walker_probe,0,sizeof(g_seq_walker_probe));
 memset((void*)g_seq_walker_diag,0,sizeof(g_seq_walker_diag));}
void note_fx_walker_probe_publish(void)
{uint32_t*w=(uint32_t*)g_seq_walker_diag;w[0]=SEQ_WALKER_DIAG_MAGIC;
 w[1]=SEQ_WALKER_DIAG_VERSION;w[2]=NOTE_FX_WALKER_CATEGORY_COUNT;w[3]=0U;
 for(uint8_t i=0U;i<NOTE_FX_WALKER_CATEGORY_COUNT;++i){const uint32_t o=4U+11U*i;
  const note_fx_walker_stat_t*s=&g_seq_walker_probe[i];w[o]=(uint32_t)s->cycles;
  w[o+1U]=(uint32_t)(s->cycles>>32U);w[o+2U]=s->max_cycles;w[o+3U]=s->calls;
  w[o+4U]=s->calls?(uint32_t)(s->cycles/s->calls):0U;w[o+5U]=s->events_in;
  w[o+6U]=s->events_out;w[o+7U]=s->branches;w[o+8U]=s->repeats;
  w[o+9U]=s->held;w[o+10U]=s->emissions;}}

typedef struct {uint32_t source_token,generation,group_id,lifetime_end;
 uint8_t note,velocity,dependency_mask,reserved;} note_fx_held_pitch_t;
typedef struct {uint8_t model,p1,p2,p3;} note_fx_slot_runtime_t;
typedef struct {uint8_t owner_slot,held_count;} note_fx_family_runtime_t;
typedef struct { int8_t timing[8],velocity[8]; } note_fx_groove_template_t;
typedef struct {uint64_t next_due;uint32_t delay,duration,source_id,occurrence_id,
 generation,group_id;uint8_t note,velocity,stage,flags,provenance,dependency_mask,
 repeats,index,decay,active;uint8_t reserved[2];} note_fx_echo_state_t;

static const note_fx_groove_template_t g_groove[NOTE_FX_GROOVE_TYPE_COUNT]={
 {{0,24,0,24,0,24,0,24},{12,-8,8,-8,12,-8,8,-8}},
 {{0,32,0,32,0,32,0,32},{10,-12,6,-12,10,-12,6,-12}},
 {{0,12,-5,18,0,10,-4,16},{16,-10,5,-7,13,-9,4,-6}},
 {{0,18,-8,10,4,22,-6,12},{18,-14,8,-5,12,-12,7,-4}}};
static const uint8_t g_harmony[NOTE_FX_HARMONIZER_TYPE_COUNT][4]={
 {0,4,7,255},{0,3,7,255},{0,5,7,255},{0,2,7,255},
 {0,4,7,10},{0,4,7,11},{0,3,6,10},{0,4,8,255}};
static const uint8_t g_scale_count[7]={7,7,7,7,5,5,12};
static const uint8_t g_scale[7][12]={{0,2,4,5,7,9,11},{0,2,3,5,7,8,10},
 {0,2,3,5,7,9,10},{0,2,4,5,7,9,10},{0,2,4,7,9},{0,3,5,7,10},
 {0,1,2,3,4,5,6,7,8,9,10,11}};
_Static_assert(sizeof(note_fx_slot_runtime_t)<=224U,"Note FX slot runtime budget");
_Static_assert(sizeof(note_fx_held_pitch_t)==20U,"Note FX held-state budget");
_Static_assert(sizeof(note_fx_echo_state_t)==48U,"Note FX Echo-state budget");
_Static_assert(sizeof(g_groove)==64U,"Groove table proof");
_Static_assert(sizeof(g_harmony)==32U,"Harmony table proof");

typedef struct { note_fx_slot_runtime_t slot[NOTE_FX_TRACK_COUNT][NOTE_FX_SLOT_COUNT];
 uint64_t work_slot_mask;uint32_t token,samples_per_step_q16;
 uint64_t transport_position_q16,block_start;uint32_t pattern_position_q16[NOTE_FX_TRACK_COUNT];
 uint8_t scale_index,root_index;
} note_fx_engine_context_t;
static CONTROL_M4_SRAM2 note_fx_engine_context_t g_seq_context;
static SEQ_HOT_D1 note_fx_echo_state_t
    g_echo[SEQ_PRODUCT_MAX_EMITTING_VOICES][SEQ_PRODUCT_HARMONY_FANOUT_MAX];
static uint64_t g_echo_active[SEQ_PRODUCT_HARMONY_FANOUT_MAX];
static CONTROL_M4_SRAM2 note_fx_held_pitch_t
    g_held[2][SEQ_PRODUCT_HELD_STATE_CAPACITY];
static CONTROL_M4_SRAM2 note_fx_family_runtime_t
    g_family[NOTE_FX_TRACK_COUNT][2];
static SEQ_HOT_D1 note_fx_echo_diag_t g_echo_diag;
static note_fx_engine_context_t *const g_context=&g_seq_context;
#define g_slot (g_context->slot)
#define g_work_slot_mask (g_context->work_slot_mask)
#define g_token (g_context->token)
#define g_samples_per_step_q16 (g_context->samples_per_step_q16)
static uint64_t slot_bit(uint8_t t,uint8_t s){return UINT64_C(1)<<((uint32_t)t*NOTE_FX_SLOT_COUNT+s);}
static uint8_t model_is_arp(uint8_t model){return (model==NOTE_FX_MODEL_ARP_FREE||model==NOTE_FX_MODEL_ARP_SYNC)?1U:0U;}
static uint8_t model_is_generator(uint8_t model){return (model_is_arp(model)||model==NOTE_FX_MODEL_EUCLID)?1U:0U;}
static note_fx_walker_category_t walker_category(uint8_t model)
{switch(model){case NOTE_FX_MODEL_PROBABILITY:return NOTE_FX_WALKER_PROBABILITY;
 case NOTE_FX_MODEL_GATE:return NOTE_FX_WALKER_GATE;
 case NOTE_FX_MODEL_GROOVE:return NOTE_FX_WALKER_GROOVE;
 case NOTE_FX_MODEL_ECHO:return NOTE_FX_WALKER_ECHO;
 case NOTE_FX_MODEL_HARMONIZER:return NOTE_FX_WALKER_HARMONIZER;
 case NOTE_FX_MODEL_CHORD:return NOTE_FX_WALKER_CHORD;
 case NOTE_FX_MODEL_ARP_FREE:case NOTE_FX_MODEL_ARP_SYNC:return NOTE_FX_WALKER_ARP;
 case NOTE_FX_MODEL_EUCLID:return NOTE_FX_WALKER_EUCLID;
 default:return NOTE_FX_WALKER_OFF;}}
static uint8_t model_needs_held(uint8_t model){return model_is_generator(model);}
static uint8_t held_family(uint8_t model){return(model==NOTE_FX_MODEL_EUCLID)?1U:0U;}
static uint8_t held_has_deadline(const note_fx_held_pitch_t*h){return(uint8_t)(h->lifetime_end!=0U);}
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
static uint8_t product_track(uint16_t lane){return(lane<(BRICK_ENTITY_TOP_LEVEL_COUNT-1U)
 *SEQ_LOGICAL_CAPACITY_MAX)?(uint8_t)(lane/SEQ_LOGICAL_CAPACITY_MAX)
 :(uint8_t)(BRICK_ENTITY_FIRST_GROUP_CHILD_ID+lane
 -(BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_LOGICAL_CAPACITY_MAX);}
static note_fx_echo_state_t*echo_state(const note_event_t*e){
 ++g_echo_diag.alloc_attempts;const uint16_t lane=product_lane(e);
 const uint8_t branch=note_event_branch(e);
 if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES||branch>=SEQ_PRODUCT_HARMONY_FANOUT_MAX){
  ++g_echo_diag.alloc_failures;return NULL;}
 note_fx_echo_state_t*x=&g_echo[lane][branch];
 if(x->active){++g_echo_diag.reuse_hits;
  if(product_track(lane)!=e->track){++g_echo_diag.key_collision_count;
   x->active=0U;g_echo_active[branch]&=~(UINT64_C(1)<<lane);
   if(g_echo_diag.active)--g_echo_diag.active;}}
 else ++g_echo_diag.free_hits;
 return x;}
static uint8_t lane_temporal(uint8_t track,uint16_t lane){return(track<BRICK_ENTITY_TOP_LEVEL_COUNT)?(uint8_t)(lane%SEQ_LOGICAL_CAPACITY_MAX):0U;}
static note_fx_held_pitch_t*held_at(uint8_t slot,uint16_t lane,uint8_t branch)
{return &g_held[held_family(g_slot[product_track(lane)][slot].model)]
                 [lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];}
static void held_remove(note_fx_family_runtime_t*f,note_fx_held_pitch_t*h){if(!h->source_token)return;memset(h,0,sizeof(*h));if(f->held_count)--f->held_count;}
static note_event_result_t held_ingest(uint8_t slot,note_fx_slot_runtime_t*r,const note_event_t*e){const uint16_t lane=product_lane(e);const uint8_t branch=note_event_branch(e);note_fx_family_runtime_t*f=family_state(e->track,slot);if(!family_owns(e->track,slot))return NOTE_EVENT_RESULT_ACCEPTED;if(lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES||branch>=SEQ_PRODUCT_HARMONY_FANOUT_MAX)return NOTE_EVENT_RESULT_REJECTED_DESTINATION;note_fx_held_pitch_t*h=held_at(slot,lane,branch);if(e->kind==NOTE_EVENT_KIND_OFF){if(h->source_token==e->source_id&&h->generation==e->source_generation)held_remove(f,h);return NOTE_EVENT_RESULT_ACCEPTED;}if(e->duration_samples==0U)return NOTE_EVENT_RESULT_DROPPED_POLICY;if(!h->source_token)++f->held_count;*h=(note_fx_held_pitch_t){.source_token=e->source_id,.generation=e->source_generation,.group_id=e->group_id,.lifetime_end=(uint32_t)(e->sample_abs+e->duration_samples),.note=e->note,.velocity=e->velocity,.dependency_mask=e->dependency_mask};(void)r;return NOTE_EVENT_RESULT_ACCEPTED;}
static void held_expire(uint8_t t,uint8_t s,uint64_t sample){note_fx_family_runtime_t*f=family_state(t,s);for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_held_pitch_t*h=held_at(s,lane,branch);if(h->source_token&&held_deadline_reached(h,sample))held_remove(f,h);}}
static note_fx_held_pitch_t*held_rank(uint8_t t,uint8_t s,note_fx_slot_runtime_t*r,uint8_t rank,uint8_t sorted){note_fx_held_pitch_t*items[SEQ_LOGICAL_CAPACITY_MAX*SEQ_PRODUCT_HARMONY_FANOUT_MAX];uint8_t count=0;for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_held_pitch_t*h=held_at(s,lane,branch);if(h->source_token)items[count++]=h;}if(rank>=count)return NULL;if(sorted)for(uint8_t i=1;i<count;++i){note_fx_held_pitch_t*x=items[i];uint8_t j=i;while(j&&items[j-1U]->note>x->note){items[j]=items[j-1U];--j;}items[j]=x;}return items[rank];}
static uint8_t arp_select(note_fx_slot_runtime_t*r,uint32_t step,uint8_t track,uint8_t slot,note_fx_held_pitch_t**oh,uint8_t*on){const uint8_t held_count=family_state(track,slot)->held_count;if(!held_count||!oh||!on)return 0;const note_fx_arp_style_t style=(note_fx_arp_style_t)r->p2;uint8_t rank;if(style==NOTE_FX_ARP_RANDOM){rank=(uint8_t)(mix32(step^((uint32_t)track<<24)^((uint32_t)slot<<16)^UINT32_C(0x9E3779B9))%held_count);}else if(style==NOTE_FX_ARP_UP_DOWN&&held_count>1U){const uint8_t cycle=(uint8_t)(2U*held_count-2U),pos=(uint8_t)(step%cycle);rank=pos<held_count?pos:(uint8_t)(cycle-pos);}else{rank=(uint8_t)(step%held_count);if(style==NOTE_FX_ARP_DOWN)rank=(uint8_t)(held_count-1U-rank);}note_fx_held_pitch_t*h=held_rank(track,slot,r,rank,(uint8_t)(style!=NOTE_FX_ARP_ORDER));if(!h)return 0;const uint8_t range=r->p3>=1&&r->p3<=4?r->p3:1;const uint32_t cycle=style==NOTE_FX_ARP_UP_DOWN&&held_count>1?(uint32_t)(2U*held_count-2U):held_count;const uint16_t raised=(uint16_t)h->note+(uint16_t)(12U*((step/cycle)%range));*on=raised<128U?(uint8_t)raised:h->note;*oh=h;return 1;}
static note_event_result_t emit_generated(uint8_t t,uint8_t s,const note_fx_held_pitch_t*h,uint8_t n,uint64_t sample,uint64_t duration,note_fx_emit_fn emit,void*ctx)
{
 if(!emit)return NOTE_EVENT_RESULT_ACCEPTED;
 const note_fx_walker_category_t category=(g_slot[t][s].model==NOTE_FX_MODEL_EUCLID)?NOTE_FX_WALKER_EUCLID:NOTE_FX_WALKER_ARP;
 const uint32_t probe=note_fx_walker_probe_begin();
 if(g_slot[t][s].model==NOTE_FX_MODEL_EUCLID)seq_probe_activity(SEQ_PROBE_EUCLID_EMISSIONS,1U);else seq_probe_activity(SEQ_PROBE_ARP_EMISSIONS,1U);
 const uint32_t token=next_token();const uint8_t mask=(uint8_t)(h->dependency_mask|(uint8_t)(1U<<s));const uint32_t bounded_duration=held_remaining(h,sample,duration>UINT32_MAX?UINT32_MAX:(uint32_t)duration);const note_fx_held_pitch_t*base=&g_held[held_family(g_slot[t][s].model)][0];const uint16_t lane=(uint16_t)((h-base)/SEQ_PRODUCT_HARMONY_FANOUT_MAX);const note_event_t e={.sample_abs=sample,.duration_samples=bounded_duration?bounded_duration:1U,.source_id=h->source_token,.occurrence_id=token,.source_generation=h->generation,.group_id=h->group_id?h->group_id:token,.track=t,.note=n,.velocity=h->velocity,.kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_FX,.stage=(uint8_t)(s+1U),.flags=NOTE_EVENT_FLAG_GENERATED,.temporal_index=lane_temporal(t,lane),.dependency_mask=mask};
 note_fx_walker_probe_record(category,probe,1U,1U,1U,0U,0U,1U);
 return emit(&e,ctx);
}
static uint8_t append(note_event_t*out,uint8_t cap,uint8_t*count,const note_event_t*e,uint8_t stage){if(*count>=cap)return 0;out[*count]=*e;out[*count].stage=stage;++*count;return 1;}
static uint64_t step_samples(void){const uint64_t x=((uint64_t)g_samples_per_step_q16+0x8000ULL)>>16;return x?x:1U;}
static uint8_t groove_phase(uint8_t track,uint64_t sample_abs){const uint64_t step=step_samples();const uint64_t delta=(sample_abs>=g_context->block_start)?sample_abs-g_context->block_start:0U;const uint64_t position=g_context->pattern_position_q16[track]+(delta<<16U)/step;return(uint8_t)((position*6ULL>>16U)%8ULL);}
static uint8_t probability_pass(const note_fx_slot_runtime_t*r,const note_event_t*e,uint8_t slot){static const uint8_t num[10]={0,1,2,1,2,3,1,2,3,4},den[10]={1,2,2,3,3,3,4,4,4,4};if(e->kind==NOTE_EVENT_KIND_OFF)return 1U;const uint8_t c=r->p2<10?r->p2:0;uint64_t identity=e->group_id;if(r->p3){const uint64_t p=seq_division_period_samples((uint8_t)(r->p3-1U),g_samples_per_step_q16);identity=e->sample_abs/(p?p:1U);}const uint64_t cycle=e->sample_abs/(step_samples()*16ULL);if(c&&((uint8_t)(cycle%den[c])+1U)!=num[c])return 0;if(r->p1>=100)return 1;if(!r->p1)return 0;return(uint8_t)((mix32((uint32_t)identity^(uint32_t)(identity>>32)^((uint32_t)e->track<<24)^((uint32_t)slot<<16)^(uint32_t)cycle)%100U)<r->p1);}
static uint8_t scale_shift(uint8_t note,int8_t shift){uint8_t sc=g_context->scale_index;if(sc>=7)sc=0;const uint8_t root=g_context->root_index%12U,count=g_scale_count[sc];int16_t best=0;uint8_t dist=255;for(int16_t d=-12;d<128;++d){int16_t oct=d/(int16_t)count,idx=d%(int16_t)count;if(idx<0){idx+=count;--oct;}const int16_t cand=(int16_t)root+g_scale[sc][idx]+12*oct;const uint8_t dd=(uint8_t)(cand>note?cand-note:note-cand);if(dd<dist){dist=dd;best=d;if(!dd)break;}}int16_t d=best+shift,oct=d/(int16_t)count,idx=d%(int16_t)count;if(idx<0){idx+=count;--oct;}int16_t target=(int16_t)root+g_scale[sc][idx]+12*oct;while(target<0)target+=12;while(target>127)target-=12;return(uint8_t)target;}

static note_event_result_t direct(uint8_t slot,note_fx_slot_runtime_t*r,const note_event_t*e,note_event_t*out,uint8_t cap,uint8_t*count){const uint8_t stage=(uint8_t)(slot+1U);
 if(r->model==NOTE_FX_MODEL_PROBABILITY){const uint8_t pass=probability_pass(r,e,slot);return!pass?NOTE_EVENT_RESULT_ACCEPTED:(append(out,cap,count,e,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY);}
 if(r->model==NOTE_FX_MODEL_GROOVE){note_event_t x=*e;const uint8_t phase=groove_phase(e->track,e->sample_abs);const note_fx_groove_template_t*t=&g_groove[r->p1%NOTE_FX_GROOVE_TYPE_COUNT];const int64_t off=((int64_t)t->timing[phase]*(int64_t)step_samples()*r->p2)/(96LL*100LL);x.sample_abs=(off<0&&x.sample_abs<(uint64_t)(-off))?0U:(uint64_t)((int64_t)x.sample_abs+off);if((e->provenance==NOTE_EVENT_SOURCE_KEY||e->provenance==NOTE_EVENT_SOURCE_MIDI)&&x.sample_abs<e->sample_abs)x.sample_abs=e->sample_abs;if(x.kind==NOTE_EVENT_KIND_ON){int32_t v=x.velocity+((int32_t)t->velocity[phase]*x.velocity*r->p3)/10000;if(v<1)v=1;if(v>127)v=127;x.velocity=(uint8_t)v;}return append(out,cap,count,&x,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 if(r->model==NOTE_FX_MODEL_ECHO){if(!append(out,cap,count,e,stage))return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
  if(e->kind==NOTE_EVENT_KIND_ON&&r->p2){note_fx_echo_state_t*x=echo_state(e);if(!x)return NOTE_EVENT_RESULT_REJECTED_CAPACITY;
   const uint32_t delay=(uint32_t)seq_division_period_samples(r->p1,g_samples_per_step_q16);
   const uint8_t was_active=x->active;const uint64_t promised=was_active?x->next_due:UINT64_MAX;
   *x=(note_fx_echo_state_t){
    .next_due=e->sample_abs+(delay?delay:1U),.delay=delay?delay:1U,
    .duration=e->duration_samples,.source_id=e->source_id,
    .occurrence_id=e->occurrence_id,.generation=e->source_generation,
    .group_id=e->group_id,.note=e->note,.velocity=e->velocity,.stage=stage,
    .flags=e->flags,.provenance=e->provenance,.dependency_mask=e->dependency_mask,
    .repeats=r->p2,.index=0U,.decay=r->p3,.active=1U};
   const uint16_t lane=product_lane(e);const uint8_t branch=note_event_branch(e);
   g_echo_active[branch]|=UINT64_C(1)<<lane;
   if(!was_active){++g_echo_diag.active;if(g_echo_diag.active>g_echo_diag.active_peak)
    g_echo_diag.active_peak=g_echo_diag.active;}
   if(promised<x->next_due)x->next_due=promised;}return NOTE_EVENT_RESULT_ACCEPTED;}
 if(r->model==NOTE_FX_MODEL_GATE){if(e->kind==NOTE_EVENT_KIND_OFF)return NOTE_EVENT_RESULT_ACCEPTED;note_event_t on=*e;on.flags|=NOTE_EVENT_FLAG_GATE;if(r->p3==NOTE_FX_GATE_MODE_LEGATO)on.flags|=NOTE_EVENT_FLAG_LEGATO;else if(r->p3==NOTE_FX_GATE_MODE_RETRIG)on.flags|=NOTE_EVENT_FLAG_RETRIGGER;int32_t var=0;if(r->p2){const uint32_t h=mix32(e->group_id^((uint32_t)slot<<24));var=(int32_t)(h%(2U*r->p2+1U))-(int32_t)r->p2;}int32_t pct=(int32_t)r->p1+var;if(pct<1)pct=1;if(r->p3==NOTE_FX_GATE_MODE_CLIP&&pct>100)pct=100;on.duration_samples=(uint32_t)(((uint64_t)pct*step_samples()+50U)/100U);return append(out,cap,count,&on,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 if(r->model==NOTE_FX_MODEL_HARMONIZER){uint8_t emitted=0;for(uint8_t voice=0;voice<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++voice){uint8_t interval=g_harmony[r->p1%NOTE_FX_HARMONIZER_TYPE_COUNT][voice];if(interval==255)continue;if(voice<r->p3)interval=(uint8_t)(interval+12U);if(r->p2&&voice)interval=(uint8_t)(interval+12U*(1U+(uint8_t)((voice-1U)%r->p2)));if((uint16_t)e->note+interval>=128)continue;note_event_t x=*e;x.note=(uint8_t)(e->note+interval);x.dependency_mask=(uint8_t)((x.dependency_mask&NOTE_EVENT_DEPENDENCY_SLOT_MASK)|(voice<<NOTE_EVENT_BRANCH_SHIFT));if(voice){x.occurrence_id=child_id(e->occurrence_id,slot,voice,0);x.provenance=NOTE_EVENT_SOURCE_FX;x.flags|=NOTE_EVENT_FLAG_GENERATED;}uint8_t dup=0;for(uint8_t i=0;i<*count;++i)if(out[i].note==x.note)dup=1;if(!dup){if(!append(out,cap,count,&x,stage))return NOTE_EVENT_RESULT_REJECTED_CAPACITY;++emitted;}}return emitted?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_DROPPED_POLICY;}
 return append(out,cap,count,e,stage)?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_REJECTED_CAPACITY;}

static note_event_result_t chord_group(uint8_t slot,note_fx_slot_runtime_t*r,
 const note_event_t*in,uint8_t n,note_event_t*out,uint8_t cap,uint8_t*count){
 const uint8_t stage=(uint8_t)(slot+1U);*count=0;
 for(uint8_t i=0;i<n;++i){note_event_t x=in[i];x.note=scale_shift(x.note,(int8_t)r->p1-7);
  uint8_t duplicate=0;for(uint8_t j=0;j<*count;++j)if(out[j].note==x.note)duplicate=1;
  if(!duplicate&&!append(out,cap,count,&x,stage))return NOTE_EVENT_RESULT_REJECTED_CAPACITY;}
 for(uint8_t i=1;i<*count;++i){note_event_t x=out[i];uint8_t j=i;while(j&&out[j-1U].note>x.note){out[j]=out[j-1U];--j;}out[j]=x;}
 const uint8_t invert=(r->p3<*count)?r->p3:*count;
 for(uint8_t i=0;i<*count;++i){uint16_t note=out[i].note;
  if(i<invert)note+=12U;
  if(r->p2!=0U)note+=(uint16_t)(12U*(i%(uint8_t)(r->p2+1U)));
  while(note>127U&&note>=12U)note-=12U;
  out[i].note=(uint8_t)note;}
 uint8_t write=0;for(uint8_t i=0;i<*count;++i){uint8_t duplicate=0;for(uint8_t j=0;j<write;++j)if(out[j].note==out[i].note)duplicate=1;if(!duplicate)out[write++]=out[i];}
 *count=write;return write?NOTE_EVENT_RESULT_ACCEPTED:NOTE_EVENT_RESULT_DROPPED_POLICY;}

static note_event_result_t echo_process(uint64_t start,uint64_t end,note_fx_emit_fn emit,void*ctx)
{const uint32_t probe=seq_probe_begin(SEQ_PROBE_ECHO);
 for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){uint64_t work=g_echo_active[branch];
 while(work){const uint16_t lane=(uint16_t)__builtin_ctzll(work);work&=work-1U;
  note_fx_echo_state_t*x=&g_echo[lane][branch];
  while(x->active&&x->next_due<end){const uint32_t walker_probe=note_fx_walker_probe_begin();
   const uint64_t due=x->next_due;note_event_t e;
   ++x->index;e=(note_event_t){.sample_abs=due,.duration_samples=x->duration,
    .source_id=x->source_id,.occurrence_id=x->occurrence_id,
    .source_generation=x->generation,.group_id=x->group_id,
    .track=product_track(lane),
    .note=x->note,.velocity=x->velocity,.kind=NOTE_EVENT_KIND_ON,
    .provenance=x->provenance,.stage=x->stage,.flags=x->flags,
    .temporal_index=lane_temporal(product_track(lane),lane),
    .dependency_mask=(uint8_t)((x->dependency_mask&NOTE_EVENT_DEPENDENCY_SLOT_MASK)
      |(branch<<NOTE_EVENT_BRANCH_SHIFT))};
   e.occurrence_id=child_id(e.occurrence_id,
       (uint8_t)(e.stage?e.stage-1U:0U),branch,x->index);
   e.group_id=mix32(e.group_id^((uint32_t)e.stage<<24)^x->index);if(!e.group_id)e.group_id=1U;
   e.provenance=NOTE_EVENT_SOURCE_FX;e.flags|=(NOTE_EVENT_FLAG_GENERATED|NOTE_EVENT_FLAG_ECHO);
   uint32_t velocity=e.velocity;for(uint8_t n=0U;n<x->index;++n)
       velocity=(velocity*(100U-x->decay)+50U)/100U;
   e.velocity=(uint8_t)(velocity?velocity:1U);
   if(e.duration_samples==0U)e.duration_samples=x->delay;
   note_fx_walker_probe_record(NOTE_FX_WALKER_ECHO,walker_probe,1U,1U,1U,1U,0U,1U);
   seq_probe_activity(SEQ_PROBE_ECHO_DUE,1U);
   note_event_result_t result=NOTE_EVENT_RESULT_ACCEPTED;if(emit)result=emit(&e,ctx);
   if(x->index>=x->repeats){x->active=0U;g_echo_active[branch]&=~(UINT64_C(1)<<lane);
    if(g_echo_diag.active)--g_echo_diag.active;}
   else{x->next_due=due+x->delay;
    if(x->next_due<=due)x->next_due=due+x->delay;}
    if(result!=NOTE_EVENT_RESULT_ACCEPTED){seq_probe_end(SEQ_PROBE_ECHO,probe);return result;}}
 }}(void)start;seq_probe_end(SEQ_PROBE_ECHO,probe);return NOTE_EVENT_RESULT_ACCEPTED;}

void note_fx_engine_init(void){memset(&g_seq_context,0,sizeof(g_seq_context));memset(g_echo,0,sizeof(g_echo));memset(g_echo_active,0,sizeof(g_echo_active));memset(g_held,0,sizeof(g_held));memset(g_family,0,sizeof(g_family));for(uint8_t t=0;t<NOTE_FX_TRACK_COUNT;++t)for(uint8_t f=0;f<2U;++f)g_family[t][f].owner_slot=UINT8_MAX;memset(&g_echo_diag,0,sizeof(g_echo_diag));g_seq_context.samples_per_step_q16=UINT32_C(65536);}
void note_fx_engine_echo_diag_capture(note_fx_echo_diag_t*out){if(out)*out=g_echo_diag;}
void note_fx_engine_set_samples_per_step_q16(uint32_t v){g_samples_per_step_q16=v?v:1U;}
static void held_clear_family(uint8_t t,uint8_t family){for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch)memset(&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch],0,sizeof(note_fx_held_pitch_t));g_family[t][family].held_count=0U;}
static void family_reselect(uint8_t t,uint8_t family){note_fx_family_runtime_t*f=&g_family[t][family];uint8_t owner=UINT8_MAX;for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)if(model_is_generator(g_slot[t][s].model)&&held_family(g_slot[t][s].model)==family){owner=s;break;}if(owner!=f->owner_slot){held_clear_family(t,family);f->owner_slot=owner;}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
static void echo_clear_owner(uint8_t t,uint8_t s){for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane){if(product_track(lane)!=t)continue;for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_echo_state_t*x=&g_echo[lane][branch];if(x->active&&x->stage==(uint8_t)(s+1U)){x->active=0U;g_echo_active[branch]&=~(UINT64_C(1)<<lane);if(g_echo_diag.active)--g_echo_diag.active;}}}}
note_event_result_t note_fx_engine_configure(uint8_t t,uint8_t s,uint8_t model,uint8_t p1,uint8_t p2,uint8_t p3){if(t>=NOTE_FX_TRACK_COUNT||s>=NOTE_FX_SLOT_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;note_fx_slot_runtime_t*r=&g_slot[t][s];if(model>=NOTE_FX_MODEL_COUNT)model=NOTE_FX_MODEL_OFF;const uint8_t old_model=r->model;if(old_model!=model){if(old_model==NOTE_FX_MODEL_ECHO)echo_clear_owner(t,s);memset(r,0,sizeof(*r));}r->model=model;r->p1=p1;r->p2=p2;r->p3=p3;if(model_is_generator(old_model))family_reselect(t,held_family(old_model));if(model_is_generator(model))family_reselect(t,held_family(model));refresh_work(t,s);return NOTE_EVENT_RESULT_ACCEPTED;}
note_event_result_t note_fx_engine_transform(uint8_t s,const note_event_t*in,uint8_t n,note_event_t*out,uint8_t cap,uint8_t*count)
{
 const uint32_t lookup_probe=note_fx_walker_probe_begin();
 if(!in||!out||!count||!n||s>=NOTE_FX_SLOT_COUNT){
  note_fx_walker_probe_record(NOTE_FX_WALKER_LOOKUP,lookup_probe,n,0U,0U,0U,0U,0U);
  return NOTE_EVENT_RESULT_DROPPED_POLICY;}
 for(uint8_t i=0;i<n;++i)if(!note_event_is_valid(&in[i])||in[i].track>=NOTE_FX_TRACK_COUNT||in[i].stage!=s||in[i].track!=in[0].track||in[i].group_id!=in[0].group_id||in[i].kind!=in[0].kind){
  note_fx_walker_probe_record(NOTE_FX_WALKER_LOOKUP,lookup_probe,n,0U,0U,0U,0U,0U);
  return NOTE_EVENT_RESULT_DROPPED_POLICY;}
 note_fx_slot_runtime_t*r=&g_slot[in[0].track][s];
 uint32_t held_count=0U;
 if(model_needs_held(r->model))for(uint8_t i=0;i<n;++i){
  const note_event_result_t held_result=held_ingest(s,r,&in[i]);++held_count;
  if(held_result!=NOTE_EVENT_RESULT_ACCEPTED){
   note_fx_walker_probe_record(NOTE_FX_WALKER_LOOKUP,lookup_probe,n,0U,0U,0U,held_count,0U);
   return held_result;}}
 refresh_work(in[0].track,s);
 note_fx_walker_probe_record(NOTE_FX_WALKER_LOOKUP,lookup_probe,n,0U,0U,0U,held_count,0U);
 const note_fx_walker_category_t category=walker_category(r->model);
 uint32_t fx_probe=note_fx_walker_probe_begin();
 if(r->model==NOTE_FX_MODEL_CHORD){
  const note_event_result_t result=chord_group(s,r,in,n,out,cap,count);
  note_fx_walker_probe_record(category,fx_probe,n,*count,*count,0U,0U,0U);return result;}
 *count=0;
 for(uint8_t i=0;i<n;++i){const note_event_t*e=&in[i];
  if(!model_is_arp(r->model)&&r->model!=NOTE_FX_MODEL_EUCLID){
   const uint8_t before=*count;const note_event_result_t result=direct(s,r,e,out,cap,count);
   note_fx_walker_probe_record(category,fx_probe,1U,(uint32_t)(*count-before),
      (uint32_t)(*count-before),r->model==NOTE_FX_MODEL_ECHO?1U:0U,0U,0U);
   if(result!=NOTE_EVENT_RESULT_ACCEPTED)return result;
   if(i+1U<n)fx_probe=note_fx_walker_probe_begin();}}
 if(model_is_generator(r->model))
  note_fx_walker_probe_record(category,fx_probe,n,0U,0U,0U,held_count,0U);
 return NOTE_EVENT_RESULT_ACCEPTED;
}
void note_fx_engine_forget_causal_source(uint8_t t,uint32_t source){if(t>=NOTE_FX_TRACK_COUNT||!source)return;for(uint8_t family=0;family<2U;++family)for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if(h->source_token==source)held_remove(&g_family[t][family],h);}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
static note_event_result_t note_fx_engine_process_due(uint64_t start,uint16_t frames,uint32_t sps,note_fx_emit_fn emit,void*ctx){note_fx_engine_set_samples_per_step_q16(sps);const uint64_t end=start+frames;note_event_result_t echo_result=echo_process(start,end,emit,ctx);if(echo_result!=NOTE_EVENT_RESULT_ACCEPTED)return echo_result;const uint32_t generator_probe=seq_probe_begin(SEQ_PROBE_GENERATOR);uint64_t processed=0,work;while((work=(g_work_slot_mask&~processed))!=0){const uint32_t idx=(uint32_t)__builtin_ctzll(work);processed|=UINT64_C(1)<<idx;const uint8_t t=(uint8_t)(idx/NOTE_FX_SLOT_COUNT),s=(uint8_t)(idx%NOTE_FX_SLOT_COUNT);note_fx_slot_runtime_t*r=&g_slot[t][s];note_fx_family_runtime_t*f=family_state(t,s);const uint32_t owner_probe=note_fx_walker_probe_begin();const uint64_t owner_classified_before=note_fx_walker_probe_cycles_total();held_expire(t,s,start);refresh_work(t,s);if(!f->held_count||!family_owns(t,s)){const uint32_t owner_elapsed=DWT->CYCCNT-owner_probe;const uint64_t owner_classified=note_fx_walker_probe_cycles_total()-owner_classified_before;note_fx_walker_probe_record_elapsed(walker_category(r->model),owner_elapsed>(uint32_t)owner_classified?owner_elapsed-(uint32_t)owner_classified:0U);continue;}uint64_t period=seq_division_period_samples(r->model==NOTE_FX_MODEL_EUCLID?r->p3:r->p1,sps);if(!period)period=1;const note_fx_phase_policy_t policy=(r->model==NOTE_FX_MODEL_ARP_FREE)?NOTE_FX_PHASE_TRANSPORT_FREE:NOTE_FX_PHASE_PATTERN_SYNC;const uint64_t position=(policy==NOTE_FX_PHASE_TRANSPORT_FREE)?g_context->transport_position_q16:g_context->pattern_position_q16[t];const uint64_t step_sample=step_samples();uint64_t period_q16=(period<<16U)/step_sample;if(!period_q16)period_q16=1U;uint32_t ordinal=(uint32_t)(position/period_q16);const uint64_t remainder=position%period_q16;uint64_t sample=start;if(remainder!=0U){const uint64_t remaining=period_q16-remainder;sample+=((remaining*step_sample)+UINT16_MAX)>>16U;++ordinal;}while(sample<end){held_expire(t,s,sample);if(!f->held_count)break;if(model_is_arp(r->model)){note_fx_held_pitch_t*h=NULL;uint8_t note=0;if(arp_select(r,ordinal,t,s,&h,&note)){const note_event_result_t result=emit_generated(t,s,h,note,sample,period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED){seq_probe_end(SEQ_PROBE_GENERATOR,generator_probe);return result;}}}else{const uint8_t length=r->p1?r->p1:NOTE_FX_EUCLID_LENGTH_DEFAULT,pulse=r->p2<=length?r->p2:length;const uint64_t mask=euclid_build_mask(length,pulse);if((mask>>(ordinal%length))&UINT64_C(1))for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){const note_fx_held_pitch_t*h=held_at(s,lane,branch);if(!h->source_token)continue;const note_event_result_t result=emit_generated(t,s,h,h->note,sample,period,emit,ctx);if(result!=NOTE_EVENT_RESULT_ACCEPTED){seq_probe_end(SEQ_PROBE_GENERATOR,generator_probe);return result;}}}++ordinal;sample+=period;}refresh_work(t,s);const uint32_t owner_elapsed=DWT->CYCCNT-owner_probe;const uint64_t owner_classified=note_fx_walker_probe_cycles_total()-owner_classified_before;note_fx_walker_probe_record_elapsed(walker_category(r->model),owner_elapsed>(uint32_t)owner_classified?owner_elapsed-(uint32_t)owner_classified:0U);}seq_probe_end(SEQ_PROBE_GENERATOR,generator_probe);return NOTE_EVENT_RESULT_ACCEPTED;}
void note_fx_engine_release_terminal(const note_event_t*e){if(!e||e->track>=NOTE_FX_TRACK_COUNT||e->kind!=NOTE_EVENT_KIND_OFF)return;note_fx_engine_forget_causal_source(e->track,e->source_id);}
void note_fx_engine_forget_dependency(uint8_t t,uint8_t owner){if(t>=NOTE_FX_TRACK_COUNT||owner>=NOTE_FX_SLOT_COUNT)return;const uint8_t bit=(uint8_t)(1U<<owner);for(uint8_t family=0;family<2U;++family)for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];if((h->dependency_mask&bit)!=0U)held_remove(&g_family[t][family],h);}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
void note_fx_engine_forget_causal_sources_from_slot(uint8_t t,uint8_t first,const uint32_t*ids,uint16_t count){if(t>=NOTE_FX_TRACK_COUNT||first>=NOTE_FX_SLOT_COUNT||!ids)return;for(uint8_t family=0;family<2U;++family){if(g_family[t][family].owner_slot<first)continue;for(uint16_t lane=0;lane<SEQ_PRODUCT_MAX_EMITTING_VOICES;++lane)if(product_track(lane)==t)for(uint8_t branch=0;branch<SEQ_PRODUCT_HARMONY_FANOUT_MAX;++branch){note_fx_held_pitch_t*h=&g_held[family][lane*SEQ_PRODUCT_HARMONY_FANOUT_MAX+branch];for(uint16_t j=0;j<count;++j)if(h->source_token==ids[j]){held_remove(&g_family[t][family],h);break;}}}for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);}
note_event_result_t note_fx_engine_cleanup(uint8_t t){if(t>=NOTE_FX_TRACK_COUNT)return NOTE_EVENT_RESULT_DROPPED_POLICY;for(uint8_t family=0;family<2U;++family)held_clear_family(t,family);for(uint8_t s=0;s<NOTE_FX_SLOT_COUNT;++s)refresh_work(t,s);return NOTE_EVENT_RESULT_ACCEPTED;}

note_event_result_t note_fx_engine_process(uint64_t start,uint16_t frames,uint32_t sps,uint64_t transport,const uint32_t pattern[NOTE_FX_TRACK_COUNT],uint8_t scale,uint8_t root,note_fx_emit_fn emit,void*ctx){g_seq_context.block_start=start;g_seq_context.transport_position_q16=transport;g_seq_context.scale_index=scale;g_seq_context.root_index=root;memcpy(g_seq_context.pattern_position_q16,pattern,sizeof(g_seq_context.pattern_position_q16));return note_fx_engine_process_due(start,frames,sps,emit,ctx);}
