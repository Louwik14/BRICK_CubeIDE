#include "Storage/persistent_patch_control.h"
#include <string.h>
#include "App/live_parameter_audio_publication.h"
#include "ControlRT/audio_state_snapshot_control.h"
#include "Platform/brick_media_clock.h"
#include "Platform/memory_layout.h"
#include "IPC/live_parameter_event.h"
#include "Mod/mod_env3_control.h"
#include "Mod/mod_destination_control.h"
#include "Mod/mod_destination_contract.h"
#include "Mod/mod_lfo_v1_control.h"
#include "Mod/mod_matrix_control.h"
#include "Param/param_filter.h"
#include "Seq/seq_param_iface.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/project_control.h"
#include "Track/audio_fx_control_state.h"
#include "Track/entity_topology.h"
#include "Track/fm_control_state.h"
#include "main.h"
#include "Track/polyphony_control.h"
#include "Track/tone_program_control.h"
#include "Track/track_sound_state.h"
#include "Track/track_state.h"
#include "Track/vca_control_state.h"
#include "Track/track_catalog.h"
#include "Track/track_runtime.h"

STORAGE_STATE_SDRAM static persist_control_patch_t
    g_patch_transaction_backup[BRICK_ENTITY_CAPACITY];

static uint8_t validate_mod(const persist_control_modulation_t*m,const track_config_t*c,uint8_t owner)
{
    mod_lfo_control_bank_t lfos;mod_env3_control_state_t env,prepared;
    const uint8_t active=(uint8_t)(c[BRICK_ENTITY_GROUP_MASTER_ID].type==TRACK_TYPE_GROUP);
    for(uint8_t i=0U;i<3U;++i){mod_lfo_shape_t sh;mod_lfo_trig_mode_t tr;
        if(!persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&sh)
                ||!persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&tr))return 0U;
        lfos.lfo[i]=(mod_lfo_control_value_t){m->lfos[i].rate,(float)sh,(float)tr,m->lfos[i].phase_offset};}
    env=(mod_env3_control_state_t){m->envelope.attack,m->envelope.decay,m->envelope.sustain,m->envelope.release,(float)m->envelope.retrigger_hard};
    if(!mod_lfo_v1_prepare_bank(&lfos,&lfos)||!mod_env3_control_prepare(&env,&prepared))return 0U;
    for(uint8_t i=0U;i<2U;++i){uint8_t a,b,s;if(!persist_key_mod_source_from_disk(m->multi[i].source_a_key,&a)||!persist_key_mod_source_from_disk(m->multi[i].source_b_key,&b)||!persist_key_mod_source_from_disk(m->slew[i].source_key,&s))return 0U;}
    for(uint8_t i=0U;i<8U;++i){uint8_t src,de;param_id_t dp;if(!persist_key_mod_source_from_disk(m->routes[i].source_key,&src))return 0U;if(m->routes[i].destination_parameter!=PERSIST_CONTROL_KEY_NONE&&(!persist_key_mod_destination_from_disk(m->routes[i].destination_entity,m->routes[i].destination_parameter,active,&de,&dp)||!mod_destination_catalog_address_is_supported_projected(owner,mod_destination_address_make(de,dp),c)))return 0U;}
    return 1U;
}

static uint8_t validate_assets(const persist_control_patch_t*p,track_family_t f,track_type_t t)
{
    if(f==TRACK_FAMILY_SAMPLER){if(p->asset_count==0U)return 1U;if(p->asset_count!=1U)return 0U;if(t==TRACK_TYPE_STREAM)return p->assets[0].kind==PERSIST_ASSET_SAMPLE_STREAM;if(t==TRACK_TYPE_RAM)return p->assets[0].kind==PERSIST_ASSET_SAMPLE_RAM;if(t==TRACK_TYPE_MULTI)return p->assets[0].kind==PERSIST_ASSET_MULTI;return 0U;}
    if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_WAVE){if(p->asset_count==0U)return 1U;return(p->asset_count==2U&&p->assets[0].kind==PERSIST_ASSET_WAVETABLE&&p->assets[1].kind==PERSIST_ASSET_WAVETABLE)?1U:0U;}
    return(p->asset_count==0U)?1U:0U;
}

