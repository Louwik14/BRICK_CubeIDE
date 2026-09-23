#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "Keyboard/kbd_chords_dict.h"
#include "NoteFx/note_fx_arp.h"
#include "NoteFx/note_fx_engine.h"
#include "Seq/seq_division_catalog.h"

static const seq_division_desc_t division={"1",1U,1U,UINT32_C(65536)};
const seq_division_desc_t*seq_division_get(uint8_t index){(void)index;return &division;}
bool kbd_scale_contains_pitch_class(uint8_t scale,uint8_t pitch){(void)scale;return pitch==0U||pitch==2U||pitch==4U||pitch==5U||pitch==7U||pitch==9U||pitch==11U;}
uint8_t note_fx_chain_param_map(param_id_t id,note_fx_chain_stage_t*out_stage,uint8_t*out_param){
 if(id<PARAM_MIDI_FX_GENERATOR_P1||id>PARAM_MIDI_FX_TRIG_P4||!out_stage||!out_param)return 0U;
 const uint16_t offset=(uint16_t)(id-PARAM_MIDI_FX_GENERATOR_P1);
 *out_stage=(note_fx_chain_stage_t)(offset/NOTE_FX_CHAIN_PARAM_COUNT);
 *out_param=(uint8_t)(offset%NOTE_FX_CHAIN_PARAM_COUNT);return 1U;}
uint8_t note_fx_chain_param_is_plockable(note_fx_chain_stage_t stage,uint8_t param){
 return(uint8_t)(stage<NOTE_FX_CHAIN_STAGE_COUNT&&param<NOTE_FX_CHAIN_PARAM_COUNT
  &&!(stage==NOTE_FX_CHAIN_STAGE_GENERATOR&&param==3U));}
uint8_t note_fx_chain_state_make_effective(const note_fx_chain_state_t*raw,note_fx_chain_state_t*out){
 if(!raw||!out)return 0U;
 *out=*raw;return 1U;}

static note_event_t source(uint64_t sample,uint8_t note){return(note_event_t){
 .sample_abs=sample,.duration_samples=37U,.source_id=(uint32_t)(sample+1U),
 .occurrence_id=(uint32_t)(sample+101U),.source_generation=7U,
 .group_id=(uint32_t)(sample+201U),.track=0U,.note=note,.velocity=100U,
 .kind=NOTE_EVENT_KIND_ON,.provenance=NOTE_EVENT_SOURCE_STEP,
 .stage=NOTE_EVENT_STAGE_SOURCE,.timing_class=NOTE_EVENT_TIMING_SCHEDULED};}
static note_fx_chain_state_t state(uint8_t mode,uint8_t type){note_fx_chain_state_t s={0};s.voicer.mode=mode;s.voicer.type=type;s.scaler.transpose=12U;s.trig.gate=100U;return s;}
static void assert_identity(const note_event_t*in,const note_event_t*out){
 assert(out->sample_abs==in->sample_abs);assert(out->duration_samples==in->duration_samples);
 assert(out->source_id==in->source_id);assert(out->source_generation==in->source_generation);
 assert(out->group_id==in->group_id);assert(out->kind==in->kind);}

static void test_non_poly_modes_preserve_rhythm(void){
 static const uint8_t modes[]={NOTE_FX_VOICER_MODE_SEQ2,NOTE_FX_VOICER_MODE_SEQ3,
  NOTE_FX_VOICER_MODE_SEQ4,NOTE_FX_VOICER_MODE_UPDN2,NOTE_FX_VOICER_MODE_UPDN3,
  NOTE_FX_VOICER_MODE_UPDN4,NOTE_FX_VOICER_MODE_CLIMB2,
  NOTE_FX_VOICER_MODE_CLIMB3,NOTE_FX_VOICER_MODE_CLIMB4};
 for(uint8_t m=0U;m<sizeof(modes);++m){note_fx_engine_init();
  note_fx_chain_state_t s=state(modes[m],0U);
  assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);
  for(uint8_t i=0U;i<24U;++i){note_event_t in=source((uint64_t)i*13U,(uint8_t)(i==7U?127U:60U)),out[4];uint8_t count=0U;
   assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
   assert(count==1U);assert_identity(&in,&out[0]);assert(out[0].occurrence_id==in.occurrence_id);}}}

static void test_seq4_positions_and_pitches(void){note_fx_engine_init();
 note_fx_chain_state_t s=state(NOTE_FX_VOICER_MODE_SEQ4,5U);
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);
 static const uint64_t positions[]={0U,20U,30U,50U,70U};
 static const uint8_t notes[]={60U,64U,67U,71U,60U};
 for(uint8_t i=0U;i<5U;++i){note_event_t in=source(positions[i],60U),out[4];uint8_t count=0U;
  assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
  assert(count==1U);assert(out[0].sample_abs==positions[i]);assert(out[0].note==notes[i]);}}

