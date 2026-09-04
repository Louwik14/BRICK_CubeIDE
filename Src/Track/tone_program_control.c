#include "Track/tone_program_control.h"

#include <stddef.h>
#include <string.h>
#include <math.h>

#include "Platform/memory_layout.h"
#include "Seq/seq_types.h"
#include "Track/tone_param_codec.h"
#include "IPC/live_clock_control.h"
#include "App/live_parameter_audio_publication.h"
#include "IPC/live_parameter_event.h"

SEQ_STATE_D2 static tone_program_control_t g_tone_program[SEQ_LANE_CAPACITY];

static float *tone_field(tone_program_control_t *p, param_id_t id)
{
    if (p == NULL) return NULL;
#define F(i,f) case i:return &(f)
    switch (p->tag)
    {
    case TRACK_RUNTIME_TYPE_PRISM:
        switch(id){
        F(PARAM_PRISM_OSC1_PARAM1,p->state.prism.osc[0].param1); F(PARAM_PRISM_OSC1_PARAM2,p->state.prism.osc[0].param2);
        F(PARAM_PRISM_OSC1_AMOD,p->state.prism.osc[0].amod); F(PARAM_PRISM_OSC1_MODEL,p->state.prism.osc[0].model);
        F(PARAM_PRISM_OSC2_PARAM1,p->state.prism.osc[1].param1); F(PARAM_PRISM_OSC2_PARAM2,p->state.prism.osc[1].param2);
        F(PARAM_PRISM_OSC2_AMOD,p->state.prism.osc[1].amod); F(PARAM_PRISM_OSC2_MODEL,p->state.prism.osc[1].model);
        F(PARAM_PRISM_VOLUME,p->state.prism.volume); F(PARAM_PRISM_BALANCE,p->state.prism.balance);
        F(PARAM_PRISM_TUNE,p->state.prism.tune); F(PARAM_PRISM_DETUNE,p->state.prism.detune);
        F(PARAM_PRISM_DRIFT,p->state.prism.drift); F(PARAM_PRISM_PITCH_MOD1,p->state.prism.pitch_mod[0]);
        F(PARAM_PRISM_PITCH_MOD2,p->state.prism.pitch_mod[1]); F(PARAM_PRISM_PHASE1_RESET,p->state.prism.phase1_reset); default:return NULL;}
    case TRACK_RUNTIME_TYPE_STACK:
        switch(id){
        F(PARAM_STACK_OSC1_LEVEL,p->state.stack.osc[0].level);F(PARAM_STACK_OSC2_LEVEL,p->state.stack.osc[1].level);F(PARAM_STACK_OSC3_LEVEL,p->state.stack.osc[2].level);
        F(PARAM_STACK_NOISE_LEVEL,p->state.stack.noise_level);F(PARAM_STACK_OSC1_MODEL,p->state.stack.osc[0].model);F(PARAM_STACK_OSC1_TUNE,p->state.stack.osc[0].tune);
        F(PARAM_STACK_OSC1_TIMBRE,p->state.stack.osc[0].timbre);F(PARAM_STACK_OSC1_COLOR,p->state.stack.osc[0].color);F(PARAM_STACK_OSC2_MODEL,p->state.stack.osc[1].model);
        F(PARAM_STACK_OSC2_TUNE,p->state.stack.osc[1].tune);F(PARAM_STACK_OSC2_TIMBRE,p->state.stack.osc[1].timbre);F(PARAM_STACK_OSC2_COLOR,p->state.stack.osc[1].color);
        F(PARAM_STACK_OSC3_MODEL,p->state.stack.osc[2].model);F(PARAM_STACK_OSC3_TUNE,p->state.stack.osc[2].tune);F(PARAM_STACK_OSC3_TIMBRE,p->state.stack.osc[2].timbre);
        F(PARAM_STACK_OSC3_COLOR,p->state.stack.osc[2].color);F(PARAM_STACK_OSC_DETUNE,p->state.stack.osc_detune);F(PARAM_STACK_PHASE_RESET,p->state.stack.phase_reset);default:return NULL;}
    case TRACK_RUNTIME_TYPE_WAVE:
        switch(id){F(PARAM_WAVE_OSC1_POS,p->state.wave.osc[0].position);F(PARAM_WAVE_OSC1_START,p->state.wave.osc[0].start);F(PARAM_WAVE_OSC1_LEN,p->state.wave.osc[0].length);
        F(PARAM_WAVE_OSC2_POS,p->state.wave.osc[1].position);F(PARAM_WAVE_OSC2_START,p->state.wave.osc[1].start);F(PARAM_WAVE_OSC2_LEN,p->state.wave.osc[1].length);
        F(PARAM_WAVE_VOLUME,p->state.wave.volume);F(PARAM_WAVE_BALANCE,p->state.wave.balance);F(PARAM_WAVE_TUNE,p->state.wave.tune);F(PARAM_WAVE_DETUNE,p->state.wave.detune);default:return NULL;}
    case TRACK_RUNTIME_TYPE_RAM:
        switch(id){F(PARAM_SAMPLER_GAIN,p->state.ram.gain);F(PARAM_SAMPLER_START,p->state.ram.start);F(PARAM_SAMPLER_LENGTH,p->state.ram.length);F(PARAM_SAMPLER_MODE,p->state.ram.mode);
        F(PARAM_SAMPLER_TUNE,p->state.ram.tune);F(PARAM_SAMPLER_LOOP_START,p->state.ram.loop_start);F(PARAM_SAMPLER_SLICE_COUNT,p->state.ram.slice_count);default:return NULL;}
    case TRACK_RUNTIME_TYPE_STREAM:
        switch(id){F(PARAM_SAMPLER_GAIN,p->state.stream.gain);F(PARAM_SAMPLER_CLIP_SOURCE_BPM,p->state.stream.source_bpm);F(PARAM_SAMPLER_CLIP_PLAY_MODE,p->state.stream.play_mode);
        F(PARAM_SAMPLER_CLIP_LOOP,p->state.stream.loop);F(PARAM_SAMPLER_CLIP_STRETCH_MODE,p->state.stream.stretch_mode);F(PARAM_SAMPLER_CLIP_PITCH,p->state.stream.pitch);
        F(PARAM_SAMPLER_CLIP_SYNC_LENGTH,p->state.stream.sync_length);F(PARAM_SAMPLER_CLIP_GRAIN,p->state.stream.grain);default:return NULL;}
    case TRACK_RUNTIME_TYPE_LOOPER:
        switch(id){F(PARAM_LOOPER_XFADE,p->state.looper.xfade);F(PARAM_LOOPER_STRETCH,p->state.looper.stretch);F(PARAM_LOOPER_PITCH,p->state.looper.pitch);F(PARAM_LOOPER_GRAIN,p->state.looper.grain);default:return NULL;}
    case TRACK_RUNTIME_TYPE_MULTI:
        switch(id){F(PARAM_SAMPLER_GAIN,p->state.multi.gain);F(PARAM_SAMPLER_MULTI_LOOP,p->state.multi.loop);default:return NULL;}
    case TRACK_RUNTIME_TYPE_MIDI: case TRACK_RUNTIME_TYPE_EXTERNAL:
        if(id==PARAM_MIDI_PROGRAM)return &p->state.midi.program;
        if(id>=PARAM_MIDI_CC1_1&&id<=PARAM_MIDI_CC3_4)return &p->state.midi.cc[((uint16_t)id-(uint16_t)PARAM_MIDI_CC1_1)/4U][((uint16_t)id-(uint16_t)PARAM_MIDI_CC1_1)%4U];
        return NULL;
    case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:
        switch(id){F(PARAM_DRUM_TRX_BD_PITCH,p->state.drum_analog.pitch);F(PARAM_DRUM_TRX_BD_DECAY,p->state.drum_analog.decay);F(PARAM_DRUM_TRX_BD_PITCH_SWEEP,p->state.drum_analog.pitch_sweep);
        F(PARAM_DRUM_TRX_BD_SWEEP_DECAY,p->state.drum_analog.sweep_decay);F(PARAM_DRUM_TRX_BD_ATTACK,p->state.drum_analog.attack);F(PARAM_DRUM_TRX_BD_NOISE,p->state.drum_analog.noise);
        F(PARAM_DRUM_TRX_BD_HARMONICS,p->state.drum_analog.harmonics);F(PARAM_DRUM_TRX_BD_DRIVE,p->state.drum_analog.drive);default:return NULL;}
    case TRACK_RUNTIME_TYPE_DRUM_MD:
        if(id==PARAM_DRUM_MD_MODEL)return &p->state.drum_md.model;
        if(id>=PARAM_DRUM_MD_P1&&id<=PARAM_DRUM_MD_P8)return &p->state.drum_md.p[(uint16_t)id-(uint16_t)PARAM_DRUM_MD_P1];
        return NULL;
    default:return NULL;
    }
#undef F
}

