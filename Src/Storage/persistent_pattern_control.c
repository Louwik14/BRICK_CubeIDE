#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_entity_topology.h"
#include "Storage/persistent_key_catalog.h"
#include "App/live_parameter_audio_publication.h"
#include "Platform/brick_media_clock.h"
#include "IPC/live_parameter_event.h"
#include "IPC/control_audio_fifo_layout.h"
#include "main.h"
#include "ControlRT/audio_state_snapshot_control.h"
#include "Track/entity_topology.h"
#include "Track/track_input_ownership.h"
#include "Track/track_mute.h"
#include "Track/track_runtime.h"
#include "Track/track_sound_state.h"
#include "Track/track_state.h"
#include "Mod/mod_destination_contract.h"
#include "Mod/mod_destination_control.h"
#include "Mod/mod_env3.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Track/audio_fx_control_state.h"
#include "Track/fm_control_state.h"
#include "Track/polyphony_control.h"
#include "Track/synth_polyphony.h"
#include "NoteFx/note_fx_state.h"
#include "Seq/seq_engine.h"
#include "Param/param_filter.h"
#include "Param/param_value_policy.h"
#include "Param/param_global_control.h"
#include "Track/vca_control_state.h"
#include "Track/mixer_control_state.h"
#include "Mod/mod_env3_control.h"
#include "Track/tone_program_control.h"
#include "Seq/seq_model.h"
#include "Seq/seq_param_iface.h"
#include "Seq/seq_runtime.h"
#include "Seq/seq_runtime_control.h"
#include "Seq/metronome_control.h"
#include "Keyboard/keyboard_runtime.h"
#include "Keyboard/keyboard_params.h"
#include "Track/track_catalog.h"
#include "Storage/undo_v2.h"
#include "Storage/project_control.h"
#include "Storage/persistence_debug.h"
#include "Platform/brick_build_config.h"
#include "UI/ui_active_track_sync.h"
#include <math.h>
#include <string.h>

_Static_assert(PERSIST_CONTROL_ENTITY_COUNT == 16U,
               "AUDIO full-projection entity proof changed");
_Static_assert(PERSIST_CONTROL_ENTITY_COUNT == SEQ_LANE_CAPACITY,
               "persistence and SEQ lane capacities diverged");
_Static_assert(PERSIST_CONTROL_STEP_COUNT == SEQ_MAX_STEPS,
               "persistence and SEQ step capacities diverged");
_Static_assert(PERSIST_CONTROL_PLAY_ITEM_COUNT == SEQ_PLAY_MAX_CAPACITY,
               "persistence and SEQ PLAY capacities diverged");
_Static_assert(PERSIST_CONTROL_STEP_LOCK_COUNT == SEQ_STEP_MAX_LOCKS,
               "persistence and SEQ p-lock capacities diverged");
_Static_assert(PERSIST_CONTROL_NOTE_FX_BYTES == sizeof(note_fx_chain_state_t),
               "persistence and fixed Note FX chain diverged");
_Static_assert(PERSIST_CONTROL_MOD_LFO_COUNT == MOD_LFO_COUNT_PER_TRACK,
               "persistence and CONTROL LFO counts diverged");
_Static_assert(PERSIST_CONTROL_MOD_ROUTE_COUNT == MOD_MATRIX_SLOT_COUNT,
               "persistence and CONTROL modulation route counts diverged");