static uint8_t copy_name(const char*n,persist_control_patch_t*p){if(n==NULL)return 1U;while(p->name_length<PERSIST_CONTROL_PATCH_NAME_BYTES&&n[p->name_length]){p->name[p->name_length]=n[p->name_length];++p->name_length;}return n[p->name_length]=='\0';}
static void capture_assets(uint8_t e,track_family_t f,track_type_t t,persist_control_patch_t*p){if(f==TRACK_FAMILY_SAMPLER&&(t==TRACK_TYPE_STREAM||t==TRACK_TYPE_RAM||t==TRACK_TYPE_MULTI)){if(project_control_track_asset_get(e,PROJECT_CONTROL_ASSET_SAMPLER,&p->assets[0]))p->asset_count=1U;}else if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_WAVE)for(uint8_t i=0U;i<2U;++i)if(project_control_track_asset_get(e,(project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1+i),&p->assets[p->asset_count]))++p->asset_count;}
static uint8_t capture_mod(uint8_t e,persist_control_modulation_t*m){track_sound_state_t s;mod_env3_control_state_t env;if(!track_sound_state_capture(e,&s)||!mod_env3_control_capture(e,&env))return 0U;for(uint8_t i=0U;i<3U;++i){float sh,tr;if(!mod_lfo_v1_get_track_param(e,i,MOD_LFO_PARAM_RATE,&m->lfos[i].rate)||!mod_lfo_v1_get_track_param(e,i,MOD_LFO_PARAM_SHAPE,&sh)||!mod_lfo_v1_get_track_param(e,i,MOD_LFO_PARAM_TRIG,&tr)||!mod_lfo_v1_get_track_param(e,i,MOD_LFO_PARAM_PHASE,&m->lfos[i].phase_offset)||!persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)sh,&m->lfos[i].shape_key)||!persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)(uint8_t)tr,&m->lfos[i].trigger_key))return 0U;}m->envelope=(persist_control_mod_envelope_t){env.attack,env.decay,env.sustain,env.release,(uint8_t)(env.retrigger>=0.5f)};for(uint8_t i=0U;i<2U;++i){if(!persist_key_mod_source_to_disk(s.mod_multi_source[i][0],&m->multi[i].source_a_key)||!persist_key_mod_source_to_disk(s.mod_multi_source[i][1],&m->multi[i].source_b_key)||!persist_key_mod_source_to_disk(s.mod_slew_source[i],&m->slew[i].source_key))return 0U;m->slew[i].amount=s.mod_slew_amount[i];}for(uint8_t i=0U;i<8U;++i){track_mod_matrix_slot_t*r=&s.mod_matrix[i];persist_control_mod_route_t*d=&m->routes[i];if(!persist_key_mod_source_to_disk(r->source,&d->source_key))return 0U;d->depth=r->depth;d->enabled=r->enabled;if(r->destination==MOD_DESTINATION_NONE){d->destination_entity=e;d->destination_parameter=PERSIST_CONTROL_KEY_NONE;d->enabled=0U;}else{uint8_t de;param_id_t dp;if(!mod_destination_address_resolve(r->destination,&de,&dp)||!persist_key_mod_destination_to_disk(de,dp,&d->destination_entity,&d->destination_parameter))return 0U;}}return 1U;}

