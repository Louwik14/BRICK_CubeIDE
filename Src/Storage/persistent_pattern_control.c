#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_entity_topology.h"
#include "Storage/persistent_key_catalog.h"
#include "Core/control_routing.h"
#include "Core/live_parameter_migration.h"
#include "IPC/live_parameter_audio_publication.h"
#include "Track/entity_topology.h"
#include "Track/track_input_ownership.h"
#include "Track/track_mute.h"
#include "Track/track_runtime.h"
#include "Track/track_sound_state.h"
#include "Track/track_state.h"
#include "Mod/mod_destination_catalog.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_lfo_v1.h"
#include "Mod/mod_matrix.h"
#include "NoteFx/note_fx_pipeline.h"
#include "NoteFx/note_fx_state.h"
#include "Param/param_registry.h"
#include "Param/param_registry_runtime_state.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#define SEQ_RUNTIME_INTERNAL_USE 1
#include "Seq/seq_play_scheduler.h"
#include "UI/ui_active_track_sync.h"
#include "UI/ui_track_catalog.h"
#include "Storage/undo_v2.h"
#include <math.h>
#include <string.h>

static uint8_t capture_play(uint8_t entity,uint8_t step,persist_control_step_t*out){uint8_t cap=seq_model_play_capacity(entity);out->play_count=0U;for(uint8_t v=0U;v<cap;++v){persist_control_play_item_t*p=&out->play[v];int16_t x;if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_NOTE,&x)!=0U){p->note=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_NOTE;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_VELOCITY,&x)!=0U){p->velocity=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_VELOCITY;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_LENGTH,&x)!=0U){p->length=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_LENGTH;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_MICROTIMING,&x)!=0U){p->microtiming=(int8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_MICROTIMING;}if(p->present_mask!=0U)out->play_count=(uint8_t)(v+1U);}return 1U;}
static uint8_t capture_plock_value(param_id_t id,seq_value16_t raw,persist_control_step_lock_t*out){const float value=seq_param_iface_decode_param_value(id,raw);out->kind=PERSIST_VALUE_FLOAT32;out->value.f32=value;if(id==PARAM_LFO1_SHAPE||id==PARAM_LFO2_SHAPE||id==PARAM_LFO3_SHAPE){out->kind=PERSIST_VALUE_U32;return persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)(value+0.5f),&out->value.u32);}if(id==PARAM_LFO1_TRIG||id==PARAM_LFO2_TRIG||id==PARAM_LFO3_TRIG){out->kind=PERSIST_VALUE_U32;return persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)(uint8_t)(value+0.5f),&out->value.u32);}if(id==PARAM_MIDI_FX_S1_MODEL||id==PARAM_MIDI_FX_S2_MODEL||id==PARAM_MIDI_FX_S3_MODEL){out->kind=PERSIST_VALUE_U32;return persist_key_note_fx_to_disk((note_fx_model_t)(uint8_t)(value+0.5f),&out->value.u32);}return 1U;}
static uint8_t capture_sequence(uint8_t entity,persist_control_entity_t*out){out->sequence.length=seq_model_get_track_length(entity);(void)seq_runtime_get_track_div(entity,&out->sequence.division);(void)seq_runtime_get_track_quant(entity,&out->sequence.quantization);(void)seq_runtime_get_track_swing(entity,&out->sequence.swing);for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){persist_control_step_t*step=&out->sequence.steps[s];step->trigger=seq_model_get_trig(entity,s);step->roll=seq_model_get_step_roll(entity,s);if(capture_play(entity,s,step)==0U)return 0U;step->lock_count=seq_model_step_param_plock_count(entity,s);for(uint8_t i=0U;i<step->lock_count;++i){seq_plock_entry_t raw;if(seq_model_step_param_plock_get_at(entity,s,i,&raw)==0U)return 0U;if(raw.set_id==SEQ_PLOCK_SET_TONE){if((raw.param_slot>=SEQ_PARAM_TONE_SLOT_COUNT)||persist_key_tone_slot_to_disk(raw.param_slot,&step->locks[i].parameter)==0U)return 0U;step->locks[i].kind=PERSIST_VALUE_FLOAT32;step->locks[i].value.f32=(float)raw.value16/65535.0f;}else{param_id_t id;if(seq_param_iface_slot_to_param(entity,raw.set_id,raw.param_slot,&id)==0U||persist_key_param_to_disk(id,&step->locks[i].parameter)==0U||capture_plock_value(id,raw.value16,&step->locks[i])==0U)return 0U;}step->locks[i].flags=raw.flags;}}return 1U;}
static uint8_t capture_note_fx(uint8_t entity,const persist_entity_caps_t*caps,persist_control_entity_t*out){if(caps==NULL||caps->note_fx_owner==0U){out->note_fx_count=0U;return(caps!=NULL)?1U:0U;}note_fx_track_state_t state;if(note_fx_state_capture_track(entity,&state)==0U)return 0U;out->note_fx_count=PERSIST_CONTROL_NOTE_FX_COUNT;for(uint8_t slot=0U;slot<PERSIST_CONTROL_NOTE_FX_COUNT;++slot){if(persist_key_note_fx_to_disk((note_fx_model_t)state.value[slot][3U],&out->note_fx[slot].model_key)==0U)return 0U;memcpy(out->note_fx[slot].values,state.value[slot],PERSIST_CONTROL_NOTE_FX_VALUE_COUNT);}return 1U;}
static uint8_t capture_one_param(uint8_t entity,persist_control_entity_t*out,param_id_t id){persist_param_descriptor_t d;if(track_runtime_get_param_rule(id).domain==TRACK_RUNTIME_PARAM_DOMAIN_TONE)return 1U;if(persist_key_param_descriptor(id,&d)==0U||d.persistent==0U||d.scope!=PERSIST_PARAM_SCOPE_ENTITY||track_runtime_get_effective_param_status(entity,id)!=TRACK_RUNTIME_PARAM_ALLOWED)return 1U;float value;if(param_registry_get_track_value(id,entity,&value)==0U)return 0U;if(out->parameter_count>=PERSIST_CONTROL_ENTITY_PARAM_COUNT)return 0U;persist_control_parameter_t*p=&out->parameters[out->parameter_count++];p->key=d.key;p->kind=d.kind;p->value.f32=value;return 1U;}
static uint8_t capture_params(uint8_t entity,persist_control_entity_t*out){for(uint8_t slot=0U;slot<SEQ_PARAM_TONE_SLOT_COUNT;++slot){float value;if((out->parameter_count>=PERSIST_CONTROL_ENTITY_PARAM_COUNT)||(param_registry_control_tone_get(entity,slot,&value)==0U))return 0U;persist_control_parameter_t*p=&out->parameters[out->parameter_count++];if(persist_key_tone_slot_to_disk(slot,&p->key)==0U)return 0U;p->kind=PERSIST_VALUE_FLOAT32;p->value.f32=value;}for(uint8_t order=0U;order<14U;++order)if(capture_one_param(entity,out,param_registry_get_audio_fx_param(order))==0U)return 0U;for(param_id_t id=0U;id<PARAM_COUNT;++id)if(param_registry_is_audio_fx_param(id)==0U&&capture_one_param(entity,out,id)==0U)return 0U;return 1U;}
static uint8_t capture_mod(uint8_t owner,persist_control_modulation_t*out)
{
    const track_sound_state_t*s=track_sound_state_get_const(owner);
    if(s==NULL)return 0U;
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_LFO_COUNT;++i){float shape=0.0f,trig=0.0f;const param_id_t base=(param_id_t)(PARAM_LFO1_RATE+i*4U);if(param_registry_get_track_value(base,owner,&out->lfos[i].rate)==0U||param_registry_get_track_value((param_id_t)(base+1U),owner,&shape)==0U||param_registry_get_track_value((param_id_t)(base+2U),owner,&trig)==0U||param_registry_get_track_value((param_id_t)(base+3U),owner,&out->lfos[i].phase_offset)==0U||persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)shape,&out->lfos[i].shape_key)==0U||persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)(uint8_t)trig,&out->lfos[i].trigger_key)==0U)return 0U;}
    for(uint8_t i=0U;i<2U;++i){float a=0.0f,b=0.0f,source=0.0f;const param_id_t multi=(param_id_t)(PARAM_MOD_MULTI_1_A+i*2U);const param_id_t slew=(param_id_t)(PARAM_MOD_SLEW_1_SOURCE+i*2U);if(param_registry_get_track_value(multi,owner,&a)==0U||param_registry_get_track_value((param_id_t)(multi+1U),owner,&b)==0U||param_registry_get_track_value(slew,owner,&source)==0U||param_registry_get_track_value((param_id_t)(slew+1U),owner,&out->slew[i].amount)==0U||persist_key_mod_source_to_disk((uint8_t)a,&out->multi[i].source_a_key)==0U||persist_key_mod_source_to_disk((uint8_t)b,&out->multi[i].source_b_key)==0U||persist_key_mod_source_to_disk((uint8_t)source,&out->slew[i].source_key)==0U)return 0U;}
    float retrig=0.0f; if(param_registry_get_track_value(PARAM_ENV3_ATTACK,owner,&out->envelope.attack)==0U||param_registry_get_track_value(PARAM_ENV3_DECAY,owner,&out->envelope.decay)==0U||param_registry_get_track_value(PARAM_ENV3_SUSTAIN,owner,&out->envelope.sustain)==0U||param_registry_get_track_value(PARAM_ENV3_RELEASE,owner,&out->envelope.release)==0U||param_registry_get_track_value(PARAM_ENV_RETRIG_MOD,owner,&retrig)==0U)return 0U;out->envelope.retrigger_hard=(uint8_t)(retrig>=0.5f);
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_ROUTE_COUNT;++i){const track_mod_matrix_slot_t*r=&s->mod_matrix[i];persist_control_mod_route_t*d=&out->routes[i];if(persist_key_mod_source_to_disk(r->source,&d->source_key)==0U)return 0U;uint8_t entity;param_id_t param;if(r->destination==MOD_DESTINATION_NONE){d->destination_entity=owner;d->destination_parameter=PERSIST_CONTROL_KEY_NONE;d->enabled=0U;}else if(mod_destination_address_resolve(r->destination,&entity,&param)==0U||persist_key_mod_destination_to_disk(entity,param,&d->destination_entity,&d->destination_parameter)==0U)return 0U;d->depth=r->depth;d->enabled=r->enabled;}
    return 1U;
}

