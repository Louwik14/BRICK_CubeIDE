#include "Core/project_control.h"

#include "Core/track_state.h"

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
#define PROJECT_CONTROL_STREAM_ASSET_BASE 0x11000000UL
#define PROJECT_CONTROL_RAM_ASSET_BASE 0x12000000UL
#define PROJECT_CONTROL_WAVE_ASSET_BASE 0x13000000UL
#define PROJECT_CONTROL_MULTI_ASSET_BASE 0x14000000UL

typedef struct { uint8_t used; uint32_t kind; char path[PROJECT_CONTROL_ASSET_PATH_BYTES]; uint16_t runtime; uint16_t pending_runtime; } project_control_bank_slot_t;

CONTROL_STATE_SDRAM static persist_control_macros_t g_macros;
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_sample_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_wavetable_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_multi_bank[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];

static uint8_t text_copy(char*dst,const char*src){uint16_t i=0U;if(dst==NULL||src==NULL)return 0U;while(i+1U<PROJECT_CONTROL_ASSET_PATH_BYTES&&src[i]!='\0'){dst[i]=src[i];++i;}dst[i]='\0';return src[i]=='\0';}
static uint8_t canonical_path_copy(char*dst,const char*src){uint16_t i=0U;uint8_t slash=0U;if(dst==NULL||src==NULL)return 0U;while(*src!='\0'){char c=*src++;if(c=='\\')c='/';if(c=='/'){if(slash!=0U)continue;slash=1U;}else slash=0U;if(c>='A'&&c<='Z')c=(char)(c+('a'-'A'));if(i+1U>=PROJECT_CONTROL_ASSET_PATH_BYTES)return 0U;dst[i++]=c;}while(i>1U&&dst[i-1U]=='/')--i;dst[i]='\0';return(i!=0U)?1U:0U;}
static uint8_t bank_set(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint32_t kind,const char*path,uint16_t runtime){if(bank==NULL||logical>=capacity||path==NULL||path[0]=='\0')return 0U;project_control_bank_slot_t next={.used=1U,.kind=kind,.runtime=runtime,.pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME};if(text_copy(next.path,path)==0U)return 0U;bank[logical]=next;return 1U;}
static uint8_t bank_find(const project_control_bank_slot_t*bank,uint16_t capacity,uint32_t kind,const char*path,uint16_t*out_logical){char canonical[PROJECT_CONTROL_ASSET_PATH_BYTES];char candidate[PROJECT_CONTROL_ASSET_PATH_BYTES];if(bank==NULL||out_logical==NULL||canonical_path_copy(canonical,path)==0U)return 0U;for(uint16_t i=0U;i<capacity;++i)if(bank[i].used!=0U&&bank[i].kind==kind&&canonical_path_copy(candidate,bank[i].path)!=0U&&strcmp(candidate,canonical)==0){*out_logical=i;return 1U;}return 0U;}
static uint8_t bank_register(project_control_bank_slot_t*bank,uint16_t capacity,uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){uint16_t existing;if(bank==NULL||path==NULL||path[0]=='\0')return 0U;if(bank_find(bank,capacity,kind,path,&existing)!=0U){if(bank[existing].runtime==PROJECT_CONTROL_INVALID_RUNTIME)bank[existing].runtime=runtime;if(out_logical!=NULL)*out_logical=existing;return 1U;}for(uint16_t i=0U;i<capacity;++i)if(bank[i].used==0U){if(bank_set(bank,capacity,i,kind,path,runtime)==0U)return 0U;if(out_logical!=NULL)*out_logical=i;return 1U;}return 0U;}
static uint8_t bank_remove(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical){if(bank==NULL||logical>=capacity||bank[logical].used==0U)return 0U;memset(&bank[logical],0,sizeof(bank[logical]));return 1U;}
static uint8_t bank_has(const project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint32_t*out_kind){if(bank==NULL||logical>=capacity||bank[logical].used==0U)return 0U;if(out_kind!=NULL)*out_kind=bank[logical].kind;return 1U;}
static uint16_t bank_list(const project_control_bank_slot_t*bank,uint16_t bank_capacity,uint32_t kind,uint16_t*out,uint16_t capacity){uint16_t n=0U;if(out==NULL)return 0U;for(uint16_t i=0U;i<bank_capacity&&n<capacity;++i)if(bank[i].used!=0U&&(kind==0U||bank[i].kind==kind))out[n++]=i;return n;}
static uint8_t bank_resolve(const project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint16_t*out_runtime){if(bank==NULL||out_runtime==NULL||logical>=capacity||bank[logical].used==0U||bank[logical].runtime==PROJECT_CONTROL_INVALID_RUNTIME)return 0U;*out_runtime=bank[logical].runtime;return 1U;}
static uint32_t bank_asset_id(uint32_t kind,uint16_t logical){uint32_t base=(kind==PERSIST_ASSET_SAMPLE_STREAM)?PROJECT_CONTROL_STREAM_ASSET_BASE:(kind==PERSIST_ASSET_SAMPLE_RAM)?PROJECT_CONTROL_RAM_ASSET_BASE:(kind==PERSIST_ASSET_WAVETABLE)?PROJECT_CONTROL_WAVE_ASSET_BASE:PROJECT_CONTROL_MULTI_ASSET_BASE;return base+(uint32_t)logical;}
static uint8_t bank_asset_decode(uint32_t id,uint32_t*out_kind,uint16_t*out_logical){const uint32_t base=id&0xFF000000UL;if(out_kind==NULL||out_logical==NULL||(id&0x00FF0000UL)!=0U)return 0U;*out_logical=(uint16_t)(id&0xFFFFU);if(base==PROJECT_CONTROL_STREAM_ASSET_BASE)*out_kind=PERSIST_ASSET_SAMPLE_STREAM;else if(base==PROJECT_CONTROL_RAM_ASSET_BASE)*out_kind=PERSIST_ASSET_SAMPLE_RAM;else if(base==PROJECT_CONTROL_WAVE_ASSET_BASE)*out_kind=PERSIST_ASSET_WAVETABLE;else if(base==PROJECT_CONTROL_MULTI_ASSET_BASE)*out_kind=PERSIST_ASSET_MULTI;else return 0U;return 1U;}