persist_codec_result_t persistent_patch_control_capture(uint8_t e,const char*n,persist_control_patch_t*p){if(p==NULL||e>=PERSIST_CONTROL_ENTITY_COUNT)return PERSIST_CODEC_INVALID_ARGUMENT;memset(p,0,sizeof(*p));track_family_t f=track_state_get_family(e);track_type_t t=track_state_get_type(e);brick_entity_id_t mod_owner=e;if(!copy_name(n,p)||!persist_key_family_to_disk(f,&p->family)||!persist_key_type_to_disk(t,&p->type)||!entity_topology_mod_owner(e,&mod_owner))return PERSIST_CODEC_UNKNOWN_KEY;capture_assets(e,f,t,p);if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_FM){p->fm_present=1U;if(!fm_control_state_get(e,&p->fm))return PERSIST_CODEC_INVALID_ENTITY;}else{p->tone_present=1U;if(!tone_program_control_capture(e,&p->tone))return PERSIST_CODEC_INVALID_ENTITY;}p->modulation_present=(uint8_t)(mod_owner==e);if(!param_filter_control_capture(e,&p->filter)||!vca_control_state_capture(e,&p->vca)||!audio_fx_control_state_capture(e,&p->audio_fx)||!polyphony_control_capture(e,&p->polyphony)||((p->modulation_present!=0U)&&!capture_mod(e,&p->modulation)))return PERSIST_CODEC_INVALID_ENTITY;return persist_codec_validate_patch(p);}
persist_codec_result_t persistent_patch_control_validate_mask(const persist_control_patch_t*p,uint16_t mask)
{
    persist_codec_result_t r=persist_codec_validate_patch(p);if(r!=PERSIST_CODEC_OK)return r;if(mask==0U)return PERSIST_CODEC_INVALID_ENTITY;
    track_family_t f;track_type_t t;if(!persist_key_family_from_disk(p->family,&f)||!persist_key_type_from_disk(p->type,&t))return PERSIST_CODEC_UNKNOWN_KEY;
    uint8_t fs[BRICK_ENTITY_CAPACITY],ts[BRICK_ENTITY_CAPACITY],voices[BRICK_ENTITY_CAPACITY],inputs[TRACK_COUNT];track_config_t c[BRICK_ENTITY_CAPACITY];
    for(uint8_t e=0U;e<BRICK_ENTITY_CAPACITY;++e){fs[e]=track_state_get_family(e);ts[e]=track_state_get_type(e);voices[e]=polyphony_control_get_voice_count(e);if(e<TRACK_COUNT)inputs[e]=track_state_get_external_input(e);if(mask&(uint16_t)(1UL<<e)){if(!entity_topology_is_active(e))return PERSIST_CODEC_INVALID_ENTITY;fs[e]=(uint8_t)f;ts[e]=(uint8_t)t;voices[e]=p->polyphony.voice_count;}}
    for(uint8_t e=0U;e<BRICK_ENTITY_CAPACITY;++e)c[e]=(track_config_t){(track_family_t)fs[e],(track_type_t)ts[e]};
    polyphony_control_state_t pp;audio_fx_control_state_t pa;
    const uint8_t expect_fm=(uint8_t)(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_FM);
    if((p->fm_present!=expect_fm)||(p->tone_present==(uint8_t)expect_fm)
            ||!validate_assets(p,f,t)||!track_structure_validate_entity_bulk_with_polyphony(fs,ts,inputs,voices)
            ||((f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_FM)?!fm_control_state_validate(&p->fm):!tone_program_control_validate(&p->tone,track_runtime_type_from_ui(t)))
            ||!param_filter_control_validate(&p->filter)||!vca_control_state_validate(&p->vca)||!audio_fx_control_state_validate(&p->audio_fx)||!polyphony_control_prepare(&p->polyphony,&pp))return PERSIST_CODEC_INVALID_ENTITY;
    for(uint8_t e=0U;e<BRICK_ENTITY_CAPACITY;++e)if(mask&(uint16_t)(1UL<<e)){brick_entity_id_t mod_owner=e;if(!entity_topology_mod_owner(e,&mod_owner)||(p->modulation_present!=(uint8_t)(mod_owner==e))||!audio_fx_control_state_prepare_for_polyphony(e,&p->audio_fx,pp.voice_count,&pa)||((p->modulation_present!=0U)&&!validate_mod(&p->modulation,c,e)))return PERSIST_CODEC_INVALID_ENTITY;}
    return PERSIST_CODEC_OK;
}
persist_codec_result_t persistent_patch_control_validate(const persist_control_patch_t*p,uint8_t target){return(target<PERSIST_CONTROL_ENTITY_COUNT)?persistent_patch_control_validate_mask(p,(uint16_t)(1UL<<target)):PERSIST_CODEC_INVALID_ENTITY;}
static uint8_t restore_mod(uint8_t e,const persist_control_modulation_t*m)
{
    mod_lfo_control_bank_t lfos;
    for(uint8_t i=0U;i<3U;++i){mod_lfo_shape_t sh;mod_lfo_trig_mode_t tr;
        if(!persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&sh)
                ||!persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&tr))return 0U;
        lfos.lfo[i]=(mod_lfo_control_value_t){m->lfos[i].rate,(float)sh,
            (float)tr,m->lfos[i].phase_offset};}
    if(!mod_lfo_v1_restore_track(e,&lfos))return 0U;
    mod_env3_control_state_t env={m->envelope.attack,m->envelope.decay,m->envelope.sustain,m->envelope.release,(float)m->envelope.retrigger_hard};
    if(!mod_env3_control_restore(e,&env))return 0U;
    for(uint8_t i=0U;i<2U;++i){uint8_t a,b,s;if(!persist_key_mod_source_from_disk(m->multi[i].source_a_key,&a)||!persist_key_mod_source_from_disk(m->multi[i].source_b_key,&b)||!persist_key_mod_source_from_disk(m->slew[i].source_key,&s)||!mod_matrix_set_multi_source(e,i,0U,(float)a)||!mod_matrix_set_multi_source(e,i,1U,(float)b)||!mod_matrix_set_slew_source(e,i,(float)s)||!mod_matrix_set_slew_amount(e,i,m->slew[i].amount))return 0U;}
    for(uint8_t i=0U;i<8U;++i){const persist_control_mod_route_t*r=&m->routes[i];uint8_t src,de;param_id_t dp;mod_destination_address_t dst=MOD_DESTINATION_NONE;if(!persist_key_mod_source_from_disk(r->source_key,&src))return 0U;if(r->destination_parameter!=PERSIST_CONTROL_KEY_NONE){if(!persist_key_mod_destination_from_disk(r->destination_entity,r->destination_parameter,entity_topology_group_is_active(),&de,&dp))return 0U;dst=mod_destination_address_make(de,dp);}if(!mod_matrix_set_slot_state(e,i,src,dst,r->depth,r->enabled))return 0U;}
    return 1U;
}
static uint8_t assets_apply(const persist_control_patch_t*p,uint8_t e,track_family_t f,track_type_t t){if(!project_control_track_assets_clear(e))return 0U;if(p->asset_count==0U)return 1U;if(f==TRACK_FAMILY_SAMPLER)return p->asset_count==1U&&project_control_track_asset_restore(e,PROJECT_CONTROL_ASSET_SAMPLER,&p->assets[0]);if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_WAVE){if(p->asset_count!=2U)return 0U;for(uint8_t i=0U;i<2U;++i)if(!project_control_track_asset_restore(e,(project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1+i),&p->assets[i]))return 0U;return 1U;}return 0U;}
static uint8_t assets_can_apply(const persist_control_patch_t*p,uint8_t e,track_family_t f,track_type_t t){if(p->asset_count==0U)return 1U;if(f==TRACK_FAMILY_SAMPLER)return p->asset_count==1U&&project_control_track_asset_can_restore(e,PROJECT_CONTROL_ASSET_SAMPLER,&p->assets[0]);if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_WAVE){if(p->asset_count!=2U)return 0U;for(uint8_t i=0U;i<2U;++i)if(!project_control_track_asset_can_restore(e,(project_control_asset_role_t)(PROJECT_CONTROL_ASSET_WAVE_OSC1+i),&p->assets[i]))return 0U;return 1U;}return 0U;}
static uint8_t restore_polyphony_audio_fx(uint8_t entity,const polyphony_control_state_t*polyphony,const audio_fx_control_state_t*audio_fx)
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

