#include "Core/project_control.h"

#include "Param/param_macro.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_pool.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Storage/memory_layout.h"
#include "Storage/persistent_key_catalog.h"

#include <string.h>

#define PROJECT_CONTROL_ASSET_PATH_BYTES (PERSIST_CONTROL_ASSET_PATH_BYTES + 1U)
#define PROJECT_CONTROL_INVALID_RUNTIME 0xFFFFU
#define PROJECT_CONTROL_ENTITY_ASSET_BASE 0x01000000UL
#define PROJECT_CONTROL_STREAM_ASSET_BASE 0x11000000UL
#define PROJECT_CONTROL_RAM_ASSET_BASE 0x12000000UL
#define PROJECT_CONTROL_WAVE_ASSET_BASE 0x13000000UL
#define PROJECT_CONTROL_MULTI_ASSET_BASE 0x14000000UL

typedef struct { uint8_t used; uint32_t kind; char path[PROJECT_CONTROL_ASSET_PATH_BYTES]; } project_control_entity_asset_t;
typedef struct { uint8_t used; uint32_t kind; char path[PROJECT_CONTROL_ASSET_PATH_BYTES]; uint16_t runtime; } project_control_bank_slot_t;

CONTROL_STATE_SDRAM static persist_control_macros_t g_macros;
CONTROL_STATE_SDRAM static project_control_entity_asset_t g_entity_assets[PERSIST_CONTROL_ENTITY_COUNT];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_sample_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_wavetable_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_multi_bank[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

static uint8_t text_copy(char*dst,const char*src){uint16_t i=0U;if(dst==NULL||src==NULL)return 0U;while(i+1U<PROJECT_CONTROL_ASSET_PATH_BYTES&&src[i]!='\0'){dst[i]=src[i];++i;}dst[i]='\0';return src[i]=='\0';}
static uint32_t entity_asset_id(uint8_t entity){return PROJECT_CONTROL_ENTITY_ASSET_BASE+(uint32_t)entity;}
static uint8_t bank_set(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint32_t kind,const char*path,uint16_t runtime){if(bank==NULL||logical>=capacity||path==NULL||path[0]=='\0')return 0U;project_control_bank_slot_t next={.used=1U,.kind=kind,.runtime=runtime};if(text_copy(next.path,path)==0U)return 0U;bank[logical]=next;return 1U;}
static uint8_t bank_register(project_control_bank_slot_t*bank,uint16_t capacity,uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){if(bank==NULL||path==NULL||path[0]=='\0')return 0U;for(uint16_t i=0U;i<capacity;++i)if(bank[i].used!=0U&&bank[i].kind==kind&&strcmp(bank[i].path,path)==0){bank[i].runtime=runtime;if(out_logical!=NULL)*out_logical=i;return 1U;}for(uint16_t i=0U;i<capacity;++i)if(bank[i].used!=0U&&bank[i].runtime==runtime){if(bank_set(bank,capacity,i,kind,path,runtime)==0U)return 0U;if(out_logical!=NULL)*out_logical=i;return 1U;}for(uint16_t i=0U;i<capacity;++i)if(bank[i].used==0U){if(bank_set(bank,capacity,i,kind,path,runtime)==0U)return 0U;if(out_logical!=NULL)*out_logical=i;return 1U;}return 0U;}
static void bank_unregister(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t runtime){for(uint16_t i=0U;i<capacity;++i)if(bank[i].used!=0U&&bank[i].runtime==runtime){memset(&bank[i],0,sizeof(bank[i]));return;}}
static uint8_t bank_resolve(const project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint16_t*out_runtime){if(bank==NULL||out_runtime==NULL||logical>=capacity||bank[logical].used==0U||bank[logical].runtime==PROJECT_CONTROL_INVALID_RUNTIME)return 0U;*out_runtime=bank[logical].runtime;return 1U;}
static uint32_t bank_asset_id(uint32_t kind,uint16_t logical){uint32_t base=(kind==PERSIST_ASSET_SAMPLE_STREAM)?PROJECT_CONTROL_STREAM_ASSET_BASE:(kind==PERSIST_ASSET_SAMPLE_RAM)?PROJECT_CONTROL_RAM_ASSET_BASE:(kind==PERSIST_ASSET_WAVETABLE)?PROJECT_CONTROL_WAVE_ASSET_BASE:PROJECT_CONTROL_MULTI_ASSET_BASE;return base+(uint32_t)logical;}
static uint8_t bank_asset_decode(uint32_t id,uint32_t*out_kind,uint16_t*out_logical){const uint32_t base=id&0xFF000000UL;if(out_kind==NULL||out_logical==NULL||(id&0x00FF0000UL)!=0U)return 0U;*out_logical=(uint16_t)(id&0xFFFFU);if(base==PROJECT_CONTROL_STREAM_ASSET_BASE)*out_kind=PERSIST_ASSET_SAMPLE_STREAM;else if(base==PROJECT_CONTROL_RAM_ASSET_BASE)*out_kind=PERSIST_ASSET_SAMPLE_RAM;else if(base==PROJECT_CONTROL_WAVE_ASSET_BASE)*out_kind=PERSIST_ASSET_WAVETABLE;else if(base==PROJECT_CONTROL_MULTI_ASSET_BASE)*out_kind=PERSIST_ASSET_MULTI;else return 0U;return 1U;}

static void capture_runtime_banks(void)
{
    for(uint16_t global=0U;global<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++global){const sample_global_slot_t*s=sample_global_pool_get_slot(global);if(s==NULL||s->state==SAMPLE_GLOBAL_STATE_EMPTY)continue;if(s->kind==SAMPLE_GLOBAL_KIND_STREAM){const sample_desc_t*d=sample_pool_get(s->backend_index);if(d!=NULL&&d->path[0]!='\0'&&g_sample_bank[global].used==0U)(void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,global,PERSIST_ASSET_SAMPLE_STREAM,d->path,global);}else if(s->kind==SAMPLE_GLOBAL_KIND_RAM){const sampler_ram_slot_t*d=sampler_ram_pool_get_slot(s->backend_index);if(d!=NULL&&d->path[0]!='\0'&&g_sample_bank[global].used==0U)(void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,global,PERSIST_ASSET_SAMPLE_RAM,d->path,global);}else if(s->kind==SAMPLE_GLOBAL_KIND_WAVETABLE){const wavetable_slot_t*d=wavetable_pool_get_slot(s->backend_index);if(d!=NULL&&d->path[0]!='\0'&&g_wavetable_bank[global].used==0U)(void)bank_set(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,global,PERSIST_ASSET_WAVETABLE,d->path,global);}}
    for(uint16_t id=0U;id<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++id){const multi_sample_instrument_t*m=multi_sample_pool_get_instrument(id);if(m!=NULL&&m->index_path[0]!='\0'&&g_multi_bank[id].used==0U)(void)bank_set(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,id,PERSIST_ASSET_MULTI,m->index_path,id);}
}

void project_control_init(void){memset(&g_macros,0,sizeof(g_macros));memset(g_entity_assets,0,sizeof(g_entity_assets));memset(g_sample_bank,0,sizeof(g_sample_bank));memset(g_wavetable_bank,0,sizeof(g_wavetable_bank));memset(g_multi_bank,0,sizeof(g_multi_bank));g_macros.hall_switch_key=PERSIST_MACRO_HALL_SCENE;for(uint8_t i=0U;i<PERSIST_CONTROL_MACRO_COUNT;++i)g_macros.selected_scene[i]=i;capture_runtime_banks();}
project_control_hall_mode_t project_control_get_hall_mode(void){return(g_macros.hall_switch_key==PERSIST_MACRO_HALL_SWITCH)?PROJECT_CONTROL_HALL_SWITCH:PROJECT_CONTROL_HALL_SCENE;}
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode){if(mode>PROJECT_CONTROL_HALL_SWITCH)return 0U;g_macros.hall_switch_key=(mode==PROJECT_CONTROL_HALL_SWITCH)?PERSIST_MACRO_HALL_SWITCH:PERSIST_MACRO_HALL_SCENE;return 1U;}
uint8_t project_control_get_macro_scene(uint8_t macro){return(macro<PERSIST_CONTROL_MACRO_COUNT)?g_macros.selected_scene[macro]:0U;}
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene){if(macro>=PERSIST_CONTROL_MACRO_COUNT||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros.selected_scene[macro]=scene;return 1U;}
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;const persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(lock>=s->lock_count){out->track=0xFFU;out->param=PARAM_COUNT;out->scene_value=0.0f;return 1U;}if(persist_key_param_from_disk(s->locks[lock].parameter,&out->param)==0U)return 0U;out->track=s->locks[lock].entity;out->scene_value=s->locks[lock].value.f32;return 1U;}
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in){if(in==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(in->track==0xFFU||in->param>=PARAM_COUNT){if(lock<s->lock_count){for(uint8_t i=lock;i+1U<s->lock_count;++i)s->locks[i]=s->locks[i+1U];--s->lock_count;}param_macro_sync_scene_sources();return 1U;}persist_control_parameter_key_t key;if(in->track>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_to_disk(in->param,&key)==0U)return 0U;if(lock>s->lock_count)return 0U;if(lock==s->lock_count){if(s->lock_count>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;++s->lock_count;}s->locks[lock]=(persist_control_macro_lock_t){in->track,key,PERSIST_VALUE_FLOAT32,{.f32=in->scene_value}};param_macro_sync_scene_sources();return 1U;}
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock){return(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=g_macros.scenes[scene].lock_count)?1U:0U;}
uint8_t project_control_capture_macros(persist_control_macros_t*out){if(out==NULL)return 0U;*out=g_macros;return 1U;}
uint8_t project_control_apply_macros(const persist_control_macros_t*in){if(in==NULL)return 0U;for(uint8_t m=0U;m<PERSIST_CONTROL_MACRO_COUNT;++m)if(in->selected_scene[m]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros=*in;param_macro_sync_scene_sources();return 1U;}

uint8_t project_control_set_entity_asset(uint8_t entity,uint32_t kind,const char*path){if(entity>=PERSIST_CONTROL_ENTITY_COUNT||path==NULL||path[0]=='\0')return 0U;project_control_entity_asset_t next={.used=1U,.kind=kind};if(text_copy(next.path,path)==0U)return 0U;g_entity_assets[entity]=next;return 1U;}
uint8_t project_control_clear_entity_asset(uint8_t entity){if(entity>=PERSIST_CONTROL_ENTITY_COUNT)return 0U;memset(&g_entity_assets[entity],0,sizeof(g_entity_assets[entity]));return 1U;}
uint8_t project_control_get_entity_asset(uint8_t entity,persist_control_asset_ref_t*out){if(entity>=PERSIST_CONTROL_ENTITY_COUNT||out==NULL||g_entity_assets[entity].used==0U)return 0U;memset(out,0,sizeof(*out));out->id=entity_asset_id(entity);out->kind=g_entity_assets[entity].kind;out->path_length=(uint16_t)strlen(g_entity_assets[entity].path);memcpy(out->path,g_entity_assets[entity].path,out->path_length);return 1U;}

static uint8_t capture_bank(const project_control_bank_slot_t*bank,uint16_t capacity,persist_control_asset_ref_t*out,uint16_t out_capacity,uint16_t*count){for(uint16_t i=0U;i<capacity;++i){if(bank[i].used==0U)continue;if(*count>=out_capacity)return 0U;persist_control_asset_ref_t*a=&out[(*count)++];memset(a,0,sizeof(*a));a->id=bank_asset_id(bank[i].kind,i);a->kind=bank[i].kind;a->path_length=(uint16_t)strlen(bank[i].path);memcpy(a->path,bank[i].path,a->path_length);}return 1U;}
uint16_t project_control_capture_assets(persist_control_asset_ref_t*out,uint16_t capacity){uint16_t n=0U;if(out==NULL)return 0U;capture_runtime_banks();if(capture_bank(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,out,capacity,&n)==0U)return n;if(capture_bank(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,out,capacity,&n)==0U)return n;if(capture_bank(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,out,capacity,&n)==0U)return n;for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT&&n<capacity;++e)if(project_control_get_entity_asset(e,&out[n])!=0U)++n;return n;}

uint8_t project_control_register_sample_runtime(uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){if(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM)return 0U;return bank_register(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,runtime,out_logical);}
uint8_t project_control_register_wavetable_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){return bank_register(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,PERSIST_ASSET_WAVETABLE,path,runtime,out_logical);}
uint8_t project_control_register_multi_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){return bank_register(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,path,runtime,out_logical);}
void project_control_unregister_sample_runtime(uint16_t runtime){bank_unregister(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,runtime);}
void project_control_unregister_wavetable_runtime(uint16_t runtime){bank_unregister(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,runtime);}
void project_control_unregister_multi_runtime(uint16_t runtime){bank_unregister(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,runtime);}
uint8_t project_control_resolve_sample_runtime(uint16_t logical,uint16_t*out_runtime,uint32_t*out_kind){if(bank_resolve(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime)==0U)return 0U;const sample_global_slot_t*s=sample_global_pool_get_slot(*out_runtime);if(s==NULL||(s->kind!=SAMPLE_GLOBAL_KIND_STREAM&&s->kind!=SAMPLE_GLOBAL_KIND_RAM))return 0U;if(out_kind!=NULL)*out_kind=(s->kind==SAMPLE_GLOBAL_KIND_RAM)?PERSIST_ASSET_SAMPLE_RAM:PERSIST_ASSET_SAMPLE_STREAM;return 1U;}
uint8_t project_control_resolve_wavetable_runtime(uint16_t logical,uint16_t*out_runtime){return bank_resolve(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime);}
uint8_t project_control_resolve_multi_runtime(uint16_t logical,uint16_t*out_runtime){return bank_resolve(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,out_runtime);}

