#include "Storage/persistent_control_codec.h"
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

static uint8_t codec_value_kind_valid(persist_control_value_kind_t kind)
{ return (uint8_t)((kind>=PERSIST_VALUE_BOOL)&&(kind<=PERSIST_VALUE_FLOAT32)); }

static uint8_t codec_parameter_key_valid(uint32_t key)
{
    param_id_t id=0U;
    return persist_key_param_from_disk(key,&id);
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
{ return (uint8_t)((key==PERSIST_ASSET_SAMPLE)||(key==PERSIST_ASSET_SAMPLE_STREAM)||(key==PERSIST_ASSET_SAMPLE_RAM)||(key==PERSIST_ASSET_MULTI)||(key==PERSIST_ASSET_WAVETABLE)); }
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

static void codec_parameter(codec_io_t *io, persist_control_parameter_t *p)
{
    uint8_t kind=(uint8_t)p->kind; codec_u32(io,&p->key); codec_u8(io,&kind);
    if(io->mode==CODEC_READ)p->kind=(persist_control_value_kind_t)kind;
    codec_value(io,p->kind,&p->value);
}

static void codec_asset(codec_io_t *io, persist_control_asset_ref_t *a)
{
    codec_u32(io,&a->id); codec_u32(io,&a->kind); codec_u16(io,&a->path_length);
    if(a->path_length>PERSIST_CONTROL_ASSET_PATH_BYTES){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    codec_bytes(io,(uint8_t *)a->path,a->path_length);
    if((io->mode==CODEC_READ)&&(a->path_length<PERSIST_CONTROL_ASSET_PATH_BYTES))a->path[a->path_length]='\0';
}

static void codec_sequence(codec_io_t *io, persist_control_entity_t *entity,uint8_t group_active)
{
    persist_control_sequence_t *s=&entity->sequence;
    codec_u8(io,&s->length); codec_u8(io,&s->division); codec_u8(io,&s->quantization); codec_u8(io,&s->swing);
    for(uint8_t step=0U;step<PERSIST_CONTROL_STEP_COUNT;++step)
    {
        persist_control_step_t *st=&s->steps[step];
        codec_u8(io,&st->trigger);codec_u8(io,&st->roll);codec_u8(io,&st->play_count);codec_u8(io,&st->lock_count);
        if((st->play_count>persist_control_entity_play_limit(group_active,entity->entity_id))
                ||(st->lock_count>PERSIST_CONTROL_STEP_LOCK_COUNT))
        {io->result=(st->play_count>persist_control_entity_play_limit(group_active,entity->entity_id))?PERSIST_CODEC_INVALID_PLAY:PERSIST_CODEC_INVALID_PLOCK;return;}
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

static void codec_entity(codec_io_t *io, persist_control_entity_t *e,uint8_t group_active)
{
    codec_u8(io,&e->entity_id);codec_u32(io,&e->family);codec_u32(io,&e->type);
    codec_u8(io,&e->midi_channel);codec_u32(io,&e->midi_source_key);codec_u32(io,&e->input_key);
    codec_u8(io,&e->muted);codec_u32(io,&e->asset);codec_u16(io,&e->parameter_count);
    if(e->parameter_count>PERSIST_CONTROL_ENTITY_PARAM_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    for(uint16_t i=0U;i<e->parameter_count;++i)codec_parameter(io,&e->parameters[i]);
    codec_u8(io,&e->note_fx_count);
    if((e->note_fx_count>PERSIST_CONTROL_NOTE_FX_COUNT)
            ||((persist_control_entity_allows_note_fx(group_active,e->entity_id)==0U)&&(e->note_fx_count!=0U)))
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
    codec_u32(io,&p->globals.record_start_key);codec_u32(io,&p->globals.record_length_key);codec_u16(io,&p->globals.parameter_count);
    if(p->globals.parameter_count>PERSIST_CONTROL_GLOBAL_PARAM_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
    for(uint16_t i=0U;i<p->globals.parameter_count;++i)codec_parameter(io,&p->globals.parameters[i]);
}

static void codec_macros(codec_io_t *io,persist_control_macros_t *m)
{
    codec_u32(io,&m->hall_switch_key);codec_bytes(io,m->selected_scene,PERSIST_CONTROL_MACRO_COUNT);
    for(uint8_t s=0U;s<PERSIST_CONTROL_MACRO_SCENE_COUNT;++s)
    { codec_u8(io,&m->scenes[s].lock_count);if(m->scenes[s].lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}
      for(uint8_t i=0U;i<m->scenes[s].lock_count;++i){persist_control_macro_lock_t *l=&m->scenes[s].locks[i];uint8_t kind=(uint8_t)l->kind;codec_u8(io,&l->entity);codec_u32(io,&l->parameter);codec_u8(io,&kind);if(io->mode==CODEC_READ)l->kind=(persist_control_value_kind_t)kind;codec_value(io,l->kind,&l->value);} }
}

static uint8_t codec_parameter_value_valid(param_id_t id,
                                           persist_control_value_kind_t kind,
                                           const persist_control_value_t *value)
{
    if ((id >= PARAM_COUNT) || (value == NULL)) return 0U;
    if (kind != PERSIST_VALUE_FLOAT32) return 0U;
    return (uint8_t)(isfinite(value->f32)
            && (value->f32 >= param_registry[id].min)
            && (value->f32 <= param_registry[id].max));
}

static persist_codec_result_t codec_validate_parameters(const persist_control_parameter_t *p,
                                                         uint16_t count,
                                                         persist_param_scope_t scope)
{
    for(uint16_t i=0U;i<count;++i)
    { param_id_t id=0U;persist_param_descriptor_t d;if((persist_key_param_from_disk(p[i].key,&id)==0U)||(persist_key_param_descriptor(id,&d)==0U)||(d.persistent==0U)||(d.scope!=scope)||(p[i].kind!=d.kind)||(codec_value_kind_valid(p[i].kind)==0U)||(codec_parameter_value_valid(id,p[i].kind,&p[i].value)==0U))return PERSIST_CODEC_UNKNOWN_KEY;
      for(uint16_t j=0U;j<i;++j)if(p[j].key==p[i].key)return PERSIST_CODEC_DUPLICATE; }
    return PERSIST_CODEC_OK;
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

persist_codec_result_t persist_codec_validate_pattern(const persist_control_pattern_t *p)
{
    if(p==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;
    const uint8_t group_active=(p->entities[PERSIST_CONTROL_GROUP_MASTER_ID].type==PERSIST_TYPE_GROUP)?1U:0U;
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e)
    { const persist_control_entity_t *x=&p->entities[e];uint8_t input=0U;if((x->entity_id!=e)||(codec_family_valid(x->family)==0U)||(codec_type_valid(x->type)==0U)||(x->midi_channel<1U)||(x->midi_channel>16U)||(codec_midi_source_valid(x->midi_source_key)==0U)||(persist_key_input_from_disk(x->input_key,&input)==0U)||(x->muted>1U)||((e>=PERSIST_CONTROL_FIRST_GROUP_CHILD_ID)&&(input!=0U))||((x->family==PERSIST_FAMILY_OFF)&&(x->muted!=0U)))return PERSIST_CODEC_INVALID_ENTITY;
      if((group_active==0U)&&(e>=PERSIST_CONTROL_FIRST_GROUP_CHILD_ID)&&((x->family!=PERSIST_FAMILY_OFF)||(x->type!=PERSIST_TYPE_NONE)))return PERSIST_CODEC_INVALID_ENTITY;
      if((x->parameter_count>PERSIST_CONTROL_ENTITY_PARAM_COUNT)||(x->note_fx_count>PERSIST_CONTROL_NOTE_FX_COUNT))return PERSIST_CODEC_CAPACITY_EXCEEDED;
      if((persist_control_entity_allows_note_fx(group_active,e)==0U)&&(x->note_fx_count!=0U))return PERSIST_CODEC_INVALID_ENTITY;
      if((x->sequence.length<1U)||(x->sequence.length>PERSIST_CONTROL_STEP_COUNT)||((x->sequence.division!=1U)&&(x->sequence.division!=2U)&&(x->sequence.division!=4U)&&(x->sequence.division!=8U))||(x->sequence.quantization>100U)||(x->sequence.swing>100U))return PERSIST_CODEC_INVALID_ENTITY;
      if(x->modulation_present>1U||x->modulation_present!=persist_control_entity_is_mod_owner(group_active,e))return PERSIST_CODEC_INVALID_MODULATION;
      persist_codec_result_t r=codec_validate_parameters(x->parameters,x->parameter_count,PERSIST_PARAM_SCOPE_ENTITY);if(r!=PERSIST_CODEC_OK)return r;
      for(uint8_t n=0U;n<x->note_fx_count;++n)if(codec_note_fx_valid(x->note_fx[n].model_key)==0U)return PERSIST_CODEC_UNKNOWN_KEY;
      uint16_t locks=0U;for(uint8_t s=0U;s<PERSIST_CONTROL_STEP_COUNT;++s){const persist_control_step_t *st=&x->sequence.steps[s];if(st->trigger>1U||st->roll>=SEQ_STEP_ROLL_COUNT||(st->trigger==0U&&st->roll!=SEQ_STEP_ROLL_OFF)||st->play_count>persist_control_entity_play_limit(group_active,e))return PERSIST_CODEC_INVALID_PLAY;for(uint8_t v=0U;v<st->play_count;++v)if(codec_play_value_valid(&st->play[v])==0U)return PERSIST_CODEC_INVALID_PLAY;if(st->lock_count>PERSIST_CONTROL_STEP_LOCK_COUNT)return PERSIST_CODEC_INVALID_PLOCK;locks=(uint16_t)(locks+st->lock_count);for(uint8_t i=0U;i<st->lock_count;++i){param_id_t id=0U;persist_param_descriptor_t d;if((persist_key_param_from_disk(st->locks[i].parameter,&id)==0U)||(persist_key_param_descriptor(id,&d)==0U)||(d.plockable==0U)||(codec_plock_value_valid(id,&st->locks[i])==0U))return PERSIST_CODEC_INVALID_PLOCK;for(uint8_t j=0U;j<i;++j)if(st->locks[j].parameter==st->locks[i].parameter)return PERSIST_CODEC_DUPLICATE;}}if(locks>1024U)return PERSIST_CODEC_INVALID_PLOCK; }
    uint8_t record_mode=0U;if((p->route_count>(PERSIST_CONTROL_ENTITY_COUNT*PERSIST_CONTROL_ENTITY_COUNT))||(codec_clock_valid(p->globals.clock_source_key)==0U)||(persist_key_record_start_from_disk(p->globals.record_start_key,&record_mode)==0U)||(persist_key_record_length_from_disk(p->globals.record_length_key,&record_mode)==0U))return PERSIST_CODEC_CAPACITY_EXCEEDED;
    for(uint16_t i=0U;i<p->route_count;++i){if((p->routes[i].kind!=PERSIST_ROUTE_LOOPER_SOURCE)||(p->routes[i].source>=PERSIST_CONTROL_ENTITY_COUNT)||(p->routes[i].destination>=PERSIST_CONTROL_ENTITY_COUNT)||(p->routes[i].source==p->routes[i].destination)||(p->routes[i].enabled>1U))return PERSIST_CODEC_INVALID_ENTITY;for(uint16_t j=0U;j<i;++j)if((p->routes[j].kind==p->routes[i].kind)&&(p->routes[j].source==p->routes[i].source)&&(p->routes[j].destination==p->routes[i].destination))return PERSIST_CODEC_DUPLICATE;}
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){if(p->entities[e].modulation_present==0U)continue;const persist_control_modulation_t*m=&p->entities[e].modulation;
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_LFO_COUNT;++i){mod_lfo_shape_t shape;mod_lfo_trig_mode_t trigger;if((persist_key_lfo_shape_from_disk(m->lfos[i].shape_key,&shape)==0U)||(persist_key_lfo_trigger_from_disk(m->lfos[i].trigger_key,&trigger)==0U)||!isfinite(m->lfos[i].rate)||!isfinite(m->lfos[i].phase_offset))return PERSIST_CODEC_INVALID_MODULATION;}
    if(m->envelope.retrigger_hard>1U||!isfinite(m->envelope.attack)||!isfinite(m->envelope.decay)||!isfinite(m->envelope.sustain)||!isfinite(m->envelope.release))return PERSIST_CODEC_INVALID_MODULATION;
    for(uint8_t i=0U;i<2U;++i){uint8_t source;if((persist_key_mod_source_from_disk(m->multi[i].source_a_key,&source)==0U)||(persist_key_mod_source_from_disk(m->multi[i].source_b_key,&source)==0U)||(persist_key_mod_source_from_disk(m->slew[i].source_key,&source)==0U)||!isfinite(m->slew[i].amount))return PERSIST_CODEC_INVALID_MODULATION;}
    for(uint8_t i=0U;i<PERSIST_CONTROL_MOD_ROUTE_COUNT;++i){const persist_control_mod_route_t *route=&m->routes[i];uint8_t destination_entity;param_id_t destination;if((codec_mod_source_valid(route->source_key)==0U)||(route->enabled>1U)||!isfinite(route->depth))return PERSIST_CODEC_INVALID_MODULATION;if(route->destination_parameter==PERSIST_CONTROL_KEY_NONE){if(route->enabled!=0U||route->destination_entity!=e)return PERSIST_CODEC_INVALID_MODULATION;}else if((persist_key_mod_destination_from_disk(route->destination_entity,route->destination_parameter,group_active,&destination_entity,&destination)==0U)||((persist_control_entity_role(group_active,e)!=PERSIST_ENTITY_ROLE_GROUP_MASTER)&&(destination_entity!=e))||((persist_control_entity_role(group_active,e)==PERSIST_ENTITY_ROLE_GROUP_MASTER)&&(destination_entity<PERSIST_CONTROL_GROUP_MASTER_ID)))return PERSIST_CODEC_INVALID_MODULATION;}}
    return codec_validate_parameters(p->globals.parameters,p->globals.parameter_count,PERSIST_PARAM_SCOPE_GLOBAL);
}

persist_codec_result_t persist_codec_validate_project(const persist_control_project_t *p)
{
    if(p==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;
    if((p->active_pattern_bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT)||(p->active_pattern>=PERSIST_CONTROL_PATTERN_PER_BANK)||(p->pattern_count>(PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK))||(p->asset_count>PERSIST_CONTROL_ASSET_COUNT))return PERSIST_CODEC_CAPACITY_EXCEEDED;
    persist_codec_result_t r=persist_codec_validate_pattern(&p->working_pattern);if(r!=PERSIST_CODEC_OK)return r;
    for(uint16_t i=0U;i<p->asset_count;++i){const persist_control_asset_ref_t *a=&p->assets[i];if((a->id==PERSIST_CONTROL_ASSET_NONE)||(codec_asset_kind_valid(a->kind)==0U)||(a->path_length==0U)||(a->path_length>PERSIST_CONTROL_ASSET_PATH_BYTES))return PERSIST_CODEC_INVALID_ASSET;for(uint16_t j=0U;j<i;++j)if(p->assets[j].id==a->id)return PERSIST_CODEC_DUPLICATE;}
    if((p->macros.hall_switch_key!=PERSIST_MACRO_HALL_SCENE)&&(p->macros.hall_switch_key!=PERSIST_MACRO_HALL_SWITCH))return PERSIST_CODEC_UNKNOWN_KEY;
    for(uint8_t m=0U;m<PERSIST_CONTROL_MACRO_COUNT;++m)if(p->macros.selected_scene[m]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return PERSIST_CODEC_INVALID_ENTITY;
    for(uint8_t s=0U;s<PERSIST_CONTROL_MACRO_SCENE_COUNT;++s){const persist_control_macro_scene_t*scene=&p->macros.scenes[s];if(scene->lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;for(uint8_t i=0U;i<scene->lock_count;++i){param_id_t id;persist_param_descriptor_t d;const persist_control_macro_lock_t*l=&scene->locks[i];if(l->entity>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_from_disk(l->parameter,&id)==0U||persist_key_param_descriptor(id,&d)==0U||d.persistent==0U||l->kind!=d.kind||!isfinite(l->value.f32))return PERSIST_CODEC_UNKNOWN_KEY;for(uint8_t j=0U;j<i;++j)if(scene->locks[j].entity==l->entity&&scene->locks[j].parameter==l->parameter)return PERSIST_CODEC_DUPLICATE;}}
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){persist_control_asset_id_t id=p->working_pattern.entities[e].asset;if(id!=PERSIST_CONTROL_ASSET_NONE){uint8_t found=0U;for(uint16_t i=0U;i<p->asset_count;++i)if(p->assets[i].id==id){found=1U;break;}if(found==0U)return PERSIST_CODEC_INVALID_ASSET;}}
    for(uint8_t s=0U;s<PERSIST_CONTROL_MACRO_SCENE_COUNT;++s){if(p->macros.scenes[s].lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT)return PERSIST_CODEC_CAPACITY_EXCEEDED;for(uint8_t i=0U;i<p->macros.scenes[s].lock_count;++i){const persist_control_macro_lock_t *l=&p->macros.scenes[s].locks[i];if((l->entity>=PERSIST_CONTROL_ENTITY_COUNT)||(codec_parameter_key_valid(l->parameter)==0U)||(codec_value_kind_valid(l->kind)==0U))return PERSIST_CODEC_UNKNOWN_KEY;}}
    return PERSIST_CODEC_OK;
}

persist_codec_result_t persist_codec_validate_patch(const persist_control_patch_t *p)
{ if(p==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;if((p->name_length>PERSIST_CONTROL_PATCH_NAME_BYTES)||(codec_family_valid(p->family)==0U)||(codec_type_valid(p->type)==0U)||(p->parameter_count>PERSIST_CONTROL_ENTITY_PARAM_COUNT))return PERSIST_CODEC_CAPACITY_EXCEEDED;if((p->asset.id!=PERSIST_CONTROL_ASSET_NONE)&&((codec_asset_kind_valid(p->asset.kind)==0U)||(p->asset.path_length==0U)||(p->asset.path_length>PERSIST_CONTROL_ASSET_PATH_BYTES)))return PERSIST_CODEC_INVALID_ASSET;return codec_validate_parameters(p->parameters,p->parameter_count,PERSIST_PARAM_SCOPE_ENTITY); }

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

typedef struct{persist_control_project_t *p;persist_codec_pattern_provider_t provider;persist_codec_pattern_consumer_t consumer;persist_control_pattern_record_t *record;uint8_t seen[PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK];} project_ctx_t;
static persist_codec_result_t codec_validate_project_assets(const persist_control_project_t *p,const persist_control_pattern_t *pattern){for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){persist_control_asset_id_t id=pattern->entities[e].asset;if(id!=PERSIST_CONTROL_ASSET_NONE){uint8_t found=0U;for(uint16_t i=0U;i<p->asset_count;++i)if(p->assets[i].id==id){found=1U;break;}if(found==0U)return PERSIST_CODEC_INVALID_ASSET;}}return PERSIST_CODEC_OK;}
static void codec_project_core(codec_io_t *io,void *v){project_ctx_t *c=v;codec_u8(io,&c->p->active_pattern_bank);codec_u8(io,&c->p->active_pattern);codec_u16(io,&c->p->pattern_count);codec_pattern_body(io,&c->p->working_pattern);}
static void codec_project_assets(codec_io_t *io,void *v){project_ctx_t *c=v;codec_u16(io,&c->p->asset_count);if(c->p->asset_count>PERSIST_CONTROL_ASSET_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint16_t i=0U;i<c->p->asset_count;++i)codec_asset(io,&c->p->assets[i]);}
static void codec_project_macros(codec_io_t *io,void *v){codec_macros(io,&((project_ctx_t *)v)->p->macros);}
static void codec_project_bank(codec_io_t *io,void *v){project_ctx_t *c=v;memset(c->seen,0,sizeof(c->seen));uint16_t count=c->p->pattern_count;codec_u16(io,&count);if((count!=c->p->pattern_count)||(count>(PERSIST_CONTROL_PATTERN_BANK_COUNT*PERSIST_CONTROL_PATTERN_PER_BANK))){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint16_t i=0U;i<count;++i){if(io->mode!=CODEC_READ){if(c->provider.get==NULL){io->result=PERSIST_CODEC_IO_ERROR;return;}c->record=(persist_control_pattern_record_t *)c->provider.get(c->provider.context,i);if(c->record==NULL){io->result=PERSIST_CODEC_IO_ERROR;return;}}codec_u8(io,&c->record->bank);codec_u8(io,&c->record->pattern);codec_u8(io,&c->record->present);codec_pattern_body(io,&c->record->content);if(io->result!=PERSIST_CODEC_OK)return;if((c->record->bank>=PERSIST_CONTROL_PATTERN_BANK_COUNT)||(c->record->pattern>=PERSIST_CONTROL_PATTERN_PER_BANK)||(c->record->present!=1U)){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}uint8_t slot=(uint8_t)(c->record->bank*PERSIST_CONTROL_PATTERN_PER_BANK+c->record->pattern);if(c->seen[slot]!=0U){io->result=PERSIST_CODEC_DUPLICATE;return;}c->seen[slot]=1U;persist_codec_result_t r=persist_codec_validate_pattern(&c->record->content);if(r==PERSIST_CODEC_OK)r=codec_validate_project_assets(c->p,&c->record->content);if(r!=PERSIST_CODEC_OK){io->result=r;return;}if((io->mode==CODEC_READ)&&((c->consumer.put==NULL)||(c->consumer.put(c->consumer.context,c->record)==0U))){io->result=PERSIST_CODEC_IO_ERROR;return;}}}
static void codec_project_payload(codec_io_t *io,void *v){codec_section(io,SECTION_PROJECT_CORE,codec_project_core,v);codec_section(io,SECTION_PROJECT_ASSETS,codec_project_assets,v);codec_section(io,SECTION_PROJECT_MACROS,codec_project_macros,v);codec_section(io,SECTION_PROJECT_BANK,codec_project_bank,v);}
static void codec_pattern_payload(codec_io_t *io,void *v){codec_section(io,SECTION_PATTERN_BODY,codec_pattern_adapter,v);}
static void codec_patch_body(codec_io_t *io,void *v){persist_control_patch_t *p=v;codec_u16(io,&p->name_length);if(p->name_length>PERSIST_CONTROL_PATCH_NAME_BYTES){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}codec_bytes(io,(uint8_t *)p->name,p->name_length);codec_u32(io,&p->family);codec_u32(io,&p->type);codec_asset(io,&p->asset);codec_u16(io,&p->parameter_count);if(p->parameter_count>PERSIST_CONTROL_ENTITY_PARAM_COUNT){io->result=PERSIST_CODEC_CAPACITY_EXCEEDED;return;}for(uint16_t i=0U;i<p->parameter_count;++i)codec_parameter(io,&p->parameters[i]);}
static void codec_patch_payload(codec_io_t *io,void *v){codec_section(io,SECTION_PATCH_BODY,codec_patch_body,v);}

persist_codec_result_t persist_codec_encode_pattern(const persist_control_pattern_t *p,const persist_codec_sink_t *s,uint32_t *n){persist_codec_result_t r=persist_codec_validate_pattern(p);return(r==PERSIST_CODEC_OK)?codec_write_document(PERSIST_CODEC_DOCUMENT_PATTERN,1U,codec_pattern_payload,(void *)p,s,n):r;}
persist_codec_result_t persist_codec_encode_patch(const persist_control_patch_t *p,const persist_codec_sink_t *s,uint32_t *n){persist_codec_result_t r=persist_codec_validate_patch(p);return(r==PERSIST_CODEC_OK)?codec_write_document(PERSIST_CODEC_DOCUMENT_PATCH,1U,codec_patch_payload,(void *)p,s,n):r;}
persist_codec_result_t persist_codec_encode_project(const persist_control_project_t *p,const persist_codec_pattern_provider_t *patterns,const persist_codec_sink_t *s,uint32_t *n){persist_codec_result_t r=persist_codec_validate_project(p);if((r!=PERSIST_CODEC_OK)||(patterns==NULL)||(patterns->get==NULL))return(r!=PERSIST_CODEC_OK)?r:PERSIST_CODEC_INVALID_ARGUMENT;project_ctx_t c={.p=(persist_control_project_t *)p,.provider=*patterns};return codec_write_document(PERSIST_CODEC_DOCUMENT_PROJECT,4U,codec_project_payload,&c,s,n);}

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

typedef struct{persist_codec_project_workspace_t*w;persist_codec_pattern_consumer_t consumer;uint16_t count;uint16_t asset_count;uint8_t seen[256];} progressive_bank_ctx_t;
static void codec_project_bank_progressive(codec_io_t*io,void*v){progressive_bank_ctx_t*c=v;uint16_t count=0U;codec_u16(io,&count);if(count!=c->count){io->result=PERSIST_CODEC_BAD_LENGTH;return;}memset(c->seen,0,sizeof(c->seen));for(uint16_t i=0U;i<count;++i){persist_control_pattern_record_t*r=&c->w->unit.pattern_record;memset(r,0,sizeof(*r));codec_u8(io,&r->bank);codec_u8(io,&r->pattern);codec_u8(io,&r->present);codec_pattern_body(io,&r->content);if(io->result!=PERSIST_CODEC_OK)return;if(r->bank>=16U||r->pattern>=16U||r->present!=1U){io->result=PERSIST_CODEC_INVALID_ENTITY;return;}uint8_t slot=(uint8_t)(r->bank*16U+r->pattern);if(c->seen[slot]!=0U){io->result=PERSIST_CODEC_DUPLICATE;return;}c->seen[slot]=1U;persist_codec_result_t valid=persist_codec_validate_pattern(&r->content);for(uint8_t e=0U;valid==PERSIST_CODEC_OK&&e<PERSIST_CONTROL_ENTITY_COUNT;++e){uint32_t id=r->content.entities[e].asset;if(id==0U)continue;uint16_t a=0U;while(a<c->asset_count&&c->w->asset_ids[a]!=id)++a;if(a==c->asset_count)valid=PERSIST_CODEC_INVALID_ASSET;}if(valid!=PERSIST_CODEC_OK){io->result=valid;return;}if(c->consumer.put(c->consumer.context,r)==0U){io->result=PERSIST_CODEC_IO_ERROR;return;}}}

static uint8_t codec_project_discard_pattern(void*context,const persist_control_pattern_record_t*record)
{(void)context;(void)record;return 1U;}

static persist_codec_result_t codec_project_full_preflight(const persist_codec_source_t*s,
                                                            persist_codec_project_workspace_t*w)
{
    if(s->reset(s->context)==0U)return PERSIST_CODEC_IO_ERROR;
    memset(w,0,sizeof(*w));codec_io_t io;uint32_t crc;
    persist_codec_result_t r=codec_decode_begin(s,PERSIST_CODEC_DOCUMENT_PROJECT,4U,&io,&crc);
    if(r!=PERSIST_CODEC_OK)return r;
    project_ctx_t c={.p=&w->unit.project};
    codec_expect_section(&io,SECTION_PROJECT_CORE,codec_project_core,&c);
    codec_expect_section(&io,SECTION_PROJECT_ASSETS,codec_project_assets,&c);
    codec_expect_section(&io,SECTION_PROJECT_MACROS,codec_project_macros,&c);
    if(io.result!=PERSIST_CODEC_OK)return io.result;
    r=persist_codec_validate_project(&w->unit.project);if(r!=PERSIST_CODEC_OK)return r;
    const uint16_t count=w->unit.project.pattern_count;
    const uint16_t asset_count=w->unit.project.asset_count;
    for(uint16_t i=0U;i<asset_count;++i)w->asset_ids[i]=w->unit.project.assets[i].id;
    progressive_bank_ctx_t bank={.w=w,.consumer={.put=codec_project_discard_pattern},.count=count,.asset_count=asset_count};
    codec_expect_section(&io,SECTION_PROJECT_BANK,codec_project_bank_progressive,&bank);
    return codec_decode_end(&io,crc);
}

persist_codec_result_t persist_codec_decode_project_progressive(const persist_codec_source_t*s,persist_codec_project_workspace_t*w,const persist_codec_project_applier_t*apply,const persist_codec_pattern_consumer_t*patterns)
{
    if(w==NULL||apply==NULL||apply->apply_core==NULL||patterns==NULL||patterns->put==NULL||patterns->commit==NULL||patterns->abort==NULL)return PERSIST_CODEC_INVALID_ARGUMENT;
    persist_codec_result_t r=persist_codec_prevalidate_project(s,NULL);if(r!=PERSIST_CODEC_OK)return r;
    r=codec_project_full_preflight(s,w);if(r!=PERSIST_CODEC_OK)return r;
    if(s->reset(s->context)==0U)return PERSIST_CODEC_IO_ERROR;
    memset(w,0,sizeof(*w));codec_io_t io;uint32_t crc;r=codec_decode_begin(s,PERSIST_CODEC_DOCUMENT_PROJECT,4U,&io,&crc);if(r!=PERSIST_CODEC_OK)return r;
    project_ctx_t c={.p=&w->unit.project};codec_expect_section(&io,SECTION_PROJECT_CORE,codec_project_core,&c);codec_expect_section(&io,SECTION_PROJECT_ASSETS,codec_project_assets,&c);codec_expect_section(&io,SECTION_PROJECT_MACROS,codec_project_macros,&c);
    if(io.result==PERSIST_CODEC_OK)r=persist_codec_validate_project(&w->unit.project);else r=io.result;
    uint16_t count=0U,asset_count=0U;if(r==PERSIST_CODEC_OK){count=w->unit.project.pattern_count;asset_count=w->unit.project.asset_count;for(uint16_t i=0U;i<asset_count;++i)w->asset_ids[i]=w->unit.project.assets[i].id;if(apply->apply_core(apply->context,&w->unit.project)==0U)r=PERSIST_CODEC_IO_ERROR;}
    if(r==PERSIST_CODEC_OK){progressive_bank_ctx_t bank={.w=w,.consumer=*patterns,.count=count,.asset_count=asset_count};codec_expect_section(&io,SECTION_PROJECT_BANK,codec_project_bank_progressive,&bank);r=codec_decode_end(&io,crc);}
    if(r==PERSIST_CODEC_OK&&patterns->commit(patterns->context)==0U)r=PERSIST_CODEC_IO_ERROR;
    if(r!=PERSIST_CODEC_OK)patterns->abort(patterns->context);
    return r;
}