static uint8_t capture_play(uint8_t entity,uint8_t step,persist_control_step_t*out){uint8_t cap=seq_model_play_capacity(entity);out->play_count=0U;for(uint8_t v=0U;v<cap;++v){persist_control_play_item_t*p=&out->play[v];int16_t x;if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_NOTE,&x)!=0U){p->note=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_NOTE;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_VELOCITY,&x)!=0U){p->velocity=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_VELOCITY;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_LENGTH,&x)!=0U){p->length=(uint8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_LENGTH;}if(seq_model_play_get(entity,step,v,SEQ_STEP_PLAY_FIELD_MICROTIMING,&x)!=0U){p->microtiming=(int8_t)x;p->present_mask|=SEQ_STEP_PLAY_PRESENT_MICROTIMING;}if(p->present_mask!=0U)out->play_count=(uint8_t)(v+1U);}return 1U;}
static uint8_t capture_plock_value(param_id_t id, seq_value16_t raw,
                                   persist_control_step_lock_t *out)
{
    float value;
    if ((out == NULL)
            || (seq_param_iface_decode_param_value(id, raw, &value) == 0U))
        return 0U;
    out->kind = PERSIST_VALUE_FLOAT32;
    out->value.f32 = value;
    if ((id == PARAM_LFO1_SHAPE) || (id == PARAM_LFO2_SHAPE)
            || (id == PARAM_LFO3_SHAPE))
    {
        out->kind = PERSIST_VALUE_U32;
        return persist_key_lfo_shape_to_disk(
            (mod_lfo_shape_t)(uint8_t)(value + 0.5f), &out->value.u32);
    }
    if ((id == PARAM_LFO1_TRIG) || (id == PARAM_LFO2_TRIG)
            || (id == PARAM_LFO3_TRIG))
    {
        out->kind = PERSIST_VALUE_U32;
        return persist_key_lfo_trigger_to_disk(
            (mod_lfo_trig_mode_t)(uint8_t)(value + 0.5f), &out->value.u32);
    }
    return 1U;
}
static uint8_t capture_sequence(uint8_t entity,persist_control_entity_t*out){out->sequence.length=seq_model_get_track_length(entity);if(seq_runtime_get_track_div(entity,&out->sequence.division)==0U||seq_runtime_get_track_traversal(entity,&out->sequence.direction,&out->sequence.rotate)==0U||seq_runtime_get_track_timing(entity,&out->sequence.timing)==0U)return 0U;for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){persist_control_step_t*step=&out->sequence.steps[s];step->trigger=seq_model_get_trig(entity,s);step->roll=seq_model_get_step_roll(entity,s);if(capture_play(entity,s,step)==0U)return 0U;step->lock_count=seq_model_step_param_plock_count(entity,s);for(uint8_t i=0U;i<step->lock_count;++i){seq_plock_entry_t raw;if(seq_model_step_param_plock_get_at(entity,s,i,&raw)==0U)return 0U;if(raw.set_id==SEQ_PLOCK_SET_TONE){if((raw.param_slot>=SEQ_PARAM_TONE_SLOT_COUNT)||persist_key_tone_slot_to_disk(raw.param_slot,&step->locks[i].parameter)==0U)return 0U;step->locks[i].kind=PERSIST_VALUE_FLOAT32;step->locks[i].value.f32=(float)raw.value16/65535.0f;}else{param_id_t id;if(seq_param_iface_slot_to_param(entity,raw.set_id,raw.param_slot,&id)==0U||persist_key_param_to_disk(id,&step->locks[i].parameter)==0U||capture_plock_value(id,raw.value16,&step->locks[i])==0U)return 0U;}step->locks[i].flags=raw.flags;}}return 1U;}
static uint8_t capture_note_fx(uint8_t entity,const persist_entity_caps_t*caps,persist_control_entity_t*out){if(caps==NULL)return 0U;if(caps->note_fx_owner==0U){memset(&out->note_fx,0,sizeof(out->note_fx));return 1U;}return note_fx_chain_state_capture_track(entity,&out->note_fx);}
static uint8_t capture_product_state(uint8_t entity,persist_control_entity_t*out)
{
    const track_family_t family=track_state_get_family(entity);
    const track_type_t type=track_state_get_type(entity);
    if(family==TRACK_FAMILY_SAMPLER&&(type==TRACK_TYPE_STREAM||type==TRACK_TYPE_RAM||type==TRACK_TYPE_MULTI))
    {
        persist_control_asset_ref_t asset;
        if(project_control_track_asset_get(entity,PROJECT_CONTROL_ASSET_SAMPLER,&asset)!=0U)out->assets[out->asset_count++]=asset;
    }
    else if(family==TRACK_FAMILY_SYNTH&&type==TRACK_TYPE_WAVE)
    {
        for(uint8_t osc=0U;osc<2U;++osc){persist_control_asset_ref_t asset;if(project_control_track_asset_get(entity,(project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1+osc),&asset)!=0U)out->assets[out->asset_count++]=asset;}
    }
    else if(family==TRACK_FAMILY_SYNTH&&type==TRACK_TYPE_FM)
    {
        out->fm_present=1U;
        if(fm_control_state_get(entity,&out->fm)==0U)return 0U;
    }
    else
    {
        out->tone_present=1U;
        if(tone_program_control_capture(entity,&out->tone)==0U)return 0U;
    }
    if(param_filter_control_capture(entity,&out->filter)==0U
            ||vca_control_state_capture(entity,&out->vca)==0U
            ||mixer_control_state_capture(entity,&out->mixer)==0U
            ||audio_fx_control_state_capture(entity,&out->audio_fx)==0U
            ||polyphony_control_capture(entity,&out->polyphony)==0U)return 0U;
    return 1U;
}
static uint8_t capture_mod(uint8_t owner,persist_control_modulation_t*out)
{
    const track_sound_state_t*s=track_sound_state_get_const(owner);
    if(s==NULL)return 0U;
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_LFO_COUNT;++i){float shape=0.0f,trig=0.0f;if(mod_lfo_v1_get_track_param(owner,i,MOD_LFO_PARAM_RATE,&out->lfos[i].rate)==0U||mod_lfo_v1_get_track_param(owner,i,MOD_LFO_PARAM_SHAPE,&shape)==0U||mod_lfo_v1_get_track_param(owner,i,MOD_LFO_PARAM_TRIG,&trig)==0U||mod_lfo_v1_get_track_param(owner,i,MOD_LFO_PARAM_PHASE,&out->lfos[i].phase_offset)==0U||persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)shape,&out->lfos[i].shape_key)==0U||persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)(uint8_t)trig,&out->lfos[i].trigger_key)==0U)return 0U;}
    for(uint8_t i=0U;i<2U;++i){if(persist_key_mod_source_to_disk(s->mod_multi_source[i][0],&out->multi[i].source_a_key)==0U||persist_key_mod_source_to_disk(s->mod_multi_source[i][1],&out->multi[i].source_b_key)==0U||persist_key_mod_source_to_disk(s->mod_slew_source[i],&out->slew[i].source_key)==0U)return 0U;out->slew[i].amount=s->mod_slew_amount[i];}
    mod_env3_control_state_t env;if(mod_env3_control_capture(owner,&env)==0U)return 0U;out->envelope.attack=env.attack;out->envelope.decay=env.decay;out->envelope.sustain=env.sustain;out->envelope.release=env.release;out->envelope.retrigger_hard=(uint8_t)(env.retrigger>=0.5f);
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_ROUTE_COUNT;++i){const track_mod_matrix_slot_t*r=&s->mod_matrix[i];persist_control_mod_route_t*d=&out->routes[i];if(persist_key_mod_source_to_disk(r->source,&d->source_key)==0U)return 0U;uint8_t entity;param_id_t param;if(r->destination==MOD_DESTINATION_NONE){d->destination_entity=owner;d->destination_parameter=PERSIST_CONTROL_KEY_NONE;d->enabled=0U;}else if(mod_destination_address_resolve(r->destination,&entity,&param)==0U||persist_key_mod_destination_to_disk(entity,param,&d->destination_entity,&d->destination_parameter)==0U)return 0U;d->depth=r->depth;d->enabled=r->enabled;}
    return 1U;
}