static void apply_bank_asset(uint32_t kind,uint16_t logical,const char*path)
{
    uint16_t runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    if(kind==PERSIST_ASSET_SAMPLE_STREAM){for(uint16_t slot=0U;slot<SAMPLE_POOL_SIZE;++slot)if(sample_pool_get_state(slot)==SAMPLE_POOL_SLOT_EMPTY&&sample_pool_load(slot,path)){if(sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_STREAM,slot,&runtime)!=0U)break;} (void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_SAMPLE_RAM){uint16_t backend=sampler_ram_pool_find_free_slot();if(backend<SAMPLER_RAM_POOL_MAX_SLOTS)(void)sampler_ram_pool_load_wav(backend,path,&runtime);(void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_WAVETABLE){uint16_t backend=wavetable_pool_find_free_slot();if(backend<WAVETABLE_POOL_MAX_SLOTS)(void)wavetable_pool_load_file(backend,path,&runtime);(void)bank_set(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_MULTI){for(uint16_t backend=0U;backend<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++backend)if(multi_sample_pool_get_state(backend)==MULTI_SAMPLE_INSTRUMENT_EMPTY){multi_sample_load_result_t r=multi_sample_load_instrument(path,backend);if(r==MULTI_SAMPLE_LOAD_OK||r==MULTI_SAMPLE_LOAD_ALREADY_READY)runtime=backend;break;}(void)bank_set(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,kind,path,runtime);}
}

uint8_t project_control_apply_assets(const persist_control_asset_ref_t*assets,uint16_t count,const persist_control_pattern_t*pattern)
{
    if((count!=0U&&assets==NULL)||pattern==NULL)return 0U;
    memset(g_entity_assets,0,sizeof(g_entity_assets));memset(g_sample_bank,0,sizeof(g_sample_bank));memset(g_wavetable_bank,0,sizeof(g_wavetable_bank));memset(g_multi_bank,0,sizeof(g_multi_bank));
    multi_sample_cancel_all_loads();sample_global_pool_reset();sample_pool_init();sampler_ram_pool_reset();wavetable_pool_reset();multi_sample_pool_reset();
    for(uint16_t i=0U;i<count;++i){char path[PROJECT_CONTROL_ASSET_PATH_BYTES];if(assets[i].path_length>=sizeof(path))return 0U;memcpy(path,assets[i].path,assets[i].path_length);path[assets[i].path_length]='\0';uint32_t kind;uint16_t logical;if(bank_asset_decode(assets[i].id,&kind,&logical)!=0U){if(((kind==PERSIST_ASSET_SAMPLE_STREAM||kind==PERSIST_ASSET_SAMPLE_RAM||kind==PERSIST_ASSET_WAVETABLE)&&logical>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)||(kind==PERSIST_ASSET_MULTI&&logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS))return 0U;apply_bank_asset(kind,logical,path);}}
    for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){uint32_t id=pattern->entities[e].asset;if(id==0U)continue;uint16_t i;for(i=0U;i<count&&assets[i].id!=id;++i){}if(i==count)return 0U;char path[PROJECT_CONTROL_ASSET_PATH_BYTES];uint16_t n=assets[i].path_length;if(n>=sizeof(path))return 0U;memcpy(path,assets[i].path,n);path[n]='\0';if(project_control_set_entity_asset(e,assets[i].kind,path)==0U)return 0U;}
    return 1U;
}