persist_codec_result_t persistent_pattern_control_capture(persist_control_pattern_t*out){if(out==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;memset(out,0,sizeof(*out));const uint8_t group_active=entity_topology_group_is_active();for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){persist_control_entity_t*d=&out->entities[e];persist_entity_caps_t caps;d->entity_id=e;if(persist_entity_caps_resolve(group_active,e,&caps)==0U||caps.persistable==0U)return PERSIST_CODEC_INVALID_ENTITY;if(persist_key_family_to_disk(track_state_get_family(e),&d->family)==0U||persist_key_type_to_disk(track_state_get_type(e),&d->type)==0U||persist_key_midi_source_to_disk(track_state_get_midi_source(e),&d->midi_source_key)==0U||persist_key_input_to_disk((caps.input_owner!=0U&&e<UI_TRACK_COUNT)?track_state_get_external_input(e):0U,&d->input_key)==0U)return PERSIST_CODEC_UNKNOWN_KEY;d->midi_channel=track_state_get_midi_channel(e);d->sequence.length=1U;d->sequence.division=1U;if(caps.active==0U)continue;d->muted=(d->family==PERSIST_FAMILY_OFF)?0U:track_mute_get(e);if(capture_params(e,d)==0U||capture_note_fx(e,&caps,d)==0U||capture_sequence(e,d)==0U)return PERSIST_CODEC_INVALID_ENTITY;if(caps.modulation_owner!=0U){d->modulation_present=1U;if(capture_mod(e,&d->modulation)==0U)return PERSIST_CODEC_INVALID_MODULATION;}}for(uint8_t dst=0U;dst<PERSIST_CONTROL_ENTITY_COUNT;++dst)for(uint8_t src=0U;src<PERSIST_CONTROL_ENTITY_COUNT;++src)if(control_routing_get_looper_source(dst,src)!=0U){persist_entity_caps_t dst_caps,src_caps;if(persist_entity_caps_resolve(group_active,dst,&dst_caps)==0U||persist_entity_caps_resolve(group_active,src,&src_caps)==0U||dst_caps.active==0U||src_caps.active==0U)return PERSIST_CODEC_INVALID_ENTITY;persist_control_route_t*r=&out->routes[out->route_count++];r->kind=PERSIST_ROUTE_LOOPER_SOURCE;r->source=src;r->destination=dst;r->enabled=1U;}out->globals.tempo_milli_bpm=seq_runtime_get_tempo_bpm_milli();if(persist_key_clock_to_disk(seq_runtime_get_clock_source(),&out->globals.clock_source_key)==0U||persist_key_record_start_to_disk(seq_runtime_get_rec_start_mode(),&out->globals.record_start_key)==0U||persist_key_record_length_to_disk(seq_runtime_get_rec_len_mode(),&out->globals.record_length_key)==0U)return PERSIST_CODEC_UNKNOWN_KEY;for(param_id_t id=0U;id<PARAM_COUNT;++id){persist_param_descriptor_t d;if(persist_key_param_descriptor(id,&d)==0U||d.persistent==0U||d.scope!=PERSIST_PARAM_SCOPE_GLOBAL)continue;if(out->globals.parameter_count>=PERSIST_CONTROL_GLOBAL_PARAM_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;persist_control_parameter_t*p=&out->globals.parameters[out->globals.parameter_count++];p->key=d.key;p->kind=d.kind;p->value.f32=param_get(id);}return persist_codec_validate_pattern(out);}

persist_codec_result_t persistent_pattern_control_validate(const persist_control_pattern_t*p)
{
    persist_codec_result_t r=persist_codec_validate_pattern(p);if(r!=PERSIST_CODEC_OK)return r;
    const uint8_t active=(p->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type==PERSIST_TYPE_GROUP)?1U:0U;
    ui_track_config_t cfg[BRICK_ENTITY_CAPACITY];uint8_t inputs[UI_TRACK_COUNT];
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){if(persist_key_family_from_disk(p->entities[e].family,&cfg[e].family)==0U||persist_key_type_from_disk(p->entities[e].type,&cfg[e].type)==0U)return PERSIST_CODEC_INVALID_ENTITY;if(e<UI_TRACK_COUNT&&persist_key_input_from_disk(p->entities[e].input_key,&inputs[e])==0U)return PERSIST_CODEC_INVALID_ENTITY;}
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){persist_entity_caps_t caps;if(persist_entity_caps_resolve(active,e,&caps)==0U)return PERSIST_CODEC_INVALID_ENTITY;if(cfg[e].family==UI_TRACK_FAMILY_OFF){if(cfg[e].type!=UI_TRACK_TYPE_NONE)return PERSIST_CODEC_INVALID_ENTITY;}else if(ui_track_catalog_type_is_valid_for_family(cfg[e].family,cfg[e].type)==false)return PERSIST_CODEC_INVALID_ENTITY;if(caps.active==0U&&cfg[e].family!=UI_TRACK_FAMILY_OFF&&ui_track_catalog_family_is_engine(cfg[e].family)==false)return PERSIST_CODEC_INVALID_ENTITY;if(caps.active!=0U&&cfg[e].family!=UI_TRACK_FAMILY_OFF&&ui_track_catalog_type_is_available(e,cfg[e].family,cfg[e].type,cfg)==false)return PERSIST_CODEC_INVALID_ENTITY;}
    return(track_input_ownership_validate_bulk(cfg,inputs)!=0U)?PERSIST_CODEC_OK:PERSIST_CODEC_INVALID_ENTITY;
}

