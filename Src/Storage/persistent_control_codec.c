#include "Storage/persistent_control_codec.h"
#include "Storage/asset_ref.h"
#include "Storage/persistent_entity_topology.h"
#include "Storage/persistent_key_catalog.h"
#include "Param/param_registry.h"
#include "Seq/seq_model.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define CODEC_MAGIC_0 'B'
#define CODEC_MAGIC_1 '6'
#define CODEC_MAGIC_2 'C'
#define CODEC_MAGIC_3 'P'

#define SECTION_PATTERN_BODY 0x1001U
#define SECTION_PROJECT_CORE 0x2001U
#define SECTION_PROJECT_ASSETS 0x2002U
#define SECTION_PROJECT_MACROS 0x2003U
#define SECTION_PROJECT_BANK 0x2004U
#define SECTION_PATCH_BODY 0x3001U

#define PERSIST_TYPED_KBD_ROOT           0x4B420001UL
#define PERSIST_TYPED_KBD_SCALE          0x4B420002UL
#define PERSIST_TYPED_KBD_OMNICHORD      0x4B420003UL
#define PERSIST_TYPED_KBD_NOTE_ORDER     0x4B420004UL
#define PERSIST_TYPED_KBD_CHORD_OVERRIDE 0x4B420005UL
#define PERSIST_TYPED_KBD_MONO_LAST      0x4B420006UL
#define PERSIST_TYPED_METRONOME_LEVEL    0x4D540001UL

#define PERSIST_V3_KBD_ROOT           PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0x27D7BBUL)
#define PERSIST_V3_KBD_SCALE          PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0xAE5DF7UL)
#define PERSIST_V3_KBD_OMNICHORD      PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0xCC7358UL)
#define PERSIST_V3_KBD_NOTE_ORDER     PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0x6E3280UL)
#define PERSIST_V3_KBD_CHORD_OVERRIDE PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0xE63C50UL)
#define PERSIST_V3_KBD_MONO_LAST      PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_PLAY, 0x8B0D59UL)
#define PERSIST_V3_METRONOME_LEVEL    PERSIST_CONTROL_PARAMETER_KEY(PERSIST_PARAM_NAMESPACE_CONFIG, 0xBC3D3DUL)

typedef enum { CODEC_COUNT, CODEC_WRITE, CODEC_READ } codec_mode_t;

typedef struct
{
    codec_mode_t mode;
    const persist_codec_sink_t *sink;
    const persist_codec_source_t *source;
    uint32_t count;
    uint32_t limit;
    uint32_t crc;
    uint8_t crc_enabled;
    persist_codec_result_t result;
} codec_io_t;

static uint32_t codec_crc32_update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    for (uint32_t i = 0U; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320UL & (0U - (crc & 1U)));
    }
    return crc;
}

uint32_t persist_codec_crc32_update(uint32_t crc,
                                    const uint8_t *data,
                                    uint32_t length)
{
    return codec_crc32_update(crc, data, length);
}

static void codec_bytes(codec_io_t *io, uint8_t *data, uint32_t length)
{
    if ((io == NULL) || (io->result != PERSIST_CODEC_OK)) return;
    if ((length > PERSIST_CODEC_MAX_DOCUMENT_BYTES)
            || (io->count > PERSIST_CODEC_MAX_DOCUMENT_BYTES - length)
            || ((io->mode == CODEC_READ) && ((io->count > io->limit)
                    || (length > io->limit - io->count))))
    {
        io->result = PERSIST_CODEC_BAD_LENGTH;
        return;
    }
    if (io->mode == CODEC_WRITE)
    {
        if ((io->sink == NULL) || (io->sink->write == NULL)
                || (io->sink->write(io->sink->context, data, length) == 0U))
        { io->result = PERSIST_CODEC_IO_ERROR; return; }
    }
    else if (io->mode == CODEC_READ)
    {
        if ((io->source == NULL) || (io->source->read == NULL)
                || (io->source->read(io->source->context, data, length) == 0U))
        { io->result = PERSIST_CODEC_IO_ERROR; return; }
    }
    if ((io->crc_enabled != 0U) && (io->mode != CODEC_COUNT))
        io->crc = codec_crc32_update(io->crc, data, length);
    io->count += length;
}

static void codec_u8(codec_io_t *io, uint8_t *value) { codec_bytes(io, value, 1U); }
static void codec_i8(codec_io_t *io, int8_t *value) { codec_bytes(io, (uint8_t *)value, 1U); }

static void codec_u16(codec_io_t *io, uint16_t *value)
{
    uint8_t b[2];
    if (io->mode != CODEC_READ) { b[0]=(uint8_t)*value; b[1]=(uint8_t)(*value>>8U); }
    codec_bytes(io, b, 2U);
    if ((io->mode == CODEC_READ) && (io->result == PERSIST_CODEC_OK))
        *value=(uint16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8U));
}

static void codec_u32(codec_io_t *io, uint32_t *value)
{
    uint8_t b[4];
    if (io->mode != CODEC_READ)
        for (uint8_t i=0U;i<4U;++i) b[i]=(uint8_t)(*value>>(8U*i));
    codec_bytes(io,b,4U);
    if ((io->mode == CODEC_READ) && (io->result == PERSIST_CODEC_OK))
        *value=(uint32_t)b[0]|((uint32_t)b[1]<<8U)|((uint32_t)b[2]<<16U)|((uint32_t)b[3]<<24U);
}

static void codec_i16(codec_io_t *io, int16_t *value)
{ uint16_t u=(uint16_t)*value; codec_u16(io,&u); if(io->mode==CODEC_READ)*value=(int16_t)u; }
static void codec_i32(codec_io_t *io, int32_t *value)
{ uint32_t u=(uint32_t)*value; codec_u32(io,&u); if(io->mode==CODEC_READ)*value=(int32_t)u; }

static void codec_f32(codec_io_t *io, float *value)
{
    uint32_t bits=0U;
    if (io->mode != CODEC_READ) memcpy(&bits,value,4U);
    codec_u32(io,&bits);
    if (io->mode == CODEC_READ) memcpy(value,&bits,4U);
}

static uint8_t codec_family_valid(uint32_t key)
{
    switch(key){case PERSIST_FAMILY_OFF:case PERSIST_FAMILY_SYNTH:case PERSIST_FAMILY_DRUM:
    case PERSIST_FAMILY_MIDI:case PERSIST_FAMILY_SAMPLER:case PERSIST_FAMILY_EXTERNAL:return 1U;default:return 0U;}
}

static uint8_t codec_type_valid(uint32_t key)
{
    switch(key){case PERSIST_TYPE_NONE:case PERSIST_TYPE_PRISM:case PERSIST_TYPE_WAVE:
    case PERSIST_TYPE_STACK:case PERSIST_TYPE_FM:case PERSIST_TYPE_DRUM_MD:
    case PERSIST_TYPE_DRUM_ANALOG_BD:case PERSIST_TYPE_MIDI:case PERSIST_TYPE_RAM_SAMPLE:
    case PERSIST_TYPE_STREAM_SAMPLE:case PERSIST_TYPE_MULTI_SAMPLE:case PERSIST_TYPE_LOOPER:
    case PERSIST_TYPE_EXTERNAL:case PERSIST_TYPE_GROUP:return 1U;default:return 0U;}
}

static uint8_t codec_asset_kind_valid(uint32_t key)
{ return (uint8_t)((key==PERSIST_ASSET_SAMPLE_STREAM)||(key==PERSIST_ASSET_SAMPLE_RAM)||(key==PERSIST_ASSET_MULTI)||(key==PERSIST_ASSET_WAVETABLE)); }
static uint8_t codec_midi_source_valid(uint32_t key)
{ return (uint8_t)((key==PERSIST_MIDI_SOURCE_INTERNAL)||(key==PERSIST_MIDI_SOURCE_EXTERNAL)||(key==PERSIST_MIDI_SOURCE_ALL)); }
static uint8_t codec_clock_valid(uint32_t key)
{ return (uint8_t)((key==PERSIST_CLOCK_INTERNAL)||(key==PERSIST_CLOCK_MIDI)||(key==PERSIST_CLOCK_USB)); }
static uint8_t codec_note_fx_valid(uint32_t key)
{ return (uint8_t)((key==PERSIST_NOTE_FX_OFF)||(key==PERSIST_NOTE_FX_ARP)||(key==PERSIST_NOTE_FX_EUCLID)); }
static uint8_t codec_mod_source_valid(uint32_t key)
{ return (uint8_t)((key==PERSIST_MOD_SOURCE_NONE)||(key==PERSIST_MOD_SOURCE_LFO1)||(key==PERSIST_MOD_SOURCE_LFO2)||(key==PERSIST_MOD_SOURCE_LFO3)||(key==PERSIST_MOD_SOURCE_ENV_FLT)||(key==PERSIST_MOD_SOURCE_ENV_VCA)||(key==PERSIST_MOD_SOURCE_ENV_MOD)||(key==PERSIST_MOD_SOURCE_MULTI1)||(key==PERSIST_MOD_SOURCE_MULTI2)||(key==PERSIST_MOD_SOURCE_SLEW1)||(key==PERSIST_MOD_SOURCE_SLEW2)); }