void project_control_reset_macros(void){memset(&g_macros,0,sizeof(g_macros));g_macros.hall_switch_key=PERSIST_MACRO_HALL_SCENE;for(uint8_t i=0U;i<PERSIST_CONTROL_MACRO_COUNT;++i)g_macros.selected_scene[i]=i;}
void project_control_reset_asset_banks(void){memset(g_sample_bank,0,sizeof(g_sample_bank));memset(g_wavetable_bank,0,sizeof(g_wavetable_bank));memset(g_multi_bank,0,sizeof(g_multi_bank));}
void project_control_init(void){project_control_reset_macros();project_control_reset_asset_banks();}
project_control_hall_mode_t project_control_get_hall_mode(void){return(g_macros.hall_switch_key==PERSIST_MACRO_HALL_SWITCH)?PROJECT_CONTROL_HALL_SWITCH:PROJECT_CONTROL_HALL_SCENE;}
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode){if(mode>PROJECT_CONTROL_HALL_SWITCH)return 0U;g_macros.hall_switch_key=(mode==PROJECT_CONTROL_HALL_SWITCH)?PERSIST_MACRO_HALL_SWITCH:PERSIST_MACRO_HALL_SCENE;return 1U;}
uint8_t project_control_get_macro_scene(uint8_t macro){return(macro<PERSIST_CONTROL_MACRO_COUNT)?g_macros.selected_scene[macro]:0U;}
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene){if(macro>=PERSIST_CONTROL_MACRO_COUNT||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros.selected_scene[macro]=scene;return 1U;}
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;const persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(lock>=s->lock_count){out->track=0xFFU;out->param=PARAM_COUNT;out->scene_value=0.0f;return 1U;}if(persist_key_param_from_disk(s->locks[lock].parameter,&out->param)==0U)return 0U;out->track=s->locks[lock].entity;out->scene_value=s->locks[lock].value.f32;return 1U;}
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in){if(in==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(in->track==0xFFU||in->param>=PARAM_COUNT){if(lock<s->lock_count){for(uint8_t i=lock;i+1U<s->lock_count;++i)s->locks[i]=s->locks[i+1U];--s->lock_count;}param_macro_sync_scene_sources();return 1U;}persist_control_parameter_key_t key;if(in->track>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_to_disk(in->param,&key)==0U)return 0U;if(lock>s->lock_count)return 0U;if(lock==s->lock_count){if(s->lock_count>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;++s->lock_count;}s->locks[lock]=(persist_control_macro_lock_t){in->track,key,PERSIST_VALUE_FLOAT32,{.f32=in->scene_value}};param_macro_sync_scene_sources();return 1U;}
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock){return(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=g_macros.scenes[scene].lock_count)?1U:0U;}
uint8_t project_control_scene_has_locks(uint8_t scene){return(scene<PERSIST_CONTROL_MACRO_SCENE_COUNT&&g_macros.scenes[scene].lock_count!=0U)?1U:0U;}
uint8_t project_control_get_scene_lock_for_param(uint8_t scene,uint8_t track,param_id_t param,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param){*out=current;return 1U;}}return 0U;}
uint8_t project_control_assign_scene_lock(uint8_t scene,uint8_t track,param_id_t param,float value){if(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;project_control_macro_lock_t next={track,param,value};for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param)return project_control_set_scene_lock(scene,lock,&next);}for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock)if(project_control_scene_lock_is_empty(scene,lock)!=0U)return project_control_set_scene_lock(scene,lock,&next);return 0U;}
uint8_t project_control_clear_scene_lock(uint8_t scene,uint8_t track,param_id_t param){if(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param){const project_control_macro_lock_t empty={0xFFU,PARAM_COUNT,0.0f};return project_control_set_scene_lock(scene,lock,&empty);}}return 0U;}
uint8_t project_control_capture_macros(persist_control_macros_t*out){if(out==NULL)return 0U;*out=g_macros;return 1U;}
const persist_control_macros_t*project_control_macros_view(void){return &g_macros;}
uint8_t project_control_apply_macros(const persist_control_macros_t*in){if(in==NULL)return 0U;for(uint8_t m=0U;m<PERSIST_CONTROL_MACRO_COUNT;++m)if(in->selected_scene[m]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros=*in;param_macro_sync_scene_sources();return 1U;}

uint16_t project_control_asset_count(void){uint16_t n=0U;for(uint16_t i=0U;i<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++i){n+=(g_sample_bank[i].used!=0U);n+=(g_wavetable_bank[i].used!=0U);}for(uint16_t i=0U;i<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++i)n+=(g_multi_bank[i].used!=0U);return n;}
uint8_t project_control_get_asset_ordinal(uint16_t ordinal,persist_control_asset_ref_t*out){if(out==NULL)return 0U;const project_control_bank_slot_t*banks[3]={g_sample_bank,g_wavetable_bank,g_multi_bank};const uint16_t caps[3]={SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS};for(uint8_t b=0U;b<3U;++b)for(uint16_t i=0U;i<caps[b];++i)if(banks[b][i].used!=0U){if(ordinal--==0U){memset(out,0,sizeof(*out));out->id=bank_asset_id(banks[b][i].kind,i);out->kind=banks[b][i].kind;out->path_length=(uint16_t)strlen(banks[b][i].path);memcpy(out->path,banks[b][i].path,out->path_length);return 1U;}}return 0U;}

uint8_t project_control_register_sample_runtime(uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){if((kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM)||(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))return 0U;return bank_register(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,runtime,out_logical);}
uint8_t project_control_register_wavetable_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)return 0U;return bank_register(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,PERSIST_ASSET_WAVETABLE,path,runtime,out_logical);}
uint8_t project_control_register_multi_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)return 0U;uint16_t published=PROJECT_CONTROL_INVALID_RUNTIME;if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME){const multi_sample_instrument_t*i=multi_sample_pool_get_instrument(runtime);if(i!=NULL&&multi_sample_pool_get_state(runtime)==MULTI_SAMPLE_INSTRUMENT_READY&&strcmp(i->index_path,path)==0)published=runtime;}return bank_register(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,path,published,out_logical);}
uint8_t project_control_find_asset(uint32_t kind,const char*path,uint16_t*out_logical){if(kind==PERSIST_ASSET_SAMPLE_STREAM||kind==PERSIST_ASSET_SAMPLE_RAM)return bank_find(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,out_logical);if(kind==PERSIST_ASSET_WAVETABLE)return bank_find(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,out_logical);if(kind==PERSIST_ASSET_MULTI)return bank_find(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,kind,path,out_logical);return 0U;}
uint8_t project_control_begin_multi_runtime(uint16_t logical,const char*path,uint16_t runtime){if(path==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||g_multi_bank[logical].used==0U||g_multi_bank[logical].kind!=PERSIST_ASSET_MULTI||strcmp(g_multi_bank[logical].path,path)!=0)return 0U;g_multi_bank[logical].runtime=PROJECT_CONTROL_INVALID_RUNTIME;g_multi_bank[logical].pending_runtime=runtime;return 1U;}
static void reapply_multi_logical(uint16_t logical)
{
    for(uint8_t track=0U;track<UI_TRACK_COUNT;++track)
    {
        float selected=0.0f;
        if(track_state_get_family(track)==UI_TRACK_FAMILY_SAMPLER
            &&track_state_get_type(track)==UI_TRACK_TYPE_MULTI
            &&param_registry_get_track_value(PARAM_SAMPLER_SAMPLE,track,&selected)!=0U
            &&(uint16_t)(selected+0.5f)==logical)
        {
            (void)param_registry_apply_track_value(PARAM_SAMPLER_SAMPLE,track,(float)logical);
        }
    }
}
void project_control_complete_multi_runtime(uint16_t logical,const char*path,uint16_t runtime,uint8_t success)
{
    if(path==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||g_multi_bank[logical].used==0U||g_multi_bank[logical].kind!=PERSIST_ASSET_MULTI||g_multi_bank[logical].pending_runtime!=runtime||strcmp(g_multi_bank[logical].path,path)!=0)return;
    g_multi_bank[logical].runtime=(success!=0U)?runtime:PROJECT_CONTROL_INVALID_RUNTIME;
    g_multi_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    reapply_multi_logical(logical);
}
uint8_t project_control_remove_sample(uint16_t logical){return bank_remove(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);}
uint8_t project_control_remove_wavetable(uint16_t logical){return bank_remove(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);}
uint8_t project_control_remove_multi(uint16_t logical){if(logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||g_multi_bank[logical].used==0U)return 0U;g_multi_bank[logical].runtime=PROJECT_CONTROL_INVALID_RUNTIME;g_multi_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;reapply_multi_logical(logical);return bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical);}
uint8_t project_control_has_sample(uint16_t logical,uint32_t*out_kind){return bank_has(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_kind);}
uint16_t project_control_sample_projection_count(uint32_t kind)
{
    uint16_t count=0U;
    if(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM)return 0U;
    for(uint16_t logical=0U;logical<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++logical)
        if(g_sample_bank[logical].used!=0U&&g_sample_bank[logical].kind==kind)++count;
    return count;
}
uint8_t project_control_sample_logical_at_ordinal(uint32_t kind,uint16_t ordinal,uint16_t*out_logical)
{
    if(out_logical==NULL||(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM))return 0U;
    for(uint16_t logical=0U;logical<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++logical)
        if(g_sample_bank[logical].used!=0U&&g_sample_bank[logical].kind==kind)
        {
            if(ordinal--==0U){*out_logical=logical;return 1U;}
        }
    return 0U;
}
uint8_t project_control_sample_ordinal_for_logical(uint32_t kind,uint16_t logical,uint16_t*out_ordinal)
{
    uint16_t ordinal=0U;
    if(out_ordinal==NULL||logical>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
        ||(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM))return 0U;
    for(uint16_t candidate=0U;candidate<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++candidate)
        if(g_sample_bank[candidate].used!=0U&&g_sample_bank[candidate].kind==kind)
        {
            if(candidate==logical){*out_ordinal=ordinal;return 1U;}
            ++ordinal;
        }
    return 0U;
}
uint8_t project_control_has_wavetable(uint16_t logical){return bank_has(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,NULL);}
uint8_t project_control_has_multi(uint16_t logical){return bank_has(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,NULL);}
uint16_t project_control_list_samples(uint32_t kind,uint16_t*out,uint16_t capacity){return bank_list(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,out,capacity);}
uint16_t project_control_list_wavetables(uint16_t*out,uint16_t capacity){return bank_list(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,0U,out,capacity);}
uint16_t project_control_list_multis(uint16_t*out,uint16_t capacity){return bank_list(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,0U,out,capacity);}
uint8_t project_control_get_logical_asset(uint32_t kind,uint16_t logical,persist_control_asset_ref_t*out){const project_control_bank_slot_t*bank=NULL;uint16_t capacity=0U;if(out==NULL)return 0U;if(kind==PERSIST_ASSET_SAMPLE_STREAM||kind==PERSIST_ASSET_SAMPLE_RAM){bank=g_sample_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}else if(kind==PERSIST_ASSET_WAVETABLE){bank=g_wavetable_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}else if(kind==PERSIST_ASSET_MULTI){bank=g_multi_bank;capacity=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;}if(bank==NULL||logical>=capacity||bank[logical].used==0U||bank[logical].kind!=kind)return 0U;memset(out,0,sizeof(*out));out->id=bank_asset_id(kind,logical);out->kind=kind;out->path_length=(uint16_t)strlen(bank[logical].path);memcpy(out->path,bank[logical].path,out->path_length);return 1U;}
uint8_t project_control_resolve_sample_runtime(uint16_t logical,uint16_t*out_runtime,uint32_t*out_kind){if(bank_resolve(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime)==0U)return 0U;const sample_global_slot_t*s=sample_global_pool_get_slot(*out_runtime);if(s==NULL||(s->kind!=SAMPLE_GLOBAL_KIND_STREAM&&s->kind!=SAMPLE_GLOBAL_KIND_RAM))return 0U;if(out_kind!=NULL)*out_kind=(s->kind==SAMPLE_GLOBAL_KIND_RAM)?PERSIST_ASSET_SAMPLE_RAM:PERSIST_ASSET_SAMPLE_STREAM;return 1U;}
uint8_t project_control_resolve_wavetable_runtime(uint16_t logical,uint16_t*out_runtime){return bank_resolve(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime);}
uint8_t project_control_resolve_multi_runtime(uint16_t logical,uint16_t*out_runtime){uint16_t runtime;if(out_runtime==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||bank_resolve(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,&runtime)==0U)return 0U;const multi_sample_instrument_t*i=multi_sample_pool_get_instrument(runtime);if(i==NULL||multi_sample_pool_get_state(runtime)!=MULTI_SAMPLE_INSTRUMENT_READY||strcmp(i->index_path,g_multi_bank[logical].path)!=0)return 0U;*out_runtime=runtime;return 1U;}

