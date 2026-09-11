#include "Track/tone_param_codec.h"

#include <stddef.h>

#include "Param/tone_param_catalog.h"
#include "Seq/seq_types.h"

#define TONE_PARAM_CODEC_ITEM(id) id,
static const param_id_t prism[] = { TONE_PARAM_CATALOG_PRISM(TONE_PARAM_CODEC_ITEM) };
static const param_id_t stack[] = { TONE_PARAM_CATALOG_STACK(TONE_PARAM_CODEC_ITEM) };
static const param_id_t fm[] = { TONE_PARAM_CATALOG_FM(TONE_PARAM_CODEC_ITEM) };
static const param_id_t wave[] = { TONE_PARAM_CATALOG_WAVE(TONE_PARAM_CODEC_ITEM) };
static const param_id_t tb303[] = { TONE_PARAM_CATALOG_TB303(TONE_PARAM_CODEC_ITEM) };
static const param_id_t ram[] = { TONE_PARAM_CATALOG_RAM(TONE_PARAM_CODEC_ITEM) };
static const param_id_t stream[] = { TONE_PARAM_CATALOG_STREAM(TONE_PARAM_CODEC_ITEM) };
static const param_id_t looper[] = { TONE_PARAM_CATALOG_LOOPER(TONE_PARAM_CODEC_ITEM) };
static const param_id_t multi[] = { TONE_PARAM_CATALOG_MULTI(TONE_PARAM_CODEC_ITEM) };
static const param_id_t midi[] = { TONE_PARAM_CATALOG_MIDI(TONE_PARAM_CODEC_ITEM) };
static const param_id_t external[] = { TONE_PARAM_CATALOG_EXTERNAL(TONE_PARAM_CODEC_ITEM) };
static const param_id_t md[] = { TONE_PARAM_CATALOG_DRUM_MD(TONE_PARAM_CODEC_ITEM) };
#undef TONE_PARAM_CODEC_ITEM

_Static_assert((PARAM_DRUM_MD_P8 - PARAM_DRUM_MD_MODEL) == 8U,
               "Drum MD parameter ABI range changed");

static uint8_t table_for(track_runtime_type_t type,const param_id_t **table,uint8_t *count)
{
 if(table==NULL||count==NULL)return 0U;
#define T(x) do{*table=(x);*count=(uint8_t)(sizeof(x)/sizeof((x)[0]));return 1U;}while(0)
 switch(type){
 case TRACK_RUNTIME_TYPE_PRISM:T(prism); case TRACK_RUNTIME_TYPE_STACK:T(stack);
 case TRACK_RUNTIME_TYPE_WAVE:T(wave); case TRACK_RUNTIME_TYPE_RAM:T(ram);
 case TRACK_RUNTIME_TYPE_TB303:T(tb303);
 case TRACK_RUNTIME_TYPE_FM:T(fm);
 case TRACK_RUNTIME_TYPE_STREAM:T(stream); case TRACK_RUNTIME_TYPE_LOOPER:T(looper);
 case TRACK_RUNTIME_TYPE_MULTI:T(multi); case TRACK_RUNTIME_TYPE_MIDI:T(midi);
 case TRACK_RUNTIME_TYPE_EXTERNAL:T(external);
 case TRACK_RUNTIME_TYPE_DRUM_MD:T(md); default:return 0U;}
#undef T
}
uint8_t tone_param_codec_count(track_runtime_type_t type){const param_id_t*t;uint8_t n;return table_for(type,&t,&n)?n:0U;}
uint8_t tone_param_codec_slot_to_param(track_runtime_type_t type,uint8_t slot,param_id_t*out)
{const param_id_t*t;uint8_t n;if(out==NULL||!table_for(type,&t,&n)||slot>=n)return 0U;*out=t[slot];return 1U;}
uint8_t tone_param_codec_param_to_slot(track_runtime_type_t type,param_id_t id,uint8_t*out)
{const param_id_t*t;uint8_t n;if(out==NULL||!table_for(type,&t,&n))return 0U;for(uint8_t i=0;i<n;++i)if(t[i]==id){*out=i;return 1U;}return 0U;}