static void codec_value(codec_io_t *io, persist_control_value_kind_t kind,
                        persist_control_value_t *value)
{
    switch(kind)
    {
        case PERSIST_VALUE_BOOL: codec_u8(io,&value->boolean); break;
        case PERSIST_VALUE_U8: codec_u8(io,&value->u8); break;
        case PERSIST_VALUE_U16: codec_u16(io,&value->u16); break;
        case PERSIST_VALUE_I16: codec_i16(io,&value->i16); break;
        case PERSIST_VALUE_U32: codec_u32(io,&value->u32); break;
        case PERSIST_VALUE_I32: codec_i32(io,&value->i32); break;
        case PERSIST_VALUE_FLOAT32: codec_f32(io,&value->f32); break;
        default: io->result=PERSIST_CODEC_UNKNOWN_KEY; break;
    }
}

static void codec_asset(codec_io_t *io, persist_control_asset_ref_t *a)
{
    codec_u32(io,&a->kind); codec_u16(io,&a->path_length);
    if(a->path_length>PERSIST_CONTROL_ASSET_PATH_BYTES){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    codec_bytes(io,(uint8_t *)a->canonical_path,a->path_length);
    if((io->mode==CODEC_READ)&&(a->path_length<PERSIST_CONTROL_ASSET_PATH_BYTES))a->canonical_path[a->path_length]='\0';
}

static void codec_sequence(codec_io_t *io, persist_control_entity_t *entity,uint8_t group_active)
{
    persist_entity_caps_t caps;
    if (persist_entity_caps_resolve(group_active, entity->entity_id, &caps) == 0U)
    {
        io->result = PERSIST_CODEC_INVALID_ENTITY;
        return;
    }
    persist_control_sequence_t *s=&entity->sequence;
    codec_u8(io,&s->length); codec_u8(io,&s->division); codec_u8(io,&s->quantization); codec_u8(io,&s->swing);
    for(uint8_t step=0U;step<PERSIST_CONTROL_STEP_COUNT;++step)
    {
        persist_control_step_t *st=&s->steps[step];
        codec_u8(io,&st->trigger);codec_u8(io,&st->roll);codec_u8(io,&st->play_count);codec_u8(io,&st->lock_count);
        const uint8_t play_limit = (io->mode == CODEC_READ)
            ? PERSIST_CONTROL_PLAY_ITEM_COUNT : caps.play_limit;
        if((st->play_count>play_limit)
                ||(st->lock_count>PERSIST_CONTROL_STEP_LOCK_COUNT))
        {io->result=(st->play_count>play_limit)?PERSIST_CODEC_INVALID_PLAY:PERSIST_CODEC_INVALID_PLOCK;return;}
        for(uint8_t i=0U;i<st->play_count;++i)
        {
            persist_control_play_item_t *p=&st->play[i];
            codec_u8(io,&p->note);codec_u8(io,&p->velocity);codec_u8(io,&p->length);
            codec_bytes(io,(uint8_t *)&p->microtiming,1U);codec_u8(io,&p->present_mask);
        }
        for(uint8_t i=0U;i<st->lock_count;++i)
        {
            persist_control_step_lock_t *l=&st->locks[i];uint8_t kind=(uint8_t)l->kind;
            codec_u32(io,&l->parameter);codec_u8(io,&l->flags);codec_u8(io,&kind);
            if(io->mode==CODEC_READ)l->kind=(persist_control_value_kind_t)kind;
            codec_value(io,l->kind,&l->value);
        }
    }
}

static void codec_modulation(codec_io_t *io, persist_control_modulation_t *m);
static persist_codec_result_t codec_validate_asset_ref(
    const persist_control_asset_ref_t *asset);

static uint8_t codec_entity_assets_valid(const persist_control_entity_t *entity)
{
    if ((entity == NULL) || (entity->asset_count > PERSIST_CONTROL_TRACK_ASSET_COUNT))
    {
        return 0U;
    }
    uint32_t expected_kind = 0U;
    uint8_t maximum = 0U;
    switch (entity->type)
    {
        case PERSIST_TYPE_WAVE:
            expected_kind = PERSIST_ASSET_WAVETABLE;
            maximum = 2U;
            break;
        case PERSIST_TYPE_RAM_SAMPLE:
            expected_kind = PERSIST_ASSET_SAMPLE_RAM;
            maximum = 1U;
            break;
        case PERSIST_TYPE_STREAM_SAMPLE:
            expected_kind = PERSIST_ASSET_SAMPLE_STREAM;
            maximum = 1U;
            break;
        case PERSIST_TYPE_MULTI_SAMPLE:
            expected_kind = PERSIST_ASSET_MULTI;
            maximum = 1U;
            break;
        default:
            maximum = 0U;
            break;
    }
    if (entity->asset_count > maximum)
    {
        return 0U;
    }
    for (uint8_t i = 0U; i < entity->asset_count; ++i)
    {
        if ((codec_validate_asset_ref(&entity->assets[i]) != PERSIST_CODEC_OK)
            || (entity->assets[i].kind != expected_kind))
        {
            return 0U;
        }
        for (uint8_t j = 0U; j < i; ++j)
        {
            if ((entity->assets[j].kind == entity->assets[i].kind)
                && (entity->assets[j].path_length == entity->assets[i].path_length)
                && (memcmp(entity->assets[j].canonical_path,
                           entity->assets[i].canonical_path,
                           entity->assets[i].path_length) == 0))
            {
                return 0U;
            }
        }
    }
    return 1U;
}

static void codec_fm_state(codec_io_t *io, fm_control_state_t *state)
{
    for (uint8_t op = 0U; op < TRACK_TONE_FM_OPERATOR_COUNT; ++op)
    {
        track_tone_fm_operator_base_t *const value = &state->base.operators[op];
        for (uint8_t i = 0U; i < 4U; ++i) codec_u8(io, &value->rates[i]);
        for (uint8_t i = 0U; i < 4U; ++i) codec_u8(io, &value->levels[i]);
        codec_u8(io, &value->breakpoint);
        codec_u8(io, &value->left_depth); codec_u8(io, &value->right_depth);
        codec_u8(io, &value->left_curve); codec_u8(io, &value->right_curve);
        codec_u8(io, &value->rate_scaling); codec_u8(io, &value->output_level);
        codec_u8(io, &value->mode); codec_u8(io, &value->coarse);
        codec_u8(io, &value->fine); codec_i8(io, &value->detune);
        codec_u8(io, &value->velocity_sensitivity); codec_u8(io, &value->enabled);
    }
    for (uint8_t i = 0U; i < 4U; ++i) codec_u8(io, &state->base.pitch_rates[i]);
    for (uint8_t i = 0U; i < 4U; ++i) codec_u8(io, &state->base.pitch_levels[i]);
    codec_u8(io, &state->base.transpose); codec_u8(io, &state->base.algorithm);
    codec_u8(io, &state->base.feedback); codec_u8(io, &state->base.key_sync);
    codec_f32(io, &state->macros.ratio); codec_f32(io, &state->macros.bright);
    codec_f32(io, &state->macros.body); codec_f32(io, &state->macros.detail);
    codec_f32(io, &state->macros.metal); codec_f32(io, &state->macros.env_attack);
    codec_f32(io, &state->macros.env_decay); codec_f32(io, &state->macros.env_sustain);
    codec_f32(io, &state->macros.env_release); codec_f32(io, &state->macros.play_vel);
    codec_f32(io, &state->macros.play_key); codec_f32(io, &state->macros.pitch_env);
    codec_f32(io, &state->macros.pitch_time);
}

static void codec_float_block(codec_io_t *io, float *values, uint16_t count)
{ for (uint16_t i=0U;i<count;++i) codec_f32(io,&values[i]); }
static void codec_tone(codec_io_t *io,tone_program_control_t*t)
{
    uint8_t tag=(uint8_t)t->tag;codec_u8(io,&tag);if(io->mode==CODEC_READ)t->tag=(track_runtime_type_t)tag;
    switch(t->tag){
    case TRACK_RUNTIME_TYPE_PRISM:codec_float_block(io,&t->state.prism.osc[0].param1,16U);break;
    case TRACK_RUNTIME_TYPE_STACK:codec_float_block(io,&t->state.stack.osc[0].level,18U);break;
    case TRACK_RUNTIME_TYPE_WAVE:codec_float_block(io,&t->state.wave.osc[0].position,10U);break;
    case TRACK_RUNTIME_TYPE_RAM:codec_float_block(io,&t->state.ram.gain,7U);break;
    case TRACK_RUNTIME_TYPE_STREAM:codec_float_block(io,&t->state.stream.gain,8U);break;
    case TRACK_RUNTIME_TYPE_LOOPER:codec_float_block(io,&t->state.looper.xfade,4U);break;
    case TRACK_RUNTIME_TYPE_MULTI:codec_float_block(io,&t->state.multi.gain,2U);break;
    case TRACK_RUNTIME_TYPE_MIDI:codec_float_block(io,&t->state.midi.program,13U);break;
    case TRACK_RUNTIME_TYPE_EXTERNAL:codec_float_block(io,&t->state.midi.program,14U);break;
    case TRACK_RUNTIME_TYPE_DRUM_BD_ANALOG:codec_float_block(io,&t->state.drum_analog.pitch,8U);break;
    case TRACK_RUNTIME_TYPE_DRUM_MD:codec_float_block(io,&t->state.drum_md.model,9U);break;
    case TRACK_RUNTIME_TYPE_NONE:case TRACK_RUNTIME_TYPE_FM:case TRACK_RUNTIME_TYPE_GROUP:break;
    default:io->result=PERSIST_CODEC_INVALID_ENTITY;break;}
}
static void codec_filter(codec_io_t*io,param_filter_control_state_t*s){codec_float_block(io,&s->morph,16U);}
static void codec_vca(codec_io_t*io,vca_control_state_t*s){codec_float_block(io,&s->attack,6U);}
static void codec_env3(codec_io_t*io,mod_env3_control_state_t*s){codec_float_block(io,&s->attack,5U);}
static void codec_mixer(codec_io_t*io,mixer_control_state_t*s){codec_float_block(io,&s->level,5U);}
static void codec_polyphony(codec_io_t*io,polyphony_control_state_t*s){codec_u8(io,&s->voice_count);codec_f32(io,&s->spread);}
static void codec_audio_fx(codec_io_t*io,audio_fx_control_state_t*s){uint8_t pos=(uint8_t)s->config.filter_position,order=(uint8_t)s->config.order;codec_u8(io,&pos);codec_u8(io,&order);if(io->mode==CODEC_READ){s->config.filter_position=(audio_fx_filter_pos_t)pos;s->config.order=(audio_fx_order_t)order;}for(uint8_t i=0U;i<2U;++i){codec_u8(io,&s->config.spatial_mode[i]);codec_u8(io,&s->model[i]);codec_f32(io,&s->p1[i]);codec_f32(io,&s->p2[i]);codec_f32(io,&s->p3[i]);codec_f32(io,&s->group_level[i]);}}
static void codec_global_audio(codec_io_t*io,param_global_control_state_t*s){codec_float_block(io,&s->send_fx[0],54U);}

static void codec_entity(codec_io_t *io, persist_control_entity_t *e,uint8_t group_active)
{
    persist_entity_caps_t caps;
    if (persist_entity_caps_resolve(group_active, e->entity_id, &caps) == 0U)
    {
        io->result = PERSIST_CODEC_INVALID_ENTITY;
        return;
    }
    codec_u8(io,&e->entity_id);codec_u32(io,&e->family);codec_u32(io,&e->type);
    codec_u8(io,&e->midi_channel);codec_u32(io,&e->midi_source_key);codec_u32(io,&e->input_key);
    codec_polyphony(io,&e->polyphony);
    codec_u8(io,&e->muted);codec_u8(io,&e->asset_count);
    if(e->asset_count>PERSIST_CONTROL_TRACK_ASSET_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    for(uint8_t i=0U;i<e->asset_count;++i)codec_asset(io,&e->assets[i]);
    codec_u8(io,&e->fm_present);
    if(e->fm_present>1U){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}
    if(e->fm_present!=0U)codec_fm_state(io,&e->fm);
    codec_u8(io,&e->tone_present);if(e->tone_present>1U){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}if(e->tone_present!=0U)codec_tone(io,&e->tone);
    codec_filter(io,&e->filter);codec_vca(io,&e->vca);codec_mixer(io,&e->mixer);codec_audio_fx(io,&e->audio_fx);
    codec_u8(io,&e->note_fx_count);
    if((e->note_fx_count>PERSIST_CONTROL_NOTE_FX_COUNT)
            ||((io->mode!=CODEC_READ)&&(caps.note_fx_owner==0U)&&(e->note_fx_count!=0U)))
    {io->result=PERSIST_CODEC_INVALID_ENTITY;return;}
    for(uint8_t i=0U;i<e->note_fx_count;++i)
    { codec_u32(io,&e->note_fx[i].model_key);codec_bytes(io,e->note_fx[i].values,PERSIST_CONTROL_NOTE_FX_VALUE_COUNT); }
    codec_u8(io,&e->modulation_present);
    if(e->modulation_present!=0U)codec_modulation(io,&e->modulation);
    codec_sequence(io,e,group_active);
}

static void codec_modulation(codec_io_t *io, persist_control_modulation_t *m)
{
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_LFO_COUNT;++i)
    {codec_f32(io,&m->lfos[i].rate);codec_u32(io,&m->lfos[i].shape_key);codec_u32(io,&m->lfos[i].trigger_key);codec_f32(io,&m->lfos[i].phase_offset);}
    for(uint8_t i=0U;i<2U;++i){codec_u32(io,&m->multi[i].source_a_key);codec_u32(io,&m->multi[i].source_b_key);}
    for(uint8_t i=0U;i<2U;++i){codec_u32(io,&m->slew[i].source_key);codec_f32(io,&m->slew[i].amount);}
    codec_f32(io,&m->envelope.attack);codec_f32(io,&m->envelope.decay);codec_f32(io,&m->envelope.sustain);codec_f32(io,&m->envelope.release);codec_u8(io,&m->envelope.retrigger_hard);
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_ROUTE_COUNT;++i)
    {codec_u32(io,&m->routes[i].source_key);codec_u8(io,&m->routes[i].destination_entity);codec_u32(io,&m->routes[i].destination_parameter);codec_f32(io,&m->routes[i].depth);codec_u8(io,&m->routes[i].enabled);}
}