static uint8_t restore_patch_owners(uint8_t entity,
                                    const persist_control_patch_t *patch,
                                    track_family_t family, track_type_t type)
{
    brick_entity_id_t mod_owner = entity;
    if (entity_topology_mod_owner(entity, &mod_owner) == 0U) return 0U;
    return assets_apply(patch, entity, family, type)
        && (patch->fm_present
            ? fm_control_state_restore(entity, &patch->fm)
            : tone_program_control_restore(entity, &patch->tone))
        && param_filter_control_restore(entity, &patch->filter)
        && vca_control_state_restore(entity, &patch->vca)
        && restore_polyphony_audio_fx(entity, &patch->polyphony,
                                      &patch->audio_fx)
        && ((patch->modulation_present == 0U) || (mod_owner != entity)
            || restore_mod(entity, &patch->modulation));
}

static uint8_t patch_transaction_restore_previous(uint16_t mask)
{
    uint8_t family[BRICK_ENTITY_CAPACITY];
    uint8_t type[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channel[BRICK_ENTITY_CAPACITY];
    uint8_t midi_source[BRICK_ENTITY_CAPACITY];
    uint8_t external_input[TRACK_COUNT];
    audio_state_snapshot_control_abort();
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        family[entity] = (uint8_t)track_state_get_family(entity);
        type[entity] = (uint8_t)track_state_get_type(entity);
        midi_channel[entity] = track_state_get_midi_channel(entity);
        midi_source[entity] = track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT)
            external_input[entity] = track_state_get_external_input(entity);
        if ((mask & (uint16_t)(1UL << entity)) != 0U)
        {
            track_family_t saved_family;
            track_type_t saved_type;
            if (!persist_key_family_from_disk(
                    g_patch_transaction_backup[entity].family, &saved_family)
                    || !persist_key_type_from_disk(
                        g_patch_transaction_backup[entity].type, &saved_type))
                return 0U;
            family[entity] = (uint8_t)saved_family;
            type[entity] = (uint8_t)saved_type;
        }
    }
    if (!audio_state_snapshot_control_begin(CONTROL_AUDIO_STATE_PATCH)
            || !track_structure_apply_entity_bulk_with_inputs(family, type,
                midi_channel, midi_source, external_input)) return 0U;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        if ((mask & (uint16_t)(1UL << entity)) == 0U) continue;
        if (!restore_patch_owners(entity, &g_patch_transaction_backup[entity],
                (track_family_t)family[entity], (track_type_t)type[entity]))
            return 0U;
    }
    return seq_param_iface_patch_runtime_transaction_rollback()
        && audio_state_snapshot_control_commit();
}