static uint8_t build_default_product_state(track_family_t family,
                                           track_type_t type,
                                           persist_control_entity_t *out)
{
    if ((family == TRACK_FAMILY_SAMPLER)
            && ((type == TRACK_TYPE_STREAM) || (type == TRACK_TYPE_RAM)
                || (type == TRACK_TYPE_MULTI)))
    {
        /* A blank sampler has no asset reference. */
    }
    else if ((family == TRACK_FAMILY_SYNTH) && (type == TRACK_TYPE_WAVE))
    {
        /* A blank wavetable synth has no oscillator asset reference. */
    }
    else if ((family == TRACK_FAMILY_SYNTH) && (type == TRACK_TYPE_FM))
    {
        out->fm_present = 1U;
        fm_control_state_make_default(&out->fm);
    }
    else
    {
        out->tone_present = 1U;
        if (tone_program_control_make_default(track_runtime_type_from_ui(type),
                                              &out->tone) == 0U)
            return 0U;
    }
    param_filter_control_make_default(&out->filter);
    vca_control_state_make_default(&out->vca);
    mixer_control_state_make_default(&out->mixer);
    audio_fx_control_state_make_default(&out->audio_fx);
    polyphony_control_make_default(&out->polyphony);
    return 1U;
}

static uint8_t build_default_note_fx(persist_control_entity_t *out)
{
    note_fx_chain_state_make_default(&out->note_fx);
    return 1U;
}

static void build_default_sequence(persist_control_entity_t *out)
{
    seq_runtime_track_defaults_t defaults;
    seq_runtime_make_default_track_control(&defaults);
    out->sequence.length = SEQ_DEFAULT_LENGTH_STEPS;
    out->sequence.division = defaults.division;
    out->sequence.direction = defaults.direction;
    out->sequence.rotate = defaults.rotate;
    out->sequence.timing = defaults.timing;
}

static uint8_t build_default_modulation(uint8_t owner,
                                        persist_control_modulation_t *out)
{
    track_sound_state_t sound;
    mod_lfo_control_bank_t lfos;
    mod_env3_control_state_t envelope;
    memset(&sound, 0, sizeof(sound));
    track_sound_state_make_default(&sound);
    mod_lfo_v1_make_default(&lfos);
    mod_env3_control_make_default(&envelope);

    for (uint8_t i = 0U; i < PERSIST_CONTROL_MOD_LFO_COUNT; ++i)
    {
        out->lfos[i].rate = lfos.lfo[i].rate;
        out->lfos[i].phase_offset = lfos.lfo[i].phase;
        if ((persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)
                    lfos.lfo[i].shape, &out->lfos[i].shape_key) == 0U)
                || (persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)
                    (uint8_t)lfos.lfo[i].trigger,
                    &out->lfos[i].trigger_key) == 0U)) return 0U;
    }
    for (uint8_t i = 0U; i < 2U; ++i)
    {
        if ((persist_key_mod_source_to_disk(sound.mod_multi_source[i][0],
                    &out->multi[i].source_a_key) == 0U)
                || (persist_key_mod_source_to_disk(sound.mod_multi_source[i][1],
                    &out->multi[i].source_b_key) == 0U)
                || (persist_key_mod_source_to_disk(sound.mod_slew_source[i],
                    &out->slew[i].source_key) == 0U)) return 0U;
        out->slew[i].amount = sound.mod_slew_amount[i];
    }
    out->envelope = (persist_control_mod_envelope_t){
        envelope.attack, envelope.decay, envelope.sustain, envelope.release,
        (uint8_t)(envelope.retrigger >= 0.5f)
    };
    for (uint8_t i = 0U; i < PERSIST_CONTROL_MOD_ROUTE_COUNT; ++i)
    {
        if (persist_key_mod_source_to_disk(sound.mod_matrix[i].source,
                    &out->routes[i].source_key) == 0U) return 0U;
        out->routes[i].destination_entity = owner;
        out->routes[i].destination_parameter = PERSIST_CONTROL_KEY_NONE;
        out->routes[i].depth = sound.mod_matrix[i].depth;
        out->routes[i].enabled = 0U;
    }
    return 1U;
}