static void test_fixed_stage_field_contracts(void){note_fx_engine_init();
 note_fx_chain_state_t s=state(NOTE_FX_VOICER_MODE_4,5U);
 s.scaler.scale=NOTE_FX_SCALER_SCALE_MAJOR;s.scaler.stick=NOTE_FX_SCALER_STICK_NEAREST;
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);
 note_event_t in=source(123U,61U),out[4];uint8_t count=0U;
 assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(count==4U);for(uint8_t i=0U;i<count;++i)assert_identity(&in,&out[i]);
 s=state(NOTE_FX_VOICER_MODE_OFF,0U);s.trig.chance=NOTE_FX_TRIG_CHANCE_100;s.trig.gate=25U;
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);count=0U;
 assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(count==1U);assert(out[0].sample_abs==in.sample_abs);assert(out[0].duration_samples==1U);
 assert(out[0].source_id==in.source_id&&out[0].group_id==in.group_id);}

static void test_note_off_fanout_contract(void){note_fx_engine_init();
 note_fx_chain_state_t s=state(NOTE_FX_VOICER_MODE_OFF,0U);
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);
 note_event_t in=source(50U,60U),out[4];in.kind=NOTE_EVENT_KIND_OFF;in.duration_samples=0U;
 uint8_t count=0U;assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(count==1U);assert_identity(&in,&out[0]);
 s=state(NOTE_FX_VOICER_MODE_4,0U);
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);count=0U;
 assert(note_fx_chain_engine_transform(&in,1U,out,4U,&count)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(count==3U);for(uint8_t i=0U;i<count;++i)assert(out[i].sample_abs==in.sample_abs);}

typedef struct {note_event_t event[32];uint8_t count;} capture_t;
static note_event_result_t capture_emit(const note_event_t*e,void*context){capture_t*c=context;
 assert(c->count<32U);c->event[c->count++]=*e;return NOTE_EVENT_RESULT_ACCEPTED;}
static void test_generator_held_identity_and_first_horizon(void){note_fx_engine_init();
 note_fx_chain_state_t s=state(NOTE_FX_VOICER_MODE_OFF,0U);
 s.generator.mode=NOTE_FX_GENERATOR_HOLD;s.generator.p1=NOTE_FX_ARP_UP;
 s.generator.p2=0U;s.generator.p3=2U;
 assert(note_fx_chain_engine_configure(0U,&s)==NOTE_EVENT_RESULT_ACCEPTED);
 note_event_t chord[3]={source(0U,67U),source(0U,60U),source(0U,64U)};
 for(uint8_t i=0U;i<3U;++i){chord[i].temporal_index=i;
  chord[i].source_id=(uint32_t)(10U+i);chord[i].group_id=1U;
  chord[i].duration_samples=1U;}
 note_event_t scratch[4];uint8_t transformed=0U;
 assert(note_fx_chain_engine_transform(chord,3U,scratch,4U,&transformed)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(transformed==0U);
 uint32_t position[NOTE_FX_TRACK_COUNT]={0};const uint8_t length[NOTE_FX_TRACK_COUNT]={16U};
 capture_t capture={0};
 assert(note_fx_chain_engine_process(0U,0U,1U,UINT32_C(65536),0U,position,length,
  capture_emit,&capture)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(capture.count==1U&&capture.event[0].note==60U);
 capture=(capture_t){0};
 position[0]=UINT32_C(1)<<16U;
 assert(note_fx_chain_engine_process(0U,1U,5U,UINT32_C(65536),UINT64_C(65536),
  position,length,capture_emit,&capture)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(capture.count==5U);
 static const uint8_t expected[]={64U,67U,72U,76U,79U};
 for(uint8_t i=0U;i<5U;++i)assert(capture.event[i].note==expected[i]);
 note_event_t replacement=source(6U,55U);replacement.temporal_index=0U;
 replacement.source_id=99U;replacement.group_id=2U;
 assert(note_fx_chain_engine_transform(&replacement,1U,scratch,4U,&transformed)==NOTE_EVENT_RESULT_ACCEPTED);
 capture=(capture_t){0};
 position[0]=UINT32_C(6)<<16U;
 assert(note_fx_chain_engine_process(0U,6U,1U,UINT32_C(65536),UINT64_C(6)<<16U,
  position,length,capture_emit,&capture)==NOTE_EVENT_RESULT_ACCEPTED);
 assert(capture.count==1U&&capture.event[0].note==55U);}

int main(void){test_non_poly_modes_preserve_rhythm();test_seq4_positions_and_pitches();
 test_fixed_stage_field_contracts();test_note_off_fanout_contract();
 test_generator_held_identity_and_first_horizon();return 0;}