persist_codec_result_t persistent_patch_control_apply_mask(
    const persist_control_patch_t *patch, uint16_t mask)
{
    persist_codec_result_t result =
        persistent_patch_control_validate_mask(patch, mask);
    if (result != PERSIST_CODEC_OK) return result;
    if (!audio_state_snapshot_control_preflight()) return PERSIST_CODEC_IO_ERROR;

    uint8_t family[BRICK_ENTITY_CAPACITY];
    uint8_t type[BRICK_ENTITY_CAPACITY];
    uint8_t midi_channel[BRICK_ENTITY_CAPACITY];
    uint8_t midi_source[BRICK_ENTITY_CAPACITY];
    uint8_t external_input[TRACK_COUNT];
    track_family_t patch_family;
    track_type_t patch_type;
    (void)persist_key_family_from_disk(patch->family, &patch_family);
    (void)persist_key_type_from_disk(patch->type, &patch_type);
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
    {
        family[entity] = (uint8_t)track_state_get_family(entity);
        type[entity] = (uint8_t)track_state_get_type(entity);
        midi_channel[entity] = track_state_get_midi_channel(entity);
        midi_source[entity] = track_state_get_midi_source(entity);
        if (entity < TRACK_COUNT)
            external_input[entity] = track_state_get_external_input(entity);
        if ((mask & (uint16_t)(1UL << entity)) == 0U) continue;
        family[entity] = (uint8_t)patch_family;
        type[entity] = (uint8_t)patch_type;
        if (!assets_can_apply(patch, entity, patch_family, patch_type)
                || (persistent_patch_control_capture(entity, NULL,
                    &g_patch_transaction_backup[entity]) != PERSIST_CODEC_OK))
            return PERSIST_CODEC_INVALID_ENTITY;
    }
    if (!seq_param_iface_patch_runtime_transaction_begin(mask))
        return PERSIST_CODEC_IO_ERROR;
    if (!audio_state_snapshot_control_begin(CONTROL_AUDIO_STATE_PATCH))
    {
        (void)seq_param_iface_patch_runtime_transaction_rollback();
        return PERSIST_CODEC_IO_ERROR;
    }

    result = PERSIST_CODEC_INVALID_ENTITY;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if (((mask & (uint16_t)(1UL << entity)) != 0U)
                && !seq_param_iface_clear_patch_runtime(entity)) goto rollback;
    if (!track_structure_apply_entity_bulk_with_inputs(family, type,
            midi_channel, midi_source, external_input)) goto rollback;
    for (uint8_t entity = 0U; entity < BRICK_ENTITY_CAPACITY; ++entity)
        if (((mask & (uint16_t)(1UL << entity)) != 0U)
                && !restore_patch_owners(entity, patch,
                    patch_family, patch_type)) goto rollback;
    if (!audio_state_snapshot_control_commit())
    {
        result = PERSIST_CODEC_IO_ERROR;
        goto rollback;
    }
    seq_param_iface_patch_runtime_transaction_commit();
    return PERSIST_CODEC_OK;

rollback:
    if (!patch_transaction_restore_previous(mask)) Error_Handler();
    return result;
}
persist_codec_result_t persistent_patch_control_apply(const persist_control_patch_t*p,uint8_t target){return(target<PERSIST_CONTROL_ENTITY_COUNT)?persistent_patch_control_apply_mask(p,(uint16_t)(1UL<<target)):PERSIST_CODEC_INVALID_ENTITY;}