persist_codec_result_t persistent_pattern_control_build_defaults(
    persist_control_pattern_t *destination,
    const persistent_pattern_default_context_t *context)
{
    if ((destination == NULL) || (context == NULL))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    memset(destination, 0, sizeof(*destination));

    track_config_t group_master;
    track_state_make_initial_config(PERSIST_CONTROL_GROUP_MASTER_ID,
                                    &group_master);
    const uint8_t group_active = (uint8_t)(group_master.type
                                           == TRACK_TYPE_GROUP);
    for (uint8_t entity = 0U; entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
    {
        persist_control_entity_t *const out = &destination->entities[entity];
        persist_entity_caps_t caps;
        track_config_t config;
        track_midi_source_t midi_source;
        track_state_make_initial_config(entity, &config);
        track_state_make_initial_midi(entity, &out->midi_channel,
                                      &midi_source);
        out->entity_id = entity;
        out->sequence.length = 1U;
        out->sequence.division = 1U;
        polyphony_control_make_default(&out->polyphony);
        if ((persist_entity_caps_resolve(group_active, entity, &caps) == 0U)
                || (persist_key_family_to_disk(config.family,
                    &out->family) == 0U)
                || (persist_key_type_to_disk(config.type, &out->type) == 0U)
                || (persist_key_midi_source_to_disk(midi_source,
                    &out->midi_source_key) == 0U)
                || (persist_key_input_to_disk(
                    (caps.input_owner && (entity < TRACK_COUNT))
                        ? track_input_ownership_initial_input(entity) : 0U,
                    &out->input_key) == 0U))
            return PERSIST_CODEC_INVALID_ENTITY;
        if (caps.active == 0U) continue;
        if (build_default_product_state(config.family, config.type, out) == 0U)
            return PERSIST_CODEC_INVALID_ENTITY;
        if ((caps.note_fx_owner != 0U) && (build_default_note_fx(out) == 0U))
            return PERSIST_CODEC_INVALID_ENTITY;
        if (caps.sequence_owner != 0U) build_default_sequence(out);
        if (caps.modulation_owner != 0U)
        {
            out->modulation_present = 1U;
            if (build_default_modulation(entity, &out->modulation) == 0U)
                return PERSIST_CODEC_INVALID_MODULATION;
        }
    }

    keyboard_params_state_t keyboard;
    keyboard_params_make_default(&keyboard);
    destination->globals.tempo_milli_bpm =
        SEQ_RUNTIME_DEFAULT_TEMPO_BPM_MILLI;
    destination->globals.keyboard = (persist_control_keyboard_t){
        keyboard.root_index, keyboard.scale_index, keyboard.omnichord ? 1U : 0U,
        (uint8_t)keyboard.note_order, keyboard.chord_override ? 1U : 0U,
        keyboard.mono_last ? 1U : 0U
    };
    destination->globals.groove_seed = (context->groove_seed != 0U)
        ? context->groove_seed : SEQ_RUNTIME_DEFAULT_GROOVE_SEED;
    destination->globals.audio = context->global_audio;
    if ((persist_key_clock_to_disk(SEQ_CLOCK_SRC_INTERNAL,
                &destination->globals.clock_source_key) == 0U)
            || (persist_key_record_start_to_disk(SEQ_REC_START_DEFAULT,
                &destination->globals.record_start_key) == 0U)
            || (persist_key_record_length_to_disk(SEQ_REC_LEN_MODE_OVERDUB,
                &destination->globals.record_length_key) == 0U))
        return PERSIST_CODEC_INVALID_ENTITY;
    return persistent_pattern_control_validate(destination);
}

persist_codec_result_t persistent_pattern_control_capture(persist_control_pattern_t*out)
{
    if(out==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    const uint8_t group_active=entity_topology_group_is_active();
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
    {
        persist_control_entity_t*d=&out->entities[e];
        persist_entity_caps_t caps;
        d->entity_id=e;
        if(!persist_entity_caps_resolve(group_active,e,&caps)
                ||!persist_key_family_to_disk(track_state_get_family(e),&d->family)
                ||!persist_key_type_to_disk(track_state_get_type(e),&d->type)
                ||!persist_key_midi_source_to_disk(track_state_get_midi_source(e),&d->midi_source_key)
                ||!persist_key_input_to_disk((caps.input_owner&&e<TRACK_COUNT)?track_state_get_external_input(e):0U,&d->input_key))
            return PERSIST_CODEC_INVALID_ENTITY;
        d->midi_channel=track_state_get_midi_channel(e);
        d->sequence.length=1U;
        d->sequence.division=1U;
        if(!polyphony_control_capture(e,&d->polyphony))return PERSIST_CODEC_INVALID_ENTITY;
        if(!caps.active)continue;
        d->muted=(d->family==PERSIST_FAMILY_OFF)?0U:(uint8_t)track_mute_get(e);
        if(!capture_product_state(e,d)||!capture_note_fx(e,&caps,d)
                ||((caps.sequence_owner!=0U)&&!capture_sequence(e,d)))
            return PERSIST_CODEC_INVALID_ENTITY;
        if(caps.modulation_owner)
        {
            d->modulation_present=1U;
            if(!capture_mod(e,&d->modulation))return PERSIST_CODEC_INVALID_MODULATION;
        }
    }
    out->globals.tempo_milli_bpm=seq_runtime_get_tempo_bpm_milli();
    out->globals.keyboard=(persist_control_keyboard_t){keyboard_runtime_get_root_index(),keyboard_runtime_get_scale_index(),keyboard_runtime_get_omnichord()?1U:0U,(uint8_t)keyboard_runtime_get_note_order(),keyboard_runtime_get_chord_override()?1U:0U,keyboard_runtime_get_mono_last()?1U:0U};
    out->globals.metronome_level=metronome_control_get_level();
    out->globals.groove_seed=seq_runtime_get_groove_seed();
    if(!param_global_control_capture(&out->globals.audio)
            ||!persist_key_clock_to_disk(seq_runtime_get_clock_source(),&out->globals.clock_source_key)
            ||!persist_key_record_start_to_disk(seq_runtime_get_rec_start_mode(),&out->globals.record_start_key)
            ||!persist_key_record_length_to_disk(seq_runtime_get_rec_len_mode(),&out->globals.record_length_key))
        return PERSIST_CODEC_INVALID_ENTITY;
    return persist_codec_validate_pattern(out);
}

persist_codec_result_t persistent_pattern_control_validate(const persist_control_pattern_t*p)
{
    persist_debug_object(PERSIST_DBG_OBJECT_PATTERN,0U,0U);
    persist_codec_result_t r=persist_codec_validate_pattern(p);
    if(r!=PERSIST_CODEC_OK)
    {
        persist_debug_validation_fail(PERSIST_DBG_VALIDATION_OTHER,(int32_t)r,
            UINT32_MAX,0U,0U,0U,0U,0U,0U,0U);
        return r;
    }
    const uint8_t active=(p->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type==PERSIST_TYPE_GROUP)?1U:0U;
    track_config_t cfg[BRICK_ENTITY_CAPACITY];uint8_t inputs[TRACK_COUNT];uint8_t families[BRICK_ENTITY_CAPACITY];uint8_t types[BRICK_ENTITY_CAPACITY];uint8_t voice_counts[BRICK_ENTITY_CAPACITY];
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
    {
        if(persist_key_family_from_disk(p->entities[e].family,&cfg[e].family)==0U
                ||persist_key_type_from_disk(p->entities[e].type,&cfg[e].type)==0U)
        {
            persist_debug_validation_fail(PERSIST_DBG_VALIDATION_ENTITY_KEY,
                PERSIST_CODEC_INVALID_ENTITY,e,p->entities[e].type,0U,0U,0U,0U,
                polyphony_control_get_voice_count(e),p->entities[e].polyphony.voice_count);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
        families[e]=(uint8_t)cfg[e].family;types[e]=(uint8_t)cfg[e].type;
        voice_counts[e]=p->entities[e].polyphony.voice_count;
        if(e<TRACK_COUNT&&persist_key_input_from_disk(p->entities[e].input_key,&inputs[e])==0U)
        {
            persist_debug_validation_fail(PERSIST_DBG_VALIDATION_INPUT_OWNERSHIP,
                PERSIST_CODEC_INVALID_ENTITY,e,(uint32_t)cfg[e].type,0U,0U,0U,0U,
                polyphony_control_get_voice_count(e),voice_counts[e]);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
    }
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
    {
        persist_entity_caps_t caps;
        const uint8_t topology_ok=persist_entity_caps_resolve(active,e,&caps);
        const uint8_t type_ok=(uint8_t)(
            (topology_ok!=0U)
            && !((caps.role==PERSIST_ENTITY_ROLE_GROUP_CHILD)
                &&((cfg[e].family!=TRACK_FAMILY_SAMPLER)||(cfg[e].type!=TRACK_TYPE_RAM)))
            && !((cfg[e].family==TRACK_FAMILY_OFF)&&(cfg[e].type!=TRACK_TYPE_NONE))
            && !((cfg[e].family!=TRACK_FAMILY_OFF)
                &&(track_catalog_type_is_valid_for_family(cfg[e].family,cfg[e].type)==false))
            && !((caps.active==0U)&&(cfg[e].family!=TRACK_FAMILY_OFF)
                &&(track_catalog_family_is_engine(cfg[e].family)==false))
            && !((caps.active!=0U)&&(cfg[e].family!=TRACK_FAMILY_OFF)
                &&(track_catalog_type_is_available(e,cfg[e].family,cfg[e].type,cfg)==false)));
        if(type_ok==0U)
        {
            persist_debug_validation_fail((topology_ok==0U)
                    ?PERSIST_DBG_VALIDATION_ENTITY_TOPOLOGY
                    :PERSIST_DBG_VALIDATION_ENTITY_TYPE,
                PERSIST_CODEC_INVALID_ENTITY,e,(uint32_t)cfg[e].type,0U,0U,0U,0U,
                polyphony_control_get_voice_count(e),voice_counts[e]);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
    }
    if(track_structure_validate_entity_bulk_with_polyphony(
            families,types,inputs,voice_counts)==0U)
    {
        uint16_t synth_voices=0U;
        uint8_t diagnosed=0U;
        for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
        {
            entity_topology_descriptor_t topology;
            if(entity_topology_resolve(active,e,&topology)==0U
                    ||topology.active==0U)continue;
            const track_runtime_family_t target_family=
                track_runtime_family_from_ui(cfg[e].family);
            const track_runtime_type_t target_runtime=
                track_runtime_type_from_ui(cfg[e].type);
            const track_runtime_engine_t target_engine=
                track_runtime_choose_engine(target_family,target_runtime);
            const track_runtime_type_t current_runtime=
                track_runtime_type_from_ui(track_state_get_type(e));
            const track_runtime_engine_t current_engine=
                track_runtime_choose_engine(
                    track_runtime_family_from_ui(track_state_get_family(e)),
                    current_runtime);
            if(target_family==TRACK_RUNTIME_FAMILY_SYNTH
                    ||target_engine==TRACK_RUNTIME_ENGINE_DRUM)
                synth_voices=(uint16_t)(synth_voices
                    +track_runtime_effective_voice_count(target_family,
                        target_runtime,voice_counts[e]));
            if((target_family!=TRACK_RUNTIME_FAMILY_OFF)
                    &&(target_family!=TRACK_RUNTIME_FAMILY_MIDI)
                    &&(target_runtime!=TRACK_RUNTIME_TYPE_GROUP)
                    &&(target_engine==TRACK_RUNTIME_ENGINE_NONE))
            {
                persist_debug_validation_fail(
                    PERSIST_DBG_VALIDATION_RUNTIME_ENGINE,
                    PERSIST_CODEC_INVALID_ENTITY,e,(uint32_t)cfg[e].type,
                    (uint32_t)current_runtime,(uint32_t)target_runtime,
                    (uint32_t)current_engine,(uint32_t)target_engine,
                    polyphony_control_get_voice_count(e),voice_counts[e]);
                diagnosed=1U;break;
            }
            if(synth_voices>SYNTH_POLYPHONY_GLOBAL_VOICE_BUDGET)
            {
                persist_debug_validation_fail(PERSIST_DBG_VALIDATION_VOICE_BUDGET,
                    PERSIST_CODEC_INVALID_ENTITY,e,(uint32_t)cfg[e].type,
                    (uint32_t)current_runtime,(uint32_t)target_runtime,
                    (uint32_t)current_engine,(uint32_t)target_engine,
                    polyphony_control_get_voice_count(e),voice_counts[e]);
                diagnosed=1U;break;
            }
        }
        if(diagnosed==0U&&track_input_ownership_validate_bulk(cfg,inputs)==0U)
            persist_debug_validation_fail(PERSIST_DBG_VALIDATION_INPUT_OWNERSHIP,
                PERSIST_CODEC_INVALID_ENTITY,UINT32_MAX,0U,0U,0U,0U,0U,0U,0U);
        else if(diagnosed==0U)
            persist_debug_validation_fail(PERSIST_DBG_VALIDATION_TRACK_STRUCTURE,
                PERSIST_CODEC_INVALID_ENTITY,UINT32_MAX,0U,0U,0U,0U,0U,0U,0U);
        return PERSIST_CODEC_INVALID_ENTITY;
    }
    return PERSIST_CODEC_OK;
}

static uint8_t apply_plock_value(param_id_t id,
                                 const persist_control_step_lock_t *lock,
                                 seq_value16_t *out)
{
    if ((lock == NULL) || (out == NULL)) return 0U;
    float value;
    if (lock->kind == PERSIST_VALUE_FLOAT32)
        value = lock->value.f32;
    else if ((id == PARAM_LFO1_SHAPE) || (id == PARAM_LFO2_SHAPE)
            || (id == PARAM_LFO3_SHAPE))
    {
        mod_lfo_shape_t v;
        if (persist_key_lfo_shape_from_disk(lock->value.u32, &v) == 0U)
            return 0U;
        value = (float)v;
    }
    else if ((id == PARAM_LFO1_TRIG) || (id == PARAM_LFO2_TRIG)
            || (id == PARAM_LFO3_TRIG))
    {
        mod_lfo_trig_mode_t v;
        if (persist_key_lfo_trigger_from_disk(lock->value.u32, &v) == 0U)
            return 0U;
        value = (float)v;
    }
    else
        return 0U;
    return seq_param_iface_encode_param_value(id, value, out);
}

static uint8_t persistent_sequence_changed(uint8_t e, const persist_control_entity_t *x)
{
    if ((e >= SEQ_LANE_CAPACITY) || (x == NULL)) return 1U;
    uint8_t value = 0U;
    seq_track_timing_config_t timing;
    uint8_t direction = (uint8_t)SEQ_DIRECTION_FWD;
    int8_t rotate = 0;
    if (seq_model_get_track_length(e) != x->sequence.length
            || seq_runtime_get_track_div(e, &value) == 0U || value != x->sequence.division
            || seq_runtime_get_track_traversal(e, &direction, &rotate) == 0U
            || direction != x->sequence.direction || rotate != x->sequence.rotate
            || seq_runtime_get_track_timing(e, &timing) == 0U
            || timing.base!=x->sequence.timing.base
            || timing.quantize!=x->sequence.timing.quantize
            || timing.timing!=x->sequence.timing.timing
            || timing.random!=x->sequence.timing.random
            || timing.velocity!=x->sequence.timing.velocity
            || timing.global!=x->sequence.timing.global
            || strncmp(timing.groove_name,x->sequence.timing.groove_name,
                       SEQ_GROOVE_NAME_BYTES)!=0) return 1U;
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
static uint8_t apply_sequence(uint8_t e,const persist_control_entity_t*x){seq_model_set_track_length(e,x->sequence.length);seq_runtime_set_track_div(e,x->sequence.division);seq_runtime_set_track_traversal(e,x->sequence.direction,x->sequence.rotate);seq_runtime_set_track_timing(e,&x->sequence.timing);for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){const persist_control_step_t*st=&x->sequence.steps[s];seq_model_set_trig(e,s,st->trigger);seq_model_set_step_roll(e,s,st->roll);seq_model_play_clear_step(e,s);seq_model_step_param_plock_clear(e,s);for(uint8_t v=0U;v<st->play_count;++v){const persist_control_play_item_t*p=&st->play[v];if(((p->present_mask&SEQ_STEP_PLAY_PRESENT_NOTE)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_NOTE,p->note)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_VELOCITY)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_VELOCITY,p->velocity)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_LENGTH)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_LENGTH,p->length)==0U)||((p->present_mask&SEQ_STEP_PLAY_PRESENT_MICROTIMING)!=0U&&seq_model_play_set(e,s,v,SEQ_STEP_PLAY_FIELD_MICROTIMING,p->microtiming)==0U))return 0U;}for(uint8_t i=0U;i<st->lock_count;++i){param_id_t id;uint8_t set;seq_param_slot_t slot;uint8_t found=0U;seq_value16_t value;if(persist_key_tone_slot_from_disk(st->locks[i].parameter,&slot)!=0U){set=SEQ_PLOCK_SET_TONE;value=(seq_value16_t)(st->locks[i].value.f32*65535.0f+0.5f);found=1U;}else{if(persist_key_param_from_disk(st->locks[i].parameter,&id)==0U||apply_plock_value(id,&st->locks[i],&value)==0U)return 0U;for(set=0U;set<SEQ_PLOCK_SET_COUNT;++set)if(seq_param_iface_param_to_slot(e,set,id,&slot)!=0U){found=1U;break;}}if(found==0U)return 0U;seq_plock_op_status_t status=seq_model_step_plock_upsert(e,s,set,slot,value,st->locks[i].flags);if(status!=SEQ_PLOCK_OP_CREATED&&status!=SEQ_PLOCK_OP_UPDATED)return 0U;}}return 1U;}
static uint8_t apply_note_fx(uint8_t e,uint8_t active,const persist_control_entity_t*x){persist_entity_caps_t caps;if(persist_entity_caps_resolve(active,e,&caps)==0U)return 0U;if(caps.note_fx_owner==0U){const uint8_t*value=(const uint8_t*)&x->note_fx;for(uint8_t i=0U;i<sizeof(x->note_fx);++i)if(value[i]!=0U)return 0U;return 1U;}return note_fx_chain_state_install_track(e,&x->note_fx);}
static uint8_t restore_polyphony_audio_fx(uint8_t entity,
    const polyphony_control_state_t*polyphony,const audio_fx_control_state_t*audio_fx)
{
    polyphony_control_state_t pp;
    audio_fx_control_state_t pa;
    live_parameter_audio_bulk_t bulk={.capture_tick=brick_media_clock_now_tick()};
    if(!polyphony_control_prepare(polyphony,&pp)
            ||!audio_fx_control_state_prepare_for_polyphony(
                entity,audio_fx,pp.voice_count,&pa)
            ||!polyphony_control_bulk_add(entity,&pp,&bulk)
            ||!audio_fx_control_state_bulk_add_prepared(entity,&pa,&bulk)
            ||((bulk.count!=0U)
                &&!live_parameter_audio_publication_submit_bulk(&bulk)))return 0U;
    const uint8_t installed=(uint8_t)(polyphony_control_install_prepared(entity,&pp)
        &&audio_fx_control_state_install_prepared(entity,&pa));
    if(installed==0U)Error_Handler();
    return installed;
}
static uint8_t apply_entity_owners(uint8_t entity,const persist_control_entity_t*x){if(x->fm_present){if(!fm_control_state_restore(entity,&x->fm))return 0U;}else if(x->tone_present&&!tone_program_control_restore(entity,&x->tone))return 0U;return param_filter_control_restore(entity,&x->filter)&&vca_control_state_restore(entity,&x->vca)&&mixer_control_state_restore(entity,&x->mixer)&&restore_polyphony_audio_fx(entity,&x->polyphony,&x->audio_fx);}
static uint8_t apply_product_state(uint8_t entity,const persist_control_entity_t*x)
{
    const track_family_t family=track_state_get_family(entity);
    const track_type_t type=track_state_get_type(entity);
    if(family==TRACK_FAMILY_SAMPLER&&(type==TRACK_TYPE_STREAM||type==TRACK_TYPE_RAM||type==TRACK_TYPE_MULTI))
    {
        if(x->asset_count==0U)return 1U;
        if (x->asset_count != 1U) return 0U;
        const project_control_asset_result_t result =
            project_control_track_asset_restore_status(
                entity, PROJECT_CONTROL_ASSET_SAMPLER, &x->assets[0]);
        if (result == PROJECT_CONTROL_ASSET_FAILED_INTERNAL
            || result == PROJECT_CONTROL_ASSET_PENDING)
            return 0U;
        return 1U;
    }
    if(family==TRACK_FAMILY_SYNTH&&type==TRACK_TYPE_WAVE)
    {
        if(x->asset_count==0U)return 1U;
        if(x->asset_count!=2U)return 0U;
        for(uint8_t osc=0U;osc<2U;++osc)
        {
            const project_control_asset_result_t result =
                project_control_track_asset_restore_status(
                    entity, (project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1+osc),
                    &x->assets[osc]);
            if (result == PROJECT_CONTROL_ASSET_FAILED_INTERNAL
                || result == PROJECT_CONTROL_ASSET_PENDING)
                return 0U;
        }
        return 1U;
    }
    if(family==TRACK_FAMILY_SYNTH&&type==TRACK_TYPE_FM)return (uint8_t)x->fm_present;
    return (uint8_t)((x->asset_count==0U)&&(x->fm_present==0U));
}
static uint8_t apply_mod(uint8_t owner,uint8_t active,const persist_control_modulation_t*m){mod_lfo_control_bank_t lfos;for(uint8_t i=0U;i<3U;++i){mod_lfo_shape_t shape;mod_lfo_trig_mode_t trig;if(persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&shape)==0U||persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&trig)==0U)return 0U;lfos.lfo[i]=(mod_lfo_control_value_t){m->lfos[i].rate,(float)shape,(float)trig,m->lfos[i].phase_offset};}if(mod_lfo_v1_restore_track(owner,&lfos)==0U)return 0U;for(uint8_t i=0U;i<2U;++i){uint8_t a,b,s;if(persist_key_mod_source_from_disk(m->multi[i].source_a_key,&a)==0U||persist_key_mod_source_from_disk(m->multi[i].source_b_key,&b)==0U||persist_key_mod_source_from_disk(m->slew[i].source_key,&s)==0U||mod_matrix_set_multi_source(owner,i,0U,(float)a)==0U||mod_matrix_set_multi_source(owner,i,1U,(float)b)==0U||mod_matrix_set_slew_source(owner,i,(float)s)==0U||mod_matrix_set_slew_amount(owner,i,m->slew[i].amount)==0U)return 0U;}const mod_env3_control_state_t env={m->envelope.attack,m->envelope.decay,m->envelope.sustain,m->envelope.release,(float)m->envelope.retrigger_hard};if(mod_env3_control_restore(owner,&env)==0U)return 0U;for(uint8_t i=0U;i<8U;++i){const persist_control_mod_route_t*r=&m->routes[i];mod_destination_address_t address=MOD_DESTINATION_NONE;uint8_t source;if(persist_key_mod_source_from_disk(r->source_key,&source)==0U)return 0U;if(r->destination_parameter!=PERSIST_CONTROL_KEY_NONE){uint8_t entity;param_id_t param;if(persist_key_mod_destination_from_disk(r->destination_entity,r->destination_parameter,active,&entity,&param)==0U)return 0U;address=mod_destination_address_make(entity,param);}if(mod_matrix_set_slot_state(owner,i,source,address,r->depth,r->enabled)==0U)return 0U;}return 1U;}