static uint8_t apply_plock_value(param_id_t id,const persist_control_step_lock_t*lock,seq_value16_t*out){float value;if(lock->kind==PERSIST_VALUE_FLOAT32)value=lock->value.f32;else if(id==PARAM_LFO1_SHAPE||id==PARAM_LFO2_SHAPE||id==PARAM_LFO3_SHAPE){mod_lfo_shape_t v;if(persist_key_lfo_shape_from_disk(lock->value.u32,&v)==0U)return 0U;value=(float)v;}else if(id==PARAM_LFO1_TRIG||id==PARAM_LFO2_TRIG||id==PARAM_LFO3_TRIG){mod_lfo_trig_mode_t v;if(persist_key_lfo_trigger_from_disk(lock->value.u32,&v)==0U)return 0U;value=(float)v;}else if(id==PARAM_MIDI_FX_S1_MODEL||id==PARAM_MIDI_FX_S2_MODEL||id==PARAM_MIDI_FX_S3_MODEL){note_fx_model_t v;if(persist_key_note_fx_from_disk(lock->value.u32,&v)==0U)return 0U;value=(float)v;}else return 0U;*out=seq_param_iface_encode_param_value(id,value);return 1U;}
static uint8_t persistent_sequence_changed(uint8_t e, const persist_control_entity_t *x)
{
    if ((e >= SEQ_LANE_CAPACITY) || (x == NULL)) return 1U;
    uint8_t value = 0U;
    if (seq_model_get_track_length(e) != x->sequence.length
            || seq_runtime_get_track_div(e, &value) == 0U || value != x->sequence.division
            || seq_runtime_get_track_quant(e, &value) == 0U || value != x->sequence.quantization
            || seq_runtime_get_track_swing(e, &value) == 0U || value != x->sequence.swing) return 1U;
    for (uint8_t s = 0U; s < PERSIST_CONTROL_STEP_COUNT; ++s)
    {
        const persist_control_step_t *st = &x->sequence.steps[s];
        if ((seq_model_get_trig(e, s) != st->trigger) || (seq_model_get_step_roll(e, s) != st->roll)) return 1U;
        for (uint8_t v = 0U; v < SEQ_PLAY_MAX_CAPACITY; ++v)
        {
            const persist_control_play_item_t *p = &st->play[v];
            const uint8_t masks[] = { SEQ_STEP_PLAY_FIELD_NOTE, SEQ_STEP_PLAY_FIELD_VELOCITY, SEQ_STEP_PLAY_FIELD_LENGTH, SEQ_STEP_PLAY_FIELD_MICROTIMING };
            const int16_t expected[] = { p->note, p->velocity, p->length, p->microtiming };
            for (uint8_t f = 0U; f < 4U; ++f)
            {
                int16_t current = 0;
                const uint8_t present = seq_model_play_get(e, s, v, (seq_step_play_field_t)masks[f], &current);
                if (present != ((p->present_mask >> f) & 1U) || (present != 0U && current != expected[f])) return 1U;
            }
        }
        if (seq_model_step_param_plock_count(e, s) != st->lock_count) return 1U;
        for (uint8_t i = 0U; i < st->lock_count; ++i)
        {
            seq_plock_entry_t raw;
            uint8_t tone_slot;
            param_id_t target_id;
            param_id_t current_id;
            seq_value16_t encoded;
            if (seq_model_step_param_plock_get_at(e, s, i, &raw) == 0U) return 1U;
            if (persist_key_tone_slot_from_disk(st->locks[i].parameter, &tone_slot) != 0U)
            {
                encoded = (seq_value16_t)(st->locks[i].value.f32 * 65535.0f + 0.5f);
                if ((raw.set_id != SEQ_PLOCK_SET_TONE) || (raw.param_slot != tone_slot)
                        || (raw.flags != st->locks[i].flags) || (raw.value16 != encoded)) return 1U;
                continue;
            }
            if ((persist_key_param_from_disk(st->locks[i].parameter, &target_id) == 0U)
                    || (seq_param_iface_slot_to_param(e, raw.set_id, raw.param_slot, &current_id) == 0U)
                    || (current_id != target_id)
                    || (apply_plock_value(target_id, &st->locks[i], &encoded) == 0U)
                    || (raw.flags != st->locks[i].flags) || (raw.value16 != encoded)) return 1U;
        }
    }
    return 0U;
}
static uint8_t apply_sequence(uint8_t e,const persist_control_entity_t*x){seq_model_set_track_length(e,x->sequence.length);seq_runtime_set_track_div(e,x->sequence.division);seq_runtime_set_track_quant(e,x->sequence.quantization);seq_runtime_set_track_swing(e,x->sequence.swing);for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){const persist_control_step_t*st=&x->sequence.steps[s];seq_model_set_trig(e,s,st->trigger);seq_model_set_step_roll(e,s,st->roll);seq_model_play_clear_step(e,s);seq_model_step_param_plock_clear(e,s);for(uint8_t v=0U;v<st->play_count;++v){const persist_control_play_item_t*p=&st->play[v];if(((p->present_mask&SEQ_STEP_PLAY_PRESENT_NOTE)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_NOTE,p->note)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_VELOCITY)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_VELOCITY,p->velocity)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_LENGTH)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_LENGTH,p->length)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_MICROTIMING)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_MICROTIMING,p->microtiming)==0U))return 0U;}for(uint8_t i=0U;i<st->lock_count;++i){param_id_t id;uint8_t set;seq_param_slot_t slot;uint8_t found=0U;seq_value16_t value;if(persist_key_tone_slot_from_disk(st->locks[i].parameter,&slot)!=0U){set=SEQ_PLOCK_SET_TONE;value=(seq_value16_t)(st->locks[i].value.f32*65535.0f+0.5f);found=1U;}else{if(persist_key_param_from_disk(st->locks[i].parameter,&id)==0U||apply_plock_value(id,&st->locks[i],&value)==0U)return 0U;for(set=0U;set<SEQ_PLOCK_SET_COUNT;++set)if(seq_param_iface_param_to_slot(e,set,id,&slot)!=0U){found=1U;break;}}if(found==0U)return 0U;seq_plock_op_status_t status=seq_model_step_plock_upsert(e,s,set,slot,value,st->locks[i].flags);if(status!=SEQ_PLOCK_OP_CREATED&&status!=SEQ_PLOCK_OP_UPDATED)return 0U;}}return 1U;}
static uint8_t apply_note_fx(uint8_t e,uint8_t active,const persist_control_entity_t*x){persist_entity_caps_t caps;if(persist_entity_caps_resolve(active,e,&caps)==0U)return 0U;if(caps.note_fx_owner==0U)return x->note_fx_count==0U;note_fx_track_state_t state;memset(&state,0,sizeof(state));for(uint8_t slot=0U;slot<x->note_fx_count;++slot){note_fx_model_t model;if(persist_key_note_fx_from_disk(x->note_fx[slot].model_key,&model)==0U)return 0U;memcpy(state.value[slot],x->note_fx[slot].values,NOTE_FX_PARAM_COUNT);state.value[slot][3U]=(uint8_t)model;}if(note_fx_state_restore_track(e,&state)==0U)return 0U;return note_fx_pipeline_configure_track(e);}
static uint8_t apply_track_param(uint8_t entity,param_id_t id,float value)
{
    param_registry_prepared_value_t prepared;
    float current=0.0f;
    if(param_registry_prepare_value(id,value,&prepared)==0U)return 0U;
    if(param_registry_get_track_value(id,entity,&current)!=0U
            && current==prepared.value)return 1U;
    return param_registry_apply_track_value(id,entity,value);
}
static uint8_t apply_one_entity_param(uint8_t entity,const persist_control_entity_t*x,param_id_t wanted){for(uint16_t i=0U;i<x->parameter_count;++i){uint8_t tone_slot;param_id_t id;if(persist_key_tone_slot_from_disk(x->parameters[i].key,&tone_slot)!=0U)continue;if(persist_key_param_from_disk(x->parameters[i].key,&id)==0U)return 0U;if(id==wanted){if(track_runtime_get_effective_param_status(entity,id)!=TRACK_RUNTIME_PARAM_ALLOWED)return 1U;return apply_track_param(entity,id,x->parameters[i].value.f32);}}return 1U;}
static uint8_t apply_entity_tone(uint8_t entity,
                                      const persist_control_entity_t *x,
                                      uint8_t *has_canonical)
{
    float values[SEQ_PARAM_TONE_SLOT_COUNT];
    *has_canonical = 0U;
    for (uint8_t slot = 0U; slot < SEQ_PARAM_TONE_SLOT_COUNT; ++slot)
        if (param_registry_control_tone_get(entity, slot, &values[slot]) == 0U)
            return 0U;
    for (uint16_t i = 0U; i < x->parameter_count; ++i)
    {
        uint8_t slot;
        if (persist_key_tone_slot_from_disk(x->parameters[i].key, &slot) == 0U)
            continue;
        *has_canonical = 1U;
        values[slot] = x->parameters[i].value.f32;
        if (param_registry_control_tone_set(entity, slot, values[slot]) == 0U)
            return 0U;
    }
    return (*has_canonical == 0U)
        || live_parameter_audio_publication_submit_tone_state(entity, values);
}
static uint8_t apply_entity_params(uint8_t entity,const persist_control_entity_t*x){uint8_t has_canonical;if(apply_entity_tone(entity,x,&has_canonical)==0U)return 0U;for(uint8_t order=0U;order<14U;++order)if(apply_one_entity_param(entity,x,param_registry_get_audio_fx_param(order))==0U)return 0U;for(uint16_t i=0U;i<x->parameter_count;++i){uint8_t tone_slot;param_id_t id;if(persist_key_tone_slot_from_disk(x->parameters[i].key,&tone_slot)!=0U)continue;if(persist_key_param_from_disk(x->parameters[i].key,&id)==0U)return 0U;if(param_registry_is_audio_fx_param(id)!=0U)continue;if(has_canonical!=0U&&track_runtime_get_param_rule(id).domain==TRACK_RUNTIME_PARAM_DOMAIN_TONE)continue;if(apply_track_param(entity,id,x->parameters[i].value.f32)==0U)return 0U;}return 1U;}
static uint8_t apply_mod(uint8_t owner,uint8_t active,const persist_control_modulation_t*m){for(uint8_t i=0U;i<3U;++i){mod_lfo_shape_t shape;mod_lfo_trig_mode_t trig;const param_id_t base=(param_id_t)(PARAM_LFO1_RATE+(i*4U));if(persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&shape)==0U||persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&trig)==0U||apply_track_param(owner,base,m->lfos[i].rate)==0U||apply_track_param(owner,(param_id_t)(base+1U),(float)shape)==0U||apply_track_param(owner,(param_id_t)(base+2U),(float)trig)==0U||apply_track_param(owner,(param_id_t)(base+3U),m->lfos[i].phase_offset)==0U)return 0U;}for(uint8_t i=0U;i<2U;++i){uint8_t a,b,s;if(persist_key_mod_source_from_disk(m->multi[i].source_a_key,&a)==0U||persist_key_mod_source_from_disk(m->multi[i].source_b_key,&b)==0U||persist_key_mod_source_from_disk(m->slew[i].source_key,&s)==0U||mod_matrix_set_multi_source(owner,i,0U,(float)a)==0U||mod_matrix_set_multi_source(owner,i,1U,(float)b)==0U||mod_matrix_set_slew_source(owner,i,(float)s)==0U||mod_matrix_set_slew_amount(owner,i,m->slew[i].amount)==0U)return 0U;}if(apply_track_param(owner,PARAM_ENV3_ATTACK,m->envelope.attack)==0U||apply_track_param(owner,PARAM_ENV3_DECAY,m->envelope.decay)==0U||apply_track_param(owner,PARAM_ENV3_SUSTAIN,m->envelope.sustain)==0U||apply_track_param(owner,PARAM_ENV3_RELEASE,m->envelope.release)==0U||apply_track_param(owner,PARAM_ENV_RETRIG_MOD,(float)m->envelope.retrigger_hard)==0U)return 0U;for(uint8_t i=0U;i<8U;++i){const persist_control_mod_route_t*r=&m->routes[i];mod_destination_address_t address=MOD_DESTINATION_NONE;uint8_t source;if(persist_key_mod_source_from_disk(r->source_key,&source)==0U)return 0U;if(r->destination_parameter!=PERSIST_CONTROL_KEY_NONE){uint8_t entity;param_id_t param;if(persist_key_mod_destination_from_disk(r->destination_entity,r->destination_parameter,active,&entity,&param)==0U)return 0U;address=mod_destination_address_make(entity,param);}if(mod_matrix_set_slot_state(owner,i,source,address,r->depth,r->enabled)==0U)return 0U;}return 1U;}

