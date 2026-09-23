#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "Keyboard/kbd_chords_dict.h"
#include "NoteFx/note_fx_engine.h"
#include "Seq/seq_division_catalog.h"

static const seq_division_desc_t division={"1",1U,1U,UINT32_C(65536)};
const seq_division_desc_t*seq_division_get(uint8_t index){(void)index;return &division;}
bool kbd_scale_contains_pitch_class(uint8_t scale,uint8_t pitch){(void)scale;return pitch==0U||pitch==2U||pitch==4U||pitch==5U||pitch==7U||pitch==9U||pitch==11U;}

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

int main(void){test_non_poly_modes_preserve_rhythm();test_seq4_positions_and_pitches();
 test_fixed_stage_field_contracts();test_note_off_fanout_contract();return 0;}