persist_codec_result_t persistent_patch_control_make_default(uint8_t e,persist_control_patch_t*p)
{
    if(p==NULL||e>=PERSIST_CONTROL_ENTITY_COUNT||!entity_topology_is_active(e))return PERSIST_CODEC_INVALID_ENTITY;
    memset(p,0,sizeof(*p));
    const track_family_t f=track_state_get_family(e);const track_type_t t=track_state_get_type(e);if(!copy_name("Init",p)||!persist_key_family_to_disk(f,&p->family)||!persist_key_type_to_disk(t,&p->type))return PERSIST_CODEC_UNKNOWN_KEY;
    if(f==TRACK_FAMILY_SYNTH&&t==TRACK_TYPE_FM){p->fm_present=1U;fm_control_state_make_default(&p->fm);}else{p->tone_present=1U;if(!tone_program_control_make_default(track_runtime_type_from_ui(t),&p->tone))return PERSIST_CODEC_INVALID_ENTITY;}
    param_filter_control_make_default(&p->filter);vca_control_state_make_default(&p->vca);audio_fx_control_state_make_default(&p->audio_fx);polyphony_control_make_default(&p->polyphony);
    brick_entity_id_t mod_owner=e;if(!entity_topology_mod_owner(e,&mod_owner))return PERSIST_CODEC_INVALID_ENTITY;p->modulation_present=(uint8_t)(mod_owner==e);if(p->modulation_present==0U)return persist_codec_validate_patch(p);
    track_sound_state_t sound;mod_env3_control_state_t env;track_sound_state_make_default(&sound);mod_env3_control_make_default(&env);p->modulation.envelope=(persist_control_mod_envelope_t){env.attack,env.decay,env.sustain,env.release,(uint8_t)(env.retrigger>=0.5f)};
    for(uint8_t i=0U;i<3U;++i){p->modulation.lfos[i].rate=param_registry[PARAM_LFO1_RATE+i*4U].default_value;p->modulation.lfos[i].phase_offset=param_registry[PARAM_LFO1_PHASE+i*4U].default_value;if(!persist_key_lfo_shape_to_disk((mod_lfo_shape_t)(uint8_t)param_registry[PARAM_LFO1_SHAPE+i*4U].default_value,&p->modulation.lfos[i].shape_key)||!persist_key_lfo_trigger_to_disk((mod_lfo_trig_mode_t)(uint8_t)param_registry[PARAM_LFO1_TRIG+i*4U].default_value,&p->modulation.lfos[i].trigger_key))return PERSIST_CODEC_UNKNOWN_KEY;}
    for(uint8_t i=0U;i<2U;++i){if(!persist_key_mod_source_to_disk(sound.mod_multi_source[i][0],&p->modulation.multi[i].source_a_key)||!persist_key_mod_source_to_disk(sound.mod_multi_source[i][1],&p->modulation.multi[i].source_b_key)||!persist_key_mod_source_to_disk(sound.mod_slew_source[i],&p->modulation.slew[i].source_key))return PERSIST_CODEC_UNKNOWN_KEY;}
    for(uint8_t i=0U;i<8U;++i){if(!persist_key_mod_source_to_disk(sound.mod_matrix[i].source,&p->modulation.routes[i].source_key))return PERSIST_CODEC_UNKNOWN_KEY;p->modulation.routes[i].destination_entity=e;p->modulation.routes[i].destination_parameter=PERSIST_CONTROL_KEY_NONE;}
    return persist_codec_validate_patch(p);
}