persist_codec_result_t persistent_pattern_control_apply(
    const persist_control_pattern_t *pattern, uint8_t resume_transport)
{
    persist_codec_result_t result = persistent_pattern_control_validate(pattern);
    if (result != PERSIST_CODEC_OK)
        return result;

    const uint8_t group_active =
        (pattern->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type
            == PERSIST_TYPE_GROUP) ? 1U : 0U;
    uint8_t families[BRICK_ENTITY_CAPACITY];
    uint8_t types[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channels[BRICK_ENTITY_CAPACITY];
    uint8_t midi_sources[BRICK_ENTITY_CAPACITY];
    uint8_t inputs[UI_TRACK_COUNT];
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        ui_track_family_t family;
        ui_track_type_t type;
        ui_track_midi_source_t midi_source;
        (void)persist_key_family_from_disk(
            pattern->entities[entity].family, &family);
        (void)persist_key_type_from_disk(
            pattern->entities[entity].type, &type);
        (void)persist_key_midi_source_from_disk(
            pattern->entities[entity].midi_source_key, &midi_source);
        families[entity] = (uint8_t)family;
        types[entity] = (uint8_t)type;
        midi_channels[entity] = pattern->entities[entity].midi_channel;
        midi_sources[entity] = (uint8_t)midi_source;
        if (entity < UI_TRACK_COUNT)
            (void)persist_key_input_from_disk(
                pattern->entities[entity].input_key, &inputs[entity]);
    }

    const uint8_t current_group_active = entity_topology_group_is_active();
    for (uint8_t entity = 0U;
         entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
    {
        entity_topology_descriptor_t current_topology;
        entity_topology_descriptor_t target_topology;
        if ((entity_topology_resolve(current_group_active, entity,
                &current_topology) == 0U)
                || (current_topology.active == 0U))
            continue;
        if ((entity_topology_resolve(group_active, entity,
                &target_topology) == 0U)
                || (target_topology.active == 0U)
                || (persistent_sequence_changed(
                    entity, &pattern->entities[entity]) != 0U))
            seq_play_scheduler_notify_track_pattern_change(entity);
    }

    if (track_structure_apply_entity_bulk_with_inputs(
            families, types, midi_channels, midi_sources, inputs) == 0U)
        return PERSIST_CODEC_INVALID_ENTITY;

    param_registry_batch_begin();
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        (void)entity_topology_resolve(group_active, entity, &topology);
        if (topology.active == 0U)
            continue;
        const persist_control_entity_t *const saved =
            &pattern->entities[entity];
        if (apply_entity_params(entity, saved) == 0U)
        {
            param_registry_batch_end();
            return PERSIST_CODEC_UNKNOWN_KEY;
        }
        if ((track_mute_set(entity, saved->muted) == 0U)
                || (apply_sequence(entity, saved) == 0U)
                || (apply_note_fx(entity, group_active, saved) == 0U))
        {
            param_registry_batch_end();
            return PERSIST_CODEC_INVALID_ENTITY;
        }
    }
    for (uint16_t index = 0U;
         index < pattern->globals.parameter_count; ++index)
    {
        const persist_control_parameter_t *const saved =
            &pattern->globals.parameters[index];
        param_id_t id;
        if (persist_key_param_from_disk(saved->key, &id) == 0U)
        {
            param_registry_batch_end();
            return PERSIST_CODEC_UNKNOWN_KEY;
        }
        param_registry_prepared_value_t prepared;
        if (param_registry_prepare_value(id, saved->value.f32, &prepared) == 0U)
        {
            param_registry_batch_end();
            return PERSIST_CODEC_INVALID_ENTITY;
        }
        if (param_get(id) != prepared.value)
            param_set(id, prepared.value);
    }
    param_registry_batch_end();

    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        (void)entity_topology_resolve(group_active, entity, &topology);
        if (topology.active == 0U)
            continue;
        if ((pattern->entities[entity].modulation_present != 0U)
                && (apply_mod(entity, group_active,
                    &pattern->entities[entity].modulation) == 0U))
            return PERSIST_CODEC_INVALID_MODULATION;
    }

    uint8_t routes[BRICK_ENTITY_CAPACITY][BRICK_ENTITY_CAPACITY] = {0};
    for (uint16_t index = 0U; index < pattern->route_count; ++index)
    {
        const persist_control_route_t *const route = &pattern->routes[index];
        if ((route->destination >= BRICK_ENTITY_CAPACITY)
                || (route->source >= BRICK_ENTITY_CAPACITY)
                || (route->destination == route->source))
            return PERSIST_CODEC_INVALID_ENTITY;
        routes[route->destination][route->source] =
            (route->enabled != 0U) ? 1U : 0U;
    }
    if (control_routing_apply_bulk(routes) == 0U)
        return PERSIST_CODEC_INVALID_ENTITY;

    seq_runtime_set_tempo_bpm_milli(pattern->globals.tempo_milli_bpm);
    seq_clock_src_t clock;
    uint8_t mode;
    if ((persist_key_clock_from_disk(
            pattern->globals.clock_source_key, &clock) == 0U)
            || (persist_key_record_start_from_disk(
                pattern->globals.record_start_key, &mode) == 0U))
        return PERSIST_CODEC_UNKNOWN_KEY;
    seq_runtime_set_clock_source(clock);
    seq_runtime_set_rec_start_mode(mode);
    if (persist_key_record_length_from_disk(
            pattern->globals.record_length_key, &mode) == 0U)
        return PERSIST_CODEC_UNKNOWN_KEY;
    seq_runtime_set_rec_len_mode(mode);
    ui_active_track_sync_full_after_global_restore();
    undo_v2_invalidate_history();
    (void)resume_transport;
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persistent_pattern_control_install_restored(
    const persist_control_pattern_t *pattern,uint8_t resume_transport)
{
    return persistent_pattern_control_apply(pattern,resume_transport);
}