static void codec_pattern_body(codec_io_t *io, persist_control_pattern_t *p)
{
    uint8_t entity_count=PERSIST_CONTROL_ENTITY_COUNT;codec_u8(io,&entity_count);
    if(entity_count!=PERSIST_CONTROL_ENTITY_COUNT){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}
    const uint8_t group_active=(p->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type==PERSIST_TYPE_GROUP)?1U:0U;
    for(uint8_t i=0U;i<PERSIST_CONTROL_ENTITY_COUNT;++i)codec_entity(io,&p->entities[i],group_active);
    codec_u16(io,&p->route_count);
    if(p->route_count>(PERSIST_CONTROL_ENTITY_COUNT*PERSIST_CONTROL_ENTITY_COUNT)){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    for(uint16_t i=0U;i<p->route_count;++i)
    {codec_u32(io,&p->routes[i].kind);codec_u8(io,&p->routes[i].source);codec_u8(io,&p->routes[i].destination);codec_u8(io,&p->routes[i].enabled);}
    codec_u32(io,&p->globals.tempo_milli_bpm);codec_u32(io,&p->globals.clock_source_key);
    codec_u32(io,&p->globals.record_start_key);codec_u32(io,&p->globals.record_length_key);
    codec_global_audio(io,&p->globals.audio);codec_u8(io,&p->globals.keyboard.root);codec_u8(io,&p->globals.keyboard.scale);codec_u8(io,&p->globals.keyboard.omnichord);codec_u8(io,&p->globals.keyboard.note_order);codec_u8(io,&p->globals.keyboard.chord_override);codec_u8(io,&p->globals.keyboard.mono_last);codec_u8(io,&p->globals.metronome_level);
}

static void codec_macros(codec_io_t *io,persist_control_macros_t *m)
{
    codec_u32(io,&m->hall_switch_key);codec_bytes(io,m->selected_scene,PERSIST_CONTROL_MACRO_COUNT);
    for(uint8_t s=0U;s<PERSIST_CONTROL_MACRO_SCENE_COUNT;++s)
    { codec_u8(io,&m->scenes[s].lock_count);if(m->scenes[s].lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
      for(uint8_t i=0U;i<m->scenes[s].lock_count;++i){persist_control_macro_lock_t *l=&m->scenes[s].locks[i];codec_u8(io,&l->entity);codec_u32(io,&l->parameter);codec_f32(io,&l->scene_value);} }
}

static uint8_t codec_play_value_valid(const persist_control_play_item_t *play)
{
    if ((play == NULL) || ((play->present_mask & (uint8_t)~0x0FU) != 0U)) return 0U;
    if (((play->present_mask & 0x01U) != 0U) && (play->note > 127U)) return 0U;
    if (((play->present_mask & 0x02U) != 0U) && (play->velocity > 127U)) return 0U;
    if (((play->present_mask & 0x04U) != 0U)
            && ((play->length < 1U) || (play->length > 64U))) return 0U;
    if (((play->present_mask & 0x08U) != 0U)
            && ((play->microtiming < -24) || (play->microtiming > 24))) return 0U;
    return 1U;
}

static uint8_t codec_parameter_value_valid(param_id_t id,
                                           persist_control_value_kind_t kind,
                                           const persist_control_value_t *value)
{
    if ((id >= PARAM_COUNT) || (value == NULL)
            || (kind != PERSIST_VALUE_FLOAT32)) return 0U;
    return (uint8_t)(isfinite(value->f32)
            && (value->f32 >= param_registry[id].min)
            && (value->f32 <= param_registry[id].max));
}

static uint8_t codec_plock_value_valid(param_id_t id,
                                       const persist_control_step_lock_t *lock)
{
    if ((id == PARAM_LFO1_SHAPE) || (id == PARAM_LFO2_SHAPE) || (id == PARAM_LFO3_SHAPE))
    { mod_lfo_shape_t ignored;return (uint8_t)((lock->kind == PERSIST_VALUE_U32)
                && persist_key_lfo_shape_from_disk(lock->value.u32,&ignored)); }
    if ((id == PARAM_LFO1_TRIG) || (id == PARAM_LFO2_TRIG) || (id == PARAM_LFO3_TRIG))
    { mod_lfo_trig_mode_t ignored;return (uint8_t)((lock->kind == PERSIST_VALUE_U32)
                && persist_key_lfo_trigger_from_disk(lock->value.u32,&ignored)); }
    if ((id == PARAM_MIDI_FX_S1_MODEL) || (id == PARAM_MIDI_FX_S2_MODEL)
            || (id == PARAM_MIDI_FX_S3_MODEL))
    { note_fx_model_t ignored;return (uint8_t)((lock->kind == PERSIST_VALUE_U32)
                && persist_key_note_fx_from_disk(lock->value.u32,&ignored)); }
    return codec_parameter_value_valid(id,lock->kind,&lock->value);
}

static uint8_t codec_input_key_valid(
    const persist_control_entity_t *entity, uint8_t *out_input)
{
    if ((entity == NULL) || (out_input == NULL)) return 0U;
    if ((entity->family == PERSIST_FAMILY_EXTERNAL)
            && (entity->type == PERSIST_TYPE_EXTERNAL))
        return persist_key_input_from_disk(entity->input_key, out_input);
    if (entity->input_key != PERSIST_INPUT_NONE) return 0U;
    *out_input = ENTITY_AUDIO_SOURCE_LINE;
    return 1U;
}

persist_codec_result_t persist_codec_validate_pattern(const persist_control_pattern_t *p)
{
    if(p==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;
    const uint8_t group_active=(p->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type==PERSIST_TYPE_GROUP)?1U:0U;
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
    { const persist_control_entity_t *x=&p->entities[e];persist_entity_caps_t caps;uint8_t input=0U;if((x->entity_id!=e)||(persist_entity_caps_resolve(group_active,e,&caps)==0U)||(caps.persistable==0U)||(codec_family_valid(x->family)==0U)||(codec_type_valid(x->type)==0U)||(codec_entity_assets_valid(x)==0U)||(x->midi_channel<1U)||(x->midi_channel>16U)||(codec_midi_source_valid(x->midi_source_key)==0U)||(codec_input_key_valid(x,&input)==0U)||(x->polyphony.voice_count<1U)||(x->polyphony.voice_count>8U)||(x->audio_fx.config.filter_position>=3U)||(x->audio_fx.config.order>=2U)||(x->audio_fx.config.spatial_mode[0]>=4U)||(x->audio_fx.config.spatial_mode[1]>=4U)||(x->muted>1U)||(x->fm_present>1U)||(x->tone_present>1U)||(x->fm_present&&x->tone_present)||((caps.input_owner==0U)&&(input!=0U))||((x->family==PERSIST_FAMILY_OFF)&&(x->muted!=0U)))return PERSIST_CODEC_INVALID_ENTITY;
      if((caps.active==0U)&&((x->asset_count!=0U)||(x->fm_present!=0U)||(x->tone_present!=0U)||(x->muted!=0U)||(x->note_fx_count!=0U)||(x->modulation_present!=0U)))return PERSIST_CODEC_INVALID_ENTITY;
      if(x->note_fx_count>PERSIST_CONTROL_NOTE_FX_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;
      if((caps.note_fx_owner==0U)&&(x->note_fx_count!=0U))return PERSIST_CODEC_INVALID_ENTITY;
      if((x->sequence.length<1U)||(x->sequence.length>PERSIST_CONTROL_STEP_COUNT)||((x->sequence.division!=1U)&&(x->sequence.division!=2U)&&(x->sequence.division!=4U)&&(x->sequence.division!=8U))||(x->sequence.quantization>100U)||(x->sequence.swing>100U))return PERSIST_CODEC_INVALID_ENTITY;
      if(x->modulation_present>1U||x->modulation_present!=caps.modulation_owner)return PERSIST_CODEC_INVALID_MODULATION;
      for(uint8_t n=0U;n<x->note_fx_count;++n)if(codec_note_fx_valid(x->note_fx[n].model_key)==0U)return PERSIST_CODEC_UNKNOWN_KEY;
      uint16_t locks=0U;for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){const persist_control_step_t *st=&x->sequence.steps[s];if(st->trigger>1U||st->roll>=SEQ_STEP_ROLL_COUNT||(st->trigger==0U&&st->roll!=SEQ_STEP_ROLL_OFF)||(caps.sequence_owner==0U&&st->trigger!=0U)||st->play_count>caps.play_limit)return PERSIST_CODEC_INVALID_PLAY;for(uint8_t v=0U;v<st->play_count;++v)if(codec_play_value_valid(&st->play[v])==0U)return PERSIST_CODEC_INVALID_PLAY;if(st->lock_count>PERSIST_CONTROL_STEP_LOCK_COUNT||(caps.sequence_owner==0U&&st->lock_count!=0U))return PERSIST_CODEC_INVALID_PLOCK;locks=(uint16_t)(locks+st->lock_count);for(uint8_t i=0U;i<st->lock_count;++i){uint8_t tone_slot=0U;param_id_t id=0U;persist_param_descriptor_t d;if(persist_key_tone_slot_from_disk(st->locks[i].parameter,&tone_slot)!=0U){if((st->locks[i].kind!=PERSIST_VALUE_FLOAT32)||!isfinite(st->locks[i].value.f32)||(st->locks[i].value.f32<0.0f)||(st->locks[i].value.f32>1.0f))return PERSIST_CODEC_INVALID_PLOCK;}else if((persist_key_param_from_disk(st->locks[i].parameter,&id)==0U)||(persist_key_param_descriptor(id,&d)==0U)||(d.plockable==0U)||(codec_plock_value_valid(id,&st->locks[i])==0U))return PERSIST_CODEC_INVALID_PLOCK;for(uint8_t j=0U;j<i;++j)if(st->locks[j].parameter==st->locks[i].parameter)return PERSIST_CODEC_DUPLICATE;}}if(locks>SEQ_PLOCK_POOL_CAP_PER_TRACK)return PERSIST_CODEC_INVALID_PLOCK; }
    uint8_t record_mode=0U;if((p->route_count>(PERSIST_CONTROL_ENTITY_COUNT*PERSIST_CONTROL_ENTITY_COUNT))||(codec_clock_valid(p->globals.clock_source_key)==0U)||(persist_key_record_start_from_disk(p->globals.record_start_key,&record_mode)==0U)||(persist_key_record_length_from_disk(p->globals.record_length_key,&record_mode)==0U))return PERSIST_CODEC_CAPACITY_EXCEEDED;
    for(uint16_t i=0U;i<p->route_count;++i){persist_entity_caps_t source_caps,destination_caps;if((p->routes[i].kind!=PERSIST_ROUTE_LOOPER_SOURCE)||(p->routes[i].source>=PERSIST_CONTROL_ENTITY_COUNT)||(p->routes[i].destination>=PERSIST_CONTROL_ENTITY_COUNT)||(p->routes[i].source==p->routes[i].destination)||(p->routes[i].enabled>1U)||(persist_entity_caps_resolve(group_active,p->routes[i].source,&source_caps)==0U)||(persist_entity_caps_resolve(group_active,p->routes[i].destination,&destination_caps)==0U)||(source_caps.active==0U)||(destination_caps.active==0U))return PERSIST_CODEC_INVALID_ENTITY;for(uint16_t j=0U;j<i;++j)if((p->routes[j].kind==p->routes[i].kind)&&(p->routes[j].source==p->routes[i].source)&&(p->routes[j].destination==p->routes[i].destination))return PERSIST_CODEC_DUPLICATE;}
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){if(p->entities[e].modulation_present==0U)continue;const persist_control_modulation_t*m=&p->entities[e].modulation;
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_LFO_COUNT;++i){mod_lfo_shape_t shape;mod_lfo_trig_mode_t trigger;if((persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&shape)==0U)||(persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&trigger)==0U)||!isfinite(m->lfos[i].rate)||!isfinite(m->lfos[i].phase_offset))return PERSIST_CODEC_INVALID_MODULATION;}
    if(m->envelope.retrigger_hard>1U||!isfinite(m->envelope.attack)||!isfinite(m->envelope.decay)||!isfinite(m->envelope.sustain)||!isfinite(m->envelope.release))return PERSIST_CODEC_INVALID_MODULATION;
    for(uint8_t i=0U;i<2U;++i){uint8_t source;if((persist_key_mod_source_from_disk(m->multi[i].source_a_key,&source)==0U)||(persist_key_mod_source_from_disk(m->multi[i].source_b_key,&source)==0U)||(persist_key_mod_source_from_disk(m->slew[i].source_key,&source)==0U)||!isfinite(m->slew[i].amount))return PERSIST_CODEC_INVALID_MODULATION;}
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_ROUTE_COUNT;++i){const persist_control_mod_route_t *route=&m->routes[i];uint8_t destination_entity;param_id_t destination;if((codec_mod_source_valid(route->source_key)==0U)||(route->enabled>1U)||!isfinite(route->depth))return PERSIST_CODEC_INVALID_MODULATION;if(route->destination_parameter==PERSIST_CONTROL_KEY_NONE){if(route->enabled!=0U||route->destination_entity!=e)return PERSIST_CODEC_INVALID_MODULATION;}else if((persist_key_mod_destination_from_disk(route->destination_entity,route->destination_parameter,group_active,&destination_entity,&destination)==0U)||(persist_entity_mod_destination_allowed(group_active,e,destination_entity)==0U))return PERSIST_CODEC_INVALID_MODULATION;}}
    if ((p->globals.keyboard.root > 11U) || (p->globals.keyboard.scale > 6U)
            || (p->globals.keyboard.omnichord > 1U)
            || (p->globals.keyboard.note_order > 1U)
            || (p->globals.keyboard.chord_override > 1U)
            || (p->globals.keyboard.mono_last > 1U)
            || (p->globals.metronome_level > 127U)) return PERSIST_CODEC_INVALID_ENTITY;
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persist_codec_validate_patch(const persist_control_patch_t *p)
{ if(p==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;if((p->name_length>PERSIST_CONTROL_PATCH_NAME_BYTES)||(codec_family_valid(p->family)==0U)||(codec_type_valid(p->type)==0U)||(p->asset_count>PERSIST_CONTROL_TRACK_ASSET_COUNT)||(p->fm_present>1U)||(p->tone_present>1U)||(p->fm_present&&p->tone_present)||(p->polyphony.voice_count<1U)||(p->polyphony.voice_count>8U))return PERSIST_CODEC_CAPACITY_EXCEEDED;for(uint8_t i=0U;i<p->asset_count;++i)if(codec_validate_asset_ref(&p->assets[i])!=PERSIST_CODEC_OK)return PERSIST_CODEC_INVALID_ASSET;return PERSIST_CODEC_OK; }

typedef void (*codec_body_fn)(codec_io_t *,void *);
static void codec_pattern_adapter(codec_io_t *io,void *p){codec_pattern_body(io,(persist_control_pattern_t *)p);}

static void codec_section(codec_io_t *io,uint16_t type,codec_body_fn fn,void *object)
{
    codec_io_t measure={.mode=CODEC_COUNT,.limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,.result=PERSIST_CODEC_OK};fn(&measure,object);if(measure.result!=PERSIST_CODEC_OK){io->result=measure.result;return;}
    uint16_t version=1U;uint32_t length=measure.count;codec_u16(io,&type);codec_u16(io,&version);codec_u32(io,&length);
    const uint32_t start=io->count;fn(io,object);if((io->result==PERSIST_CODEC_OK)&&(io->count-start!=length))io->result=PERSIST_CODEC_BAD_LENGTH;
}

static uint8_t codec_discard(void *ctx,const uint8_t *data,uint32_t len)
{ (void)ctx;(void)data;(void)len;return 1U; }

static persist_codec_result_t codec_write_document(uint8_t kind,uint16_t sections,codec_body_fn payload,void *object,const persist_codec_sink_t *sink,uint32_t *out_bytes)
{
    codec_io_t pass={.mode=CODEC_COUNT,.count=0U,.limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,.result=PERSIST_CODEC_OK};payload(&pass,object);if(pass.result!=PERSIST_CODEC_OK)return pass.result;
    uint8_t header[PERSIST_CODEC_HEADER_BYTES]={CODEC_MAGIC_0,CODEC_MAGIC_1,CODEC_MAGIC_2,CODEC_MAGIC_3};
    header[4]=(uint8_t)PERSIST_CODEC_VERSION;header[6]=kind;header[8]=(uint8_t)sections;uint32_t total=PERSIST_CODEC_HEADER_BYTES+pass.count;
    for(uint8_t i=0U;i<4U;++i)header[12U+i]=(uint8_t)(total>>(8U*i));
    /* Payload CRC is filled by a deterministic prepass into a bounded 1-byte-discard sink below. */
    codec_io_t hash={.mode=CODEC_WRITE,.limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,.crc=0xFFFFFFFFUL,.crc_enabled=1U,.result=PERSIST_CODEC_OK};
    const persist_codec_sink_t hash_sink={.write=codec_discard,.context=&hash};hash.sink=&hash_sink;payload(&hash,object);if(hash.result!=PERSIST_CODEC_OK)return hash.result;
    uint32_t payload_crc=~hash.crc;
    for(uint8_t i=0U;i<4U;++i)header[16U+i]=(uint8_t)(payload_crc>>(8U*i));
    uint32_t hc=~codec_crc32_update(0xFFFFFFFFUL,header,20U);
    for(uint8_t i=0U;i<4U;++i)header[20U+i]=(uint8_t)(hc>>(8U*i));
    if((sink==NULL)||(sink->write==NULL)||(sink->write(sink->context,header,PERSIST_CODEC_HEADER_BYTES)==0U))return PERSIST_CODEC_IO_ERROR;
    codec_io_t out={.mode=CODEC_WRITE,.sink=sink,.limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,.result=PERSIST_CODEC_OK};payload(&out,object);if(out.result==PERSIST_CODEC_OK&&out_bytes!=NULL)*out_bytes=total;return out.result;
}

static void codec_expect_section(codec_io_t *io,uint16_t expected,codec_body_fn fn,void *object)
{uint16_t type=0U,version=0U;uint32_t length=0U;codec_u16(io,&type);codec_u16(io,&version);codec_u32(io,&length);if((io->result!=PERSIST_CODEC_OK)||(type!=expected)||(version!=1U)||(io->count>io->limit)||(length>io->limit-io->count)){io->result=PERSIST_CODEC_BAD_SECTION;return;}uint32_t old=io->limit,start=io->count;io->limit=start+length;fn(io,object);if((io->result==PERSIST_CODEC_OK)&&(io->count!=io->limit))io->result=PERSIST_CODEC_BAD_LENGTH;io->limit=old;}

typedef struct{const persist_codec_project_source_t*source;uint8_t seen[PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK];} project_encode_ctx_t;
static persist_codec_result_t codec_validate_project_pattern(const persist_control_pattern_t*p){return persist_codec_validate_pattern(p);}
static persist_codec_result_t codec_validate_project_macros(const persist_control_macros_t*m){if(m==NULL||((m->hall_switch_key!=PERSIST_MACRO_HALL_SCENE)&&(m->hall_switch_key!=PERSIST_MACRO_HALL_SWITCH)))return PERSIST_CODEC_UNKNOWN_KEY;for(uint8_t i=0U;i<PERSIST_CONTROL_MACRO_COUNT;++i)if(m->selected_scene[i]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return PERSIST_CODEC_INVALID_ENTITY;for(uint8_t s=0U;s<PERSIST_CONTROL_MACRO_SCENE_COUNT;++s){if(m->scenes[s].lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;for(uint8_t i=0U;i<m->scenes[s].lock_count;++i){const persist_control_macro_lock_t*l=&m->scenes[s].locks[i];param_id_t id;persist_param_descriptor_t d;if(l->entity>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_from_disk(l->parameter,&id)==0U||persist_key_param_descriptor(id,&d)==0U||d.key==0U||d.scope!=PERSIST_PARAM_SCOPE_ENTITY||d.kind!=PERSIST_VALUE_FLOAT32||!isfinite(l->scene_value)||l->scene_value<param_registry[id].min||l->scene_value>param_registry[id].max)return PERSIST_CODEC_UNKNOWN_KEY;for(uint8_t j=0U;j<i;++j)if(m->scenes[s].locks[j].entity==l->entity&&m->scenes[s].locks[j].parameter==l->parameter)return PERSIST_CODEC_DUPLICATE;}}return PERSIST_CODEC_OK;}
static persist_codec_result_t codec_validate_asset_ref(const persist_control_asset_ref_t*a){return(asset_ref_is_canonical(a)!=0U&&codec_asset_kind_valid(a->kind))?PERSIST_CODEC_OK:PERSIST_CODEC_INVALID_ASSET;}
static void codec_project_core_encode(codec_io_t*io,void*v){project_encode_ctx_t*c=v;persist_codec_project_metadata_t m=c->source->metadata;codec_u8(io,&m.active_pattern_bank);codec_u8(io,&m.active_pattern);codec_u16(io,&m.pattern_count);const persist_control_pattern_t*p=(c->source->working_pattern.get!=NULL)?c->source->working_pattern.get(c->source->working_pattern.context):NULL;if(m.active_pattern_bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT||m.active_pattern>=PERSIST_CONTROL_PATTERN_PER_BANK||m.pattern_count>PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK||p==NULL){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}persist_codec_result_t r=codec_validate_project_pattern(p);if(r!=PERSIST_CODEC_OK){io->result=r;return;}codec_pattern_body(io,(persist_control_pattern_t*)p);}
static void codec_project_assets_encode(codec_io_t*io,void*v){project_encode_ctx_t*c=v;uint16_t count=c->source->assets.count;codec_u16(io,&count);if(count>PERSIST_CONTROL_ASSET_COUNT||c->source->assets.get==NULL){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint16_t i=0U;i<count;++i){const persist_control_asset_ref_t*a=c->source->assets.get(c->source->assets.context,i);persist_codec_result_t r=codec_validate_asset_ref(a);if(r!=PERSIST_CODEC_OK){io->result=r;return;}codec_asset(io,(persist_control_asset_ref_t*)a);}}
static void codec_project_macros_encode(codec_io_t*io,void*v){project_encode_ctx_t*c=v;persist_codec_result_t r=codec_validate_project_macros(c->source->macros);if(r!=PERSIST_CODEC_OK){io->result=r;return;}codec_macros(io,(persist_control_macros_t*)c->source->macros);}
static void codec_project_bank_encode(codec_io_t*io,void*v){project_encode_ctx_t*c=v;memset(c->seen,0,sizeof(c->seen));uint16_t count=c->source->metadata.pattern_count;codec_u16(io,&count);for(uint16_t i=0U;i<count;++i){const persist_control_pattern_record_t*r=(c->source->patterns.get!=NULL)?c->source->patterns.get(c->source->patterns.context,i):NULL;if(r==NULL){io->result=PERSIST_CODEC_IO_ERROR;return;}persist_control_pattern_record_t*record=(persist_control_pattern_record_t*)r;codec_u8(io,&record->bank);codec_u8(io,&record->pattern);codec_u8(io,&record->present);codec_pattern_body(io,&record->content);if(record->bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT||record->pattern>=PERSIST_CONTROL_PATTERN_PER_BANK||record->present!=1U){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}uint8_t slot=(uint8_t)(record->bank*PERSIST_CONTROL_PATTERN_PER_BANK+record->pattern);if(c->seen[slot]){io->result=PERSIST_CODEC_DUPLICATE;return;}c->seen[slot]=1U;persist_codec_result_t valid=codec_validate_project_pattern(&record->content);if(valid!=PERSIST_CODEC_OK){io->result=valid;return;}}}
static void codec_project_payload(codec_io_t*io,void*v){codec_section(io,SECTION_PROJECT_CORE,codec_project_core_encode,v);codec_section(io,SECTION_PROJECT_ASSETS,codec_project_assets_encode,v);codec_section(io,SECTION_PROJECT_MACROS,codec_project_macros_encode,v);codec_section(io,SECTION_PROJECT_BANK,codec_project_bank_encode,v);}
static void codec_pattern_payload(codec_io_t *io,void *v){codec_section(io,SECTION_PATTERN_BODY,codec_pattern_adapter,v);}
static void codec_patch_body(codec_io_t *io,void *v){persist_control_patch_t *p=v;codec_u16(io,&p->name_length);if(p->name_length>PERSIST_CONTROL_PATCH_NAME_BYTES){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}codec_bytes(io,(uint8_t *)p->name,p->name_length);codec_u32(io,&p->family);codec_u32(io,&p->type);codec_u8(io,&p->asset_count);if(p->asset_count>PERSIST_CONTROL_TRACK_ASSET_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint8_t i=0U;i<p->asset_count;++i)codec_asset(io,&p->assets[i]);codec_u8(io,&p->fm_present);if(p->fm_present)codec_fm_state(io,&p->fm);codec_u8(io,&p->tone_present);if(p->tone_present)codec_tone(io,&p->tone);codec_filter(io,&p->filter);codec_vca(io,&p->vca);codec_env3(io,&p->env3);codec_audio_fx(io,&p->audio_fx);codec_polyphony(io,&p->polyphony);codec_modulation(io,&p->modulation);}
static void codec_patch_payload(codec_io_t *io,void *v){codec_section(io,SECTION_PATCH_BODY,codec_patch_body,v);}

persist_codec_result_t persist_codec_encode_pattern(const persist_control_pattern_t *p,const persist_codec_sink_t *s,uint32_t *n){persist_codec_result_t r=persist_codec_validate_pattern(p);return(r==PERSIST_CODEC_OK)?codec_write_document(PERSIST_CODEC_DOCUMENT_PATTERN,1U,codec_pattern_payload,(void *)p,s,n):r;}
persist_codec_result_t persist_codec_encode_patch(const persist_control_patch_t *p,const persist_codec_sink_t *s,uint32_t *n){persist_codec_result_t r=persist_codec_validate_patch(p);return(r==PERSIST_CODEC_OK)?codec_write_document(PERSIST_CODEC_DOCUMENT_PATCH,1U,codec_patch_payload,(void *)p,s,n):r;}
persist_codec_result_t persist_codec_encode_project(const persist_codec_project_source_t*p,const persist_codec_sink_t*s,uint32_t*n){if(p==NULL||p->working_pattern.get==NULL||p->assets.get==NULL||p->macros==NULL||p->patterns.get==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;project_encode_ctx_t c={.source=p};return codec_write_document(PERSIST_CODEC_DOCUMENT_PROJECT,4U,codec_project_payload,&c,s,n);}

static persist_codec_result_t codec_emit_project_fragment(codec_io_t *io,
                                                          const persist_codec_sink_t *sink,
                                                          uint32_t *out_bytes)
{
    if (io->result == PERSIST_CODEC_OK)
    {
        if (out_bytes != NULL) *out_bytes = io->count;
        return PERSIST_CODEC_OK;
    }
    (void)sink;
    return io->result;
}

persist_codec_result_t persist_codec_encode_project_core_payload(
    const persist_codec_project_metadata_t *metadata,
    const persist_control_pattern_t *working_pattern,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes)
{
    if ((metadata == NULL) || (working_pattern == NULL) || (sink == NULL)
            || (sink->write == NULL))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    if ((metadata->active_pattern_bank >= PERSIST_CONTROL_PATTERN_BANK_COUNT)
            || (metadata->active_pattern >= PERSIST_CONTROL_PATTERN_PER_BANK)
            || (metadata->pattern_count
                > PERSIST_CONTROL_PATTERN_BANK_COUNT
                    * PERSIST_CONTROL_PATTERN_PER_BANK))
        return PERSIST_CODEC_INVALID_ENTITY;
    persist_codec_result_t result = codec_validate_project_pattern(working_pattern);
    if (result != PERSIST_CODEC_OK) return result;
    codec_io_t io = {.mode=CODEC_WRITE,.sink=sink,
                     .limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,
                     .result=PERSIST_CODEC_OK};
    persist_codec_project_metadata_t copy = *metadata;
    codec_u8(&io, &copy.active_pattern_bank);
    codec_u8(&io, &copy.active_pattern);
    codec_u16(&io, &copy.pattern_count);
    codec_pattern_body(&io, (persist_control_pattern_t *)working_pattern);
    return codec_emit_project_fragment(&io, sink, out_bytes);
}

persist_codec_result_t persist_codec_encode_project_assets_payload(
    const persist_control_asset_ref_t *assets,
    uint16_t asset_count,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes)
{
    if ((sink == NULL) || (sink->write == NULL)
            || ((asset_count != 0U) && (assets == NULL)))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    if (asset_count > PERSIST_CONTROL_ASSET_COUNT)
        return PERSIST_CODEC_CAPACITY_EXCEEDED;
    codec_io_t io = {.mode=CODEC_WRITE,.sink=sink,
                     .limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,
                     .result=PERSIST_CODEC_OK};
    uint16_t count = asset_count;
    codec_u16(&io, &count);
    for (uint16_t i = 0U; i < asset_count; ++i)
    {
        persist_codec_result_t result = codec_validate_asset_ref(&assets[i]);
        if (result != PERSIST_CODEC_OK) return result;
        codec_asset(&io, (persist_control_asset_ref_t *)&assets[i]);
    }
    return codec_emit_project_fragment(&io, sink, out_bytes);
}

persist_codec_result_t persist_codec_encode_project_macros_payload(
    const persist_control_macros_t *macros,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes)
{
    if ((macros == NULL) || (sink == NULL) || (sink->write == NULL))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    persist_codec_result_t result = codec_validate_project_macros(macros);
    if (result != PERSIST_CODEC_OK) return result;
    codec_io_t io = {.mode=CODEC_WRITE,.sink=sink,
                     .limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,
                     .result=PERSIST_CODEC_OK};
    codec_macros(&io, (persist_control_macros_t *)macros);
    return codec_emit_project_fragment(&io, sink, out_bytes);
}

persist_codec_result_t persist_codec_encode_project_pattern_record_payload(
    const persist_control_pattern_record_t *record,
    const persist_codec_sink_t *sink,
    uint32_t *out_bytes)
{
    if ((record == NULL) || (sink == NULL) || (sink->write == NULL))
        return PERSIST_CODEC_INVALID_ARGUMENT;
    if ((record->bank >= PERSIST_CONTROL_PATTERN_BANK_COUNT)
            || (record->pattern >= PERSIST_CONTROL_PATTERN_PER_BANK)
            || (record->present != 1U))
        return PERSIST_CODEC_INVALID_ENTITY;
    persist_codec_result_t result = codec_validate_project_pattern(&record->content);
    if (result != PERSIST_CODEC_OK) return result;
    codec_io_t io = {.mode=CODEC_WRITE,.sink=sink,
                     .limit=PERSIST_CODEC_MAX_DOCUMENT_BYTES,
                     .result=PERSIST_CODEC_OK};
    persist_control_pattern_record_t *copy = (persist_control_pattern_record_t *)record;
    codec_u8(&io, &copy->bank);
    codec_u8(&io, &copy->pattern);
    codec_u8(&io, &copy->present);
    codec_pattern_body(&io, &copy->content);
    return codec_emit_project_fragment(&io, sink, out_bytes);
}

uint8_t persist_codec_build_project_section_header(
    persist_codec_project_section_t section,
    uint32_t payload_bytes,
    uint8_t out_header[8])
{
    static const uint16_t types[PERSIST_CODEC_PROJECT_SECTION_COUNT] = {
        SECTION_PROJECT_CORE, SECTION_PROJECT_ASSETS,
        SECTION_PROJECT_MACROS, SECTION_PROJECT_BANK};
    if ((section >= PERSIST_CODEC_PROJECT_SECTION_COUNT)
            || (out_header == NULL))
        return 0U;
    const uint16_t type = types[section];
    out_header[0] = (uint8_t)type;
    out_header[1] = (uint8_t)(type >> 8U);
    out_header[2] = 1U;
    out_header[3] = 0U;
    for (uint8_t i = 0U; i < 4U; ++i)
        out_header[4U + i] = (uint8_t)(payload_bytes >> (8U * i));
    return 1U;
}

uint8_t persist_codec_build_project_document_header(
    uint32_t total_bytes,
    uint32_t payload_crc,
    uint8_t out_header[PERSIST_CODEC_HEADER_BYTES])
{
    if ((out_header == NULL) || (total_bytes < PERSIST_CODEC_HEADER_BYTES)
            || (total_bytes > PERSIST_CODEC_MAX_DOCUMENT_BYTES))
        return 0U;
    memset(out_header, 0, PERSIST_CODEC_HEADER_BYTES);
    out_header[0]=CODEC_MAGIC_0;out_header[1]=CODEC_MAGIC_1;
    out_header[2]=CODEC_MAGIC_2;out_header[3]=CODEC_MAGIC_3;
    out_header[4]=(uint8_t)PERSIST_CODEC_VERSION;
    out_header[6]=PERSIST_CODEC_DOCUMENT_PROJECT;
    out_header[8]=4U;
    for(uint8_t i=0U;i<4U;++i)
    {
        out_header[12U+i]=(uint8_t)(total_bytes>>(8U*i));
        out_header[16U+i]=(uint8_t)(payload_crc>>(8U*i));
    }
    const uint32_t header_crc = ~codec_crc32_update(0xFFFFFFFFUL,
                                                    out_header, 20U);
    for(uint8_t i=0U;i<4U;++i)
        out_header[20U+i]=(uint8_t)(header_crc>>(8U*i));
    return 1U;
}

static persist_codec_result_t codec_decode_begin(const persist_codec_source_t *s,uint8_t kind,uint16_t sections,codec_io_t *io,uint32_t *expected_crc)
{uint8_t h[PERSIST_CODEC_HEADER_BYTES];if((s==NULL)||(s->read==NULL)||(s->read(s->context,h,sizeof(h))==0U))return PERSIST_CODEC_IO_ERROR;if((h[0]!=CODEC_MAGIC_0)||(h[1]!=CODEC_MAGIC_1)||(h[2]!=CODEC_MAGIC_2)||(h[3]!=CODEC_MAGIC_3))return PERSIST_CODEC_BAD_MAGIC;if((h[4]!=PERSIST_CODEC_VERSION)||(h[5]!=0U))return PERSIST_CODEC_BAD_VERSION;if((h[6]!=kind)||(h[7]!=0U))return PERSIST_CODEC_BAD_DOCUMENT_KIND;if((((uint16_t)h[8]|((uint16_t)h[9]<<8U))!=sections)||(h[10]!=0U)||(h[11]!=0U))return PERSIST_CODEC_BAD_SECTION;uint32_t total=(uint32_t)h[12]|((uint32_t)h[13]<<8U)|((uint32_t)h[14]<<16U)|((uint32_t)h[15]<<24U);if((total<PERSIST_CODEC_HEADER_BYTES)||(total>PERSIST_CODEC_MAX_DOCUMENT_BYTES))return PERSIST_CODEC_BAD_LENGTH;uint32_t hc=(uint32_t)h[20]|((uint32_t)h[21]<<8U)|((uint32_t)h[22]<<16U)|((uint32_t)h[23]<<24U);if(hc!=~codec_crc32_update(0xFFFFFFFFUL,h,20U))return PERSIST_CODEC_BAD_CRC;*expected_crc=(uint32_t)h[16]|((uint32_t)h[17]<<8U)|((uint32_t)h[18]<<16U)|((uint32_t)h[19]<<24U);*io=(codec_io_t){.mode=CODEC_READ,.source=s,.limit=total-PERSIST_CODEC_HEADER_BYTES,.crc=0xFFFFFFFFUL,.crc_enabled=1U,.result=PERSIST_CODEC_OK};return PERSIST_CODEC_OK;}
static persist_codec_result_t codec_decode_end(codec_io_t *io,uint32_t expected){if(io->result!=PERSIST_CODEC_OK)return io->result;if(io->count!=io->limit)return PERSIST_CODEC_BAD_LENGTH;return(~io->crc==expected)?PERSIST_CODEC_OK:PERSIST_CODEC_BAD_CRC;}

persist_codec_result_t persist_codec_decode_pattern(const persist_codec_source_t *s,persist_codec_pattern_staging_t *st){if(st==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;memset(st,0,sizeof(*st));codec_io_t io;uint32_t crc;persist_codec_result_t r=codec_decode_begin(s,PERSIST_CODEC_DOCUMENT_PATTERN,1U,&io,&crc);if(r!=PERSIST_CODEC_OK)return r;codec_expect_section(&io,SECTION_PATTERN_BODY,codec_pattern_adapter,&st->pattern);r=codec_decode_end(&io,crc);return(r==PERSIST_CODEC_OK)?persist_codec_validate_pattern(&st->pattern):r;}
persist_codec_result_t persist_codec_decode_patch(const persist_codec_source_t *s,persist_codec_patch_staging_t *st){if(st==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;memset(st,0,sizeof(*st));codec_io_t io;uint32_t crc;persist_codec_result_t r=codec_decode_begin(s,PERSIST_CODEC_DOCUMENT_PATCH,1U,&io,&crc);if(r!=PERSIST_CODEC_OK)return r;codec_expect_section(&io,SECTION_PATCH_BODY,codec_patch_body,&st->patch);r=codec_decode_end(&io,crc);return(r==PERSIST_CODEC_OK)?persist_codec_validate_patch(&st->patch):r;}
static uint16_t codec_le16(const uint8_t*b){return(uint16_t)((uint16_t)b[0]|((uint16_t)b[1]<<8U));}
static uint32_t codec_le32(const uint8_t*b){return(uint32_t)b[0]|((uint32_t)b[1]<<8U)|((uint32_t)b[2]<<16U)|((uint32_t)b[3]<<24U);}

persist_codec_result_t persist_codec_prevalidate_project(const persist_codec_source_t*s,uint32_t*out_total)
{
    if(s==NULL||s->read==NULL||s->reset==NULL||s->size==NULL||s->reset(s->context)==0U)return PERSIST_CODEC_INVALID_ARGUMENT;
    uint8_t h[PERSIST_CODEC_HEADER_BYTES];if(s->read(s->context,h,sizeof(h))==0U)return PERSIST_CODEC_IO_ERROR;
    if(h[0]!=CODEC_MAGIC_0||h[1]!=CODEC_MAGIC_1||h[2]!=CODEC_MAGIC_2||h[3]!=CODEC_MAGIC_3)return PERSIST_CODEC_BAD_MAGIC;
    if(h[4]!=PERSIST_CODEC_VERSION||h[5]!=0U)return PERSIST_CODEC_BAD_VERSION;
    if(h[6]!=PERSIST_CODEC_DOCUMENT_PROJECT||h[7]!=0U)return PERSIST_CODEC_BAD_DOCUMENT_KIND;
    if(codec_le16(&h[8])!=4U||h[10]!=0U||h[11]!=0U)return PERSIST_CODEC_BAD_SECTION;
    const uint32_t total=codec_le32(&h[12]);uint32_t source_size=0U;if(s->size(s->context,&source_size)==0U)return PERSIST_CODEC_IO_ERROR;if(total<PERSIST_CODEC_HEADER_BYTES||total>PERSIST_CODEC_MAX_DOCUMENT_BYTES||total!=source_size)return PERSIST_CODEC_BAD_LENGTH;
    if(codec_le32(&h[20])!=~codec_crc32_update(0xFFFFFFFFUL,h,20U))return PERSIST_CODEC_BAD_CRC;
    const uint16_t expected[4]={SECTION_PROJECT_CORE,SECTION_PROJECT_ASSETS,SECTION_PROJECT_MACROS,SECTION_PROJECT_BANK};uint32_t consumed=PERSIST_CODEC_HEADER_BYTES,crc=0xFFFFFFFFUL;uint16_t pattern_count=0U;
    for(uint8_t section=0U;section<4U;++section){uint8_t sh[8];if(total-consumed<sizeof(sh)||s->read(s->context,sh,sizeof(sh))==0U)return PERSIST_CODEC_BAD_LENGTH;crc=codec_crc32_update(crc,sh,sizeof(sh));consumed+=sizeof(sh);uint32_t length=codec_le32(&sh[4]);if(codec_le16(sh)!=expected[section]||codec_le16(&sh[2])!=1U||length>total-consumed)return PERSIST_CODEC_BAD_SECTION;uint8_t first[4]={0};uint32_t offset=0U;while(offset<length){uint8_t chunk[256];uint32_t n=length-offset;if(n>sizeof(chunk))n=sizeof(chunk);if(s->read(s->context,chunk,n)==0U)return PERSIST_CODEC_IO_ERROR;crc=codec_crc32_update(crc,chunk,n);for(uint8_t i=0U;i<4U&&offset+i<length;++i)if(offset+i<4U)first[offset+i]=chunk[i];offset+=n;}consumed+=length;if(section==0U){if(length<4U||first[0]>=PERSIST_CONTROL_PATTERN_BANK_COUNT||first[1]>=PERSIST_CONTROL_PATTERN_PER_BANK)return PERSIST_CODEC_INVALID_ENTITY;pattern_count=codec_le16(&first[2]);if(pattern_count>PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK)return PERSIST_CODEC_CAPACITY_EXCEEDED;}else if(section==1U){if(length<2U||codec_le16(first)>PERSIST_CONTROL_ASSET_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;}else if(section==3U){if(length<2U||codec_le16(first)!=pattern_count)return PERSIST_CODEC_BAD_LENGTH;}}
    if(consumed!=total)return PERSIST_CODEC_BAD_LENGTH;
    if(~crc!=codec_le32(&h[16]))return PERSIST_CODEC_BAD_CRC;
    if(out_total!=NULL)*out_total=total;
    return(s->reset(s->context)!=0U)?PERSIST_CODEC_OK:PERSIST_CODEC_IO_ERROR;
}

typedef struct{persist_codec_project_workspace_t*w;const persist_codec_project_consumer_t*project;const persist_codec_pattern_consumer_t*patterns;persist_codec_project_metadata_t metadata;uint8_t mutate;uint8_t pattern_seen[256];} project_decode_ctx_t;
static void codec_project_core_decode(codec_io_t*io,void*v){project_decode_ctx_t*c=v;memset(&c->w->unit.pattern_record,0,sizeof(c->w->unit.pattern_record));codec_u8(io,&c->metadata.active_pattern_bank);codec_u8(io,&c->metadata.active_pattern);codec_u16(io,&c->metadata.pattern_count);codec_pattern_body(io,&c->w->unit.pattern_record.content);if(io->result!=PERSIST_CODEC_OK)return;if(c->metadata.active_pattern_bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT||c->metadata.active_pattern>=PERSIST_CONTROL_PATTERN_PER_BANK||c->metadata.pattern_count>PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK)io->result=PERSIST_CODEC_INVALID_ENTITY;}
static void codec_project_assets_decode(codec_io_t*io,void*v){project_decode_ctx_t*c=v;codec_u16(io,&c->metadata.asset_count);if(c->metadata.asset_count>PERSIST_CONTROL_ASSET_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint16_t i=0U;i<c->metadata.asset_count;++i){persist_control_asset_ref_t*asset=&c->w->assets[i];memset(asset,0,sizeof(*asset));codec_asset(io,asset);if(io->result!=PERSIST_CODEC_OK)return;persist_codec_result_t r=codec_validate_asset_ref(asset);if(r!=PERSIST_CODEC_OK){io->result=r;return;}for(uint16_t j=0U;j<i;++j)if(c->w->assets[j].kind==asset->kind&&c->w->assets[j].path_length==asset->path_length&&memcmp(c->w->assets[j].canonical_path,asset->canonical_path,asset->path_length)==0){io->result=PERSIST_CODEC_DUPLICATE;return;}if(c->project->validate_asset(c->project->context,asset)==0U||(c->mutate&&c->project->put_asset(c->project->context,asset)==0U)){io->result=PERSIST_CODEC_INVALID_ASSET;return;}}}
static void codec_project_macros_decode(codec_io_t*io,void*v){project_decode_ctx_t*c=v;memset(&c->w->unit.macros,0,sizeof(c->w->unit.macros));codec_macros(io,&c->w->unit.macros);if(io->result!=PERSIST_CODEC_OK)return;if(c->mutate&&c->project->apply_macros(c->project->context,&c->w->unit.macros)==0U)io->result=PERSIST_CODEC_IO_ERROR;}
static void codec_project_bank_decode(codec_io_t*io,void*v){project_decode_ctx_t*c=v;uint16_t count=0U;codec_u16(io,&count);if(count!=c->metadata.pattern_count){io->result=PERSIST_CODEC_BAD_LENGTH;return;}memset(c->pattern_seen,0,sizeof(c->pattern_seen));for(uint16_t i=0U;i<count;++i){persist_control_pattern_record_t*r=&c->w->unit.pattern_record;memset(r,0,sizeof(*r));codec_u8(io,&r->bank);codec_u8(io,&r->pattern);codec_u8(io,&r->present);codec_pattern_body(io,&r->content);if(io->result!=PERSIST_CODEC_OK)return;if(r->bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT||r->pattern>=PERSIST_CONTROL_PATTERN_PER_BANK||r->present!=1U){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}uint8_t slot=(uint8_t)(r->bank*PERSIST_CONTROL_PATTERN_PER_BANK+r->pattern);if(c->pattern_seen[slot]){io->result=PERSIST_CODEC_DUPLICATE;return;}c->pattern_seen[slot]=1U;if(c->mutate&&c->patterns->put(c->patterns->context,r)==0U){io->result=PERSIST_CODEC_IO_ERROR;return;}}}
static persist_codec_result_t codec_project_decode_pass(const persist_codec_source_t*s,project_decode_ctx_t*c)
{if(s->reset(s->context)==0U)return PERSIST_CODEC_IO_ERROR;memset(c->w,0,sizeof(*c->w));codec_io_t io;uint32_t crc;persist_codec_result_t r=codec_decode_begin(s,PERSIST_CODEC_DOCUMENT_PROJECT,4U,&io,&crc);if(r!=PERSIST_CODEC_OK)return r;codec_expect_section(&io,SECTION_PROJECT_CORE,codec_project_core_decode,c);codec_expect_section(&io,SECTION_PROJECT_ASSETS,codec_project_assets_decode,c);if(io.result==PERSIST_CODEC_OK&&c->mutate&&c->project->apply_working(c->project->context,&c->metadata,&c->w->unit.pattern_record.content)==0U)io.result=PERSIST_CODEC_IO_ERROR;codec_expect_section(&io,SECTION_PROJECT_MACROS,codec_project_macros_decode,c);codec_expect_section(&io,SECTION_PROJECT_BANK,codec_project_bank_decode,c);return codec_decode_end(&io,crc);}

persist_codec_result_t persist_codec_decode_project_progressive(const persist_codec_source_t*s,persist_codec_project_workspace_t*w,const persist_codec_project_consumer_t*project,const persist_codec_pattern_consumer_t*patterns)
{if(s==NULL||w==NULL||project==NULL||project->begin_assets==NULL||project->validate_asset==NULL||project->put_asset==NULL||project->apply_working==NULL||project->apply_macros==NULL||patterns==NULL||patterns->begin==NULL||patterns->put==NULL||patterns->commit==NULL||patterns->abort==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;persist_codec_result_t r=persist_codec_prevalidate_project(s,NULL);if(r!=PERSIST_CODEC_OK)return r;project_decode_ctx_t c={.w=w,.project=project,.patterns=patterns,.mutate=0U};r=codec_project_decode_pass(s,&c);if(r!=PERSIST_CODEC_OK)return r;if(patterns->begin(patterns->context)==0U)return PERSIST_CODEC_IO_ERROR;if(project->begin_assets(project->context)==0U){patterns->abort(patterns->context);return PERSIST_CODEC_IO_ERROR;}c.mutate=1U;r=codec_project_decode_pass(s,&c);if(r==PERSIST_CODEC_OK&&patterns->commit(patterns->context)==0U)r=PERSIST_CODEC_IO_ERROR;if(r!=PERSIST_CODEC_OK)patterns->abort(patterns->context);return r;}