static void apply_bank_asset(uint32_t kind,uint16_t logical,const char*path)
{
    uint16_t runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    if(kind==PERSIST_ASSET_SAMPLE_STREAM){for(uint16_t slot=0U;slot<SAMPLE_POOL_SIZE;++slot)if(sample_pool_get_state(slot)==SAMPLE_POOL_SLOT_EMPTY&&sample_pool_load(slot,path)){if(sample_global_pool_find_by_backend(SAMPLE_GLOBAL_KIND_STREAM,slot,&runtime)!=0U)break;} (void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_SAMPLE_RAM){uint16_t backend=sampler_ram_pool_find_free_slot();if(backend<SAMPLER_RAM_POOL_MAX_SLOTS)(void)sampler_ram_pool_load_wav(backend,path,&runtime);(void)bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_WAVETABLE){uint16_t backend=wavetable_pool_find_free_slot();if(backend<WAVETABLE_POOL_MAX_SLOTS)(void)wavetable_pool_load_file(backend,path,&runtime);(void)bank_set(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,kind,path,runtime);}
    else if(kind==PERSIST_ASSET_MULTI){(void)bank_set(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,kind,path,runtime);for(uint16_t backend=0U;backend<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++backend)if(multi_sample_pool_get_state(backend)==MULTI_SAMPLE_INSTRUMENT_EMPTY){(void)multi_sample_load_instrument(logical,path,backend);break;}}
}

uint8_t project_control_begin_asset_restore(void)
{
    project_control_reset_asset_banks();
    multi_sample_cancel_all_loads();sample_global_pool_reset();sample_pool_init();sampler_ram_pool_reset();wavetable_pool_reset();multi_sample_pool_reset();
    return 1U;
}

uint8_t project_control_validate_asset(const persist_control_asset_ref_t*asset)
{uint32_t kind;uint16_t logical;if(asset==NULL||asset->path_length==0U||asset->path_length>PERSIST_CONTROL_ASSET_PATH_BYTES||!bank_asset_decode(asset->id,&kind,&logical)||kind!=asset->kind)return 0U;if(kind==PERSIST_ASSET_SAMPLE_STREAM||kind==PERSIST_ASSET_SAMPLE_RAM||kind==PERSIST_ASSET_WAVETABLE)return(logical<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)?1U:0U;if(kind==PERSIST_ASSET_MULTI)return(logical<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)?1U:0U;return 0U;}

uint8_t project_control_put_asset(const persist_control_asset_ref_t*asset)
{
    uint32_t kind;uint16_t logical;if(!project_control_validate_asset(asset)||!bank_asset_decode(asset->id,&kind,&logical))return 0U;char path[PROJECT_CONTROL_ASSET_PATH_BYTES];memcpy(path,asset->path,asset->path_length);path[asset->path_length]='\0';uint16_t existing;if(project_control_find_asset(kind,path,&existing)!=0U)return 1U;apply_bank_asset(kind,logical,path);return 1U;
}

uint8_t project_control_ensure_asset(uint32_t kind,const char*path,uint16_t*out_logical)
{
    if(path==NULL||path[0]=='\0'||out_logical==NULL)return 0U;
    project_control_bank_slot_t*bank=NULL;uint16_t capacity=0U;
    if(kind==PERSIST_ASSET_SAMPLE_STREAM||kind==PERSIST_ASSET_SAMPLE_RAM){bank=g_sample_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}
    else if(kind==PERSIST_ASSET_WAVETABLE){bank=g_wavetable_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}
    else if(kind==PERSIST_ASSET_MULTI){bank=g_multi_bank;capacity=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;}
    else return 0U;
    if(bank_find(bank,capacity,kind,path,out_logical)!=0U)return 1U;
    for(uint16_t i=0U;i<capacity;++i)if(!bank[i].used){apply_bank_asset(kind,i,path);if(bank[i].used){*out_logical=i;return 1U;}return 0U;}
    return 0U;
}