void tone_program_control_init(void){memset(g_tone_program,0,sizeof(g_tone_program));}
uint8_t tone_program_control_activate(uint8_t track,track_runtime_type_t type)
{
 if(track>=SEQ_LANE_CAPACITY)return 0U;
 tone_program_control_t*p=&g_tone_program[track];memset(p,0,sizeof(*p));p->tag=type;
 const uint8_t count=tone_param_codec_count(type);
 for(uint8_t slot=0;slot<count;++slot){param_id_t id;if(tone_param_codec_slot_to_param(type,slot,&id)){float*f=tone_field(p,id);if(f!=NULL)*f=param_registry[id].default_value;}}
 return 1U;
}
uint8_t tone_program_control_get(uint8_t track,param_id_t id,float*out)
{if(track>=SEQ_LANE_CAPACITY||out==NULL)return 0U;float*f=tone_field(&g_tone_program[track],id);if(f==NULL)return 0U;*out=*f;return 1U;}
uint8_t tone_program_control_set(uint8_t track,param_id_t id,float value)
{if(track>=SEQ_LANE_CAPACITY)return 0U;float*f=tone_field(&g_tone_program[track],id);if(f==NULL)return 0U;*f=value;return 1U;}
uint8_t tone_program_control_capture(uint8_t track,tone_program_control_t*out_program)
{if(track>=SEQ_LANE_CAPACITY||out_program==NULL)return 0U;*out_program=g_tone_program[track];return 1U;}
uint8_t tone_program_control_validate(const tone_program_control_t *program,
                                      track_runtime_type_t expected_type)
{
 if(program==NULL||program->tag!=expected_type||expected_type==TRACK_RUNTIME_TYPE_FM)return 0U;
 const uint8_t count=tone_param_codec_count(expected_type);
 if((count==0U)&&(expected_type!=TRACK_RUNTIME_TYPE_NONE)&&(expected_type!=TRACK_RUNTIME_TYPE_GROUP))return 0U;
 tone_program_control_t copy=*program;
 for(uint8_t slot=0U;slot<count;++slot){param_id_t id;float*f;
  if(!tone_param_codec_slot_to_param(expected_type,slot,&id)||(f=tone_field(&copy,id))==NULL
      ||!isfinite(*f)||*f<param_registry[id].min||*f>param_registry[id].max)return 0U;}
 return 1U;
}
uint8_t tone_program_control_restore(uint8_t track,const tone_program_control_t*program)
{
 if(track>=SEQ_LANE_CAPACITY||program==NULL||tone_program_control_validate(program,program->tag)==0U)return 0U;
 tone_program_control_t prepared=*program;
 live_parameter_audio_bulk_t bulk={.capture_tick=0U,
  .count=0U};
 const uint8_t count=tone_param_codec_count(prepared.tag);
 for(uint8_t slot=0U;slot<count;++slot){param_id_t id;float*value;
  param_registry_prepared_value_t canonical;
  if(!tone_param_codec_slot_to_param(prepared.tag,slot,&id)
      ||(value=tone_field(&prepared,id))==NULL
      ||!param_registry_prepare_value(id,*value,&canonical))return 0U;
  *value=canonical.value;
  if(param_registry_track_value_is_audio_command(id,track)==0U)continue;
  bulk.item[bulk.count++]=(live_parameter_audio_bulk_item_t){
   .parameter_id=(uint16_t)id,.scope=LIVE_PARAMETER_EVENT_SCOPE_TRACK,
   .track=track,.slot=LIVE_PARAMETER_EVENT_INVALID_INDEX,
   .flags=LIVE_PARAMETER_EVENT_FLAG_VALUE_FLOAT_BITS,
   .value=live_parameter_event_encode_float(*value)};}
 if((bulk.count!=0U)&&!live_parameter_audio_publication_submit_bulk_now(&bulk))return 0U;
 g_tone_program[track]=prepared;
 return 1U;
}