static void persist_debug_entity_failure(persist_dbg_validation_step_t step,
    persist_codec_result_t result, uint8_t entity,
    const persist_control_entity_t *saved)
{
    track_family_t target_family=TRACK_FAMILY_OFF;
    track_type_t target_type=TRACK_TYPE_NONE;
    (void)persist_key_family_from_disk(saved->family,&target_family);
    (void)persist_key_type_from_disk(saved->type,&target_type);
    const track_runtime_type_t current_runtime=
        track_runtime_type_from_ui(track_state_get_type(entity));
    const track_runtime_type_t target_runtime=
        track_runtime_type_from_ui(target_type);
    persist_debug_object(PERSIST_DBG_OBJECT_ENTITY,entity,(uint32_t)target_type);
    persist_debug_validation_fail(step,(int32_t)result,entity,
        (uint32_t)target_type,(uint32_t)current_runtime,(uint32_t)target_runtime,
        (uint32_t)track_runtime_choose_engine(
            track_runtime_family_from_ui(track_state_get_family(entity)),
            current_runtime),
        (uint32_t)track_runtime_choose_engine(
            track_runtime_family_from_ui(target_family),target_runtime),
        polyphony_control_get_voice_count(entity),saved->polyphony.voice_count);
}

static persist_codec_result_t persistent_pattern_control_install_internal(
    const persist_control_pattern_t *pattern,
    uint8_t resume_transport)
{
    const uint8_t group_active =
        (pattern->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type
            == PERSIST_TYPE_GROUP) ? 1U : 0U;
    uint8_t families[BRICK_ENTITY_CAPACITY];
    uint8_t types[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channels[BRICK_ENTITY_CAPACITY];
    uint8_t midi_sources[BRICK_ENTITY_CAPACITY];
    uint8_t inputs[TRACK_COUNT];
    uint8_t voice_counts[BRICK_ENTITY_CAPACITY];
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        track_family_t family;
        track_type_t type;
        track_midi_source_t midi_source;
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
        voice_counts[entity] = pattern->entities[entity].polyphony.voice_count;
        if (entity < TRACK_COUNT)
            (void)persist_key_input_from_disk(
                pattern->entities[entity].input_key, &inputs[entity]);
    }

    const uint8_t current_group_active = entity_topology_group_is_active();
    seq_runtime_set_groove_seed(pattern->globals.groove_seed);
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
            seq_engine_control_mark_dirty();
    }

    if (track_structure_apply_entity_bulk_with_inputs_and_polyphony(
            families, types, midi_channels, midi_sources, inputs,
            voice_counts) == 0U)
        return PERSIST_CODEC_INVALID_ENTITY;

    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        (void)entity_topology_resolve(group_active, entity, &topology);
        if (topology.active == 0U)
            continue;
        const persist_control_entity_t *const saved =
            &pattern->entities[entity];
        persist_entity_caps_t caps;
        if (persist_entity_caps_resolve(group_active, entity, &caps) == 0U)
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_ENTITY_TOPOLOGY,
                PERSIST_CODEC_INVALID_ENTITY,entity,saved);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
        if ((apply_product_state(entity, saved) == 0U)
                || (apply_entity_owners(entity, saved) == 0U))
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_PRODUCT_STATE,
                PERSIST_CODEC_UNKNOWN_KEY,entity,saved);
            return PERSIST_CODEC_UNKNOWN_KEY;
        }
        if ((caps.sequence_owner != 0U)
                && (apply_sequence(entity, saved) == 0U))
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_SEQUENCE,
                PERSIST_CODEC_INVALID_ENTITY,entity,saved);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
        if (apply_note_fx(entity, group_active, saved) == 0U)
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_NOTE_FX,
                PERSIST_CODEC_INVALID_ENTITY,entity,saved);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
        if (track_mute_set(entity, saved->muted) == 0U)
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_OTHER,
                PERSIST_CODEC_INVALID_ENTITY,entity,saved);
            return PERSIST_CODEC_INVALID_ENTITY;
        }
    }
    if (param_global_control_restore(&pattern->globals.audio) == 0U)
    {
        persist_debug_validation_fail(PERSIST_DBG_VALIDATION_AUDIO_GLOBAL,
            PERSIST_CODEC_INVALID_ENTITY,UINT32_MAX,0U,0U,0U,0U,0U,0U,0U);
        return PERSIST_CODEC_INVALID_ENTITY;
    }

    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        entity_topology_descriptor_t topology;
        (void)entity_topology_resolve(group_active, entity, &topology);
        if (topology.active == 0U)
            continue;
        if ((pattern->entities[entity].modulation_present != 0U)
                && (apply_mod(entity, group_active,
                              &pattern->entities[entity].modulation) == 0U))
        {
            persist_debug_entity_failure(PERSIST_DBG_VALIDATION_MODULATION,
                PERSIST_CODEC_INVALID_MODULATION,entity,
                &pattern->entities[entity]);
            return PERSIST_CODEC_INVALID_MODULATION;
        }
    }

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
    keyboard_runtime_set_root(pattern->globals.keyboard.root);
    keyboard_runtime_set_scale(pattern->globals.keyboard.scale);
    keyboard_runtime_set_omnichord(pattern->globals.keyboard.omnichord != 0U);
    keyboard_runtime_set_note_order((note_order_t)pattern->globals.keyboard.note_order);
    keyboard_runtime_set_chord_override(pattern->globals.keyboard.chord_override != 0U);
    keyboard_runtime_set_mono_last(pattern->globals.keyboard.mono_last != 0U);
    if (metronome_control_set_level(pattern->globals.metronome_level) == 0U)
        return PERSIST_CODEC_IO_ERROR;
    undo_v2_invalidate_history();
    (void)resume_transport;
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persistent_pattern_control_apply(
    const persist_control_pattern_t *pattern, uint8_t resume_transport)
{
    g_persist_dbg.audio_publish_result = 1U;
    g_persist_dbg.seq_publish_result = 1U;
    if ((audio_state_snapshot_control_active() == 0U)
            && (audio_state_snapshot_control_preflight() == 0U))
        return PERSIST_CODEC_IO_ERROR;
    if (audio_state_snapshot_control_begin(
            CONTROL_AUDIO_STATE_PATTERN) == 0U)
        return PERSIST_CODEC_IO_ERROR;
    const persist_codec_result_t result =
        persistent_pattern_control_install_into_active_snapshot(pattern, resume_transport);
    if (result != PERSIST_CODEC_OK)
    {
        audio_state_snapshot_control_abort();
        return result;
    }
    /* A stopped replacement still has to publish before AUDIO is rebound.
     * Otherwise the first PLAY boundary can be rendered from the previous
     * Pattern while the new track programs are already active.  Resetting the
     * execution epoch also prevents old lock/note ownership from crossing the
     * stopped replacement.  Running cycle recalls preserve their epoch. */
    if (resume_transport == 0U)
        seq_engine_control_reset_note_fx_context();
    if (seq_engine_control_flush() == 0U)
    {
        g_persist_dbg.seq_publish_result = 0U;
        audio_state_snapshot_control_abort();
        return PERSIST_CODEC_IO_ERROR;
    }
    g_persist_dbg.seq_publish_result = 2U;
    if (audio_state_snapshot_control_commit() == 0U)
    {
        audio_state_snapshot_control_abort();
        return PERSIST_CODEC_IO_ERROR;
    }
    g_persist_dbg.audio_publish_result = 2U;
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persistent_pattern_control_install_into_active_snapshot(
    const persist_control_pattern_t *pattern,
    uint8_t resume_transport)
{
    if ((pattern == NULL) || (audio_state_snapshot_control_active() == 0U))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    const persist_codec_result_t validation =
        persistent_pattern_control_validate(pattern);
    if (validation != PERSIST_CODEC_OK) return validation;
    return persistent_pattern_control_install_internal(pattern, resume_transport);
}

void persistent_pattern_control_sync_ui_after_commit(void)
{
    ui_active_track_sync_full_after_global_restore();
}
