#include "Storage/project_control.h"

#include "Track/track_state.h"
#include "Track/track_runtime.h"
#include "Sampler/audio_wave_table_projection_control.h"
#include "IPC/control_audio_command.h"
#include "IPC/control_audio_publication.h"
#include "IPC/live_clock_control.h"

#include "Param/param_macro.h"
#include "Param/param_registry.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "Platform/memory_layout.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/asset_ref.h"
#include "ff.h"

#include <string.h>
#include <math.h>

#define PROJECT_CONTROL_ASSET_PATH_BYTES (PERSIST_CONTROL_ASSET_PATH_BYTES + 1U)
#define PROJECT_CONTROL_INVALID_RUNTIME 0xFFFFU

typedef struct { uint8_t used; uint32_t kind; char canonical_path[PROJECT_CONTROL_ASSET_PATH_BYTES]; uint16_t runtime; uint16_t pending_runtime; } project_control_bank_slot_t;

CONTROL_STATE_SDRAM static persist_control_macros_t g_macros;
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_sample_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_wavetable_bank[SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS];
CONTROL_STATE_SDRAM static project_control_bank_slot_t g_multi_bank[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
CONTROL_STATE_SDRAM static persist_control_asset_ref_t
    g_track_assets[BRICK_ENTITY_CAPACITY][PROJECT_CONTROL_ASSET_ROLE_COUNT];

static uint8_t bank_set(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint32_t kind,const char*path,uint16_t runtime){persist_control_asset_ref_t ref;if(bank==NULL||logical>=capacity||asset_ref_make_canonical(kind,path,&ref)==0U)return 0U;project_control_bank_slot_t next={.used=1U,.kind=kind,.runtime=runtime,.pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME};memcpy(next.canonical_path,ref.canonical_path,ref.path_length);next.canonical_path[ref.path_length]='\0';bank[logical]=next;return 1U;}
static uint8_t bank_find(const project_control_bank_slot_t*bank,uint16_t capacity,uint32_t kind,const char*path,uint16_t*out_logical){persist_control_asset_ref_t ref;if(bank==NULL||out_logical==NULL||asset_ref_make_canonical(kind,path,&ref)==0U)return 0U;for(uint16_t i=0U;i<capacity;++i)if(bank[i].used!=0U&&bank[i].kind==kind&&strcmp(bank[i].canonical_path,ref.canonical_path)==0){*out_logical=i;return 1U;}return 0U;}
static uint8_t bank_register(project_control_bank_slot_t*bank,uint16_t capacity,uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){uint16_t existing;if(bank==NULL||path==NULL||path[0]=='\0')return 0U;if(bank_find(bank,capacity,kind,path,&existing)!=0U){if(bank[existing].runtime==PROJECT_CONTROL_INVALID_RUNTIME)bank[existing].runtime=runtime;if(out_logical!=NULL)*out_logical=existing;return 1U;}for(uint16_t i=0U;i<capacity;++i)if(bank[i].used==0U){if(bank_set(bank,capacity,i,kind,path,runtime)==0U)return 0U;if(out_logical!=NULL)*out_logical=i;return 1U;}return 0U;}
static uint8_t bank_remove(project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical){if(bank==NULL||logical>=capacity||bank[logical].used==0U)return 0U;memset(&bank[logical],0,sizeof(bank[logical]));return 1U;}
static uint8_t bank_has(const project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint32_t*out_kind){if(bank==NULL||logical>=capacity||bank[logical].used==0U)return 0U;if(out_kind!=NULL)*out_kind=bank[logical].kind;return 1U;}
static uint16_t bank_list(const project_control_bank_slot_t*bank,uint16_t bank_capacity,uint32_t kind,uint16_t*out,uint16_t capacity){uint16_t n=0U;if(out==NULL)return 0U;for(uint16_t i=0U;i<bank_capacity&&n<capacity;++i)if(bank[i].used!=0U&&(kind==0U||bank[i].kind==kind))out[n++]=i;return n;}
static uint8_t bank_resolve(const project_control_bank_slot_t*bank,uint16_t capacity,uint16_t logical,uint16_t*out_runtime){if(bank==NULL||out_runtime==NULL||logical>=capacity||bank[logical].used==0U||bank[logical].runtime==PROJECT_CONTROL_INVALID_RUNTIME)return 0U;*out_runtime=bank[logical].runtime;return 1U;}
static uint8_t classic_find(const char*path,uint16_t*out_logical){persist_control_asset_ref_t wanted;if(out_logical==NULL||asset_ref_make_canonical(PERSIST_ASSET_SAMPLE_STREAM,path,&wanted)==0U)return 0U;for(uint16_t i=0U;i<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++i){const sample_global_slot_t*s=sample_global_pool_get_slot(i);if(s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC&&strlen(s->path)==wanted.path_length&&memcmp(s->path,wanted.canonical_path,wanted.path_length)==0){*out_logical=i;return 1U;}}return 0U;}
static uint8_t classic_asset(uint16_t logical,persist_control_asset_ref_t*out){const sample_global_slot_t*s=sample_global_pool_get_slot(logical);return(out!=NULL&&s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC)?asset_ref_make_canonical(PERSIST_ASSET_SAMPLE_STREAM,s->path,out):0U;}

static project_control_asset_result_t classic_failure_result(uint16_t logical)
{
    const FRESULT result = (FRESULT)sample_cache_get_last_fresult(logical);
    return (result == FR_INT_ERR || result == FR_INVALID_OBJECT
                || result == FR_INVALID_PARAMETER)
        ? PROJECT_CONTROL_ASSET_FAILED_INTERNAL
        : PROJECT_CONTROL_ASSET_FAILED;
}

static uint8_t project_control_ram_runtime_valid(uint16_t logical)
{
    uint16_t backend = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    if (logical >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
        || g_sample_bank[logical].runtime == PROJECT_CONTROL_INVALID_RUNTIME
        || sample_global_pool_resolve_backend(
            g_sample_bank[logical].runtime, SAMPLE_GLOBAL_KIND_RAM, &backend) == 0U)
        return 0U;
    const sample_global_slot_t *const global =
        sample_global_pool_get_slot(g_sample_bank[logical].runtime);
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(backend);
    return (uint8_t)(global != NULL
        && global->state == SAMPLE_GLOBAL_STATE_READY
        && slot != NULL && slot->state == SAMPLER_RAM_SLOT_READY
        && slot->global_slot == g_sample_bank[logical].runtime);
}

static uint8_t project_control_wavetable_runtime_valid(uint16_t logical)
{
    uint16_t backend = WAVETABLE_POOL_INVALID_SLOT;
    if (logical >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
        || g_wavetable_bank[logical].runtime == PROJECT_CONTROL_INVALID_RUNTIME
        || sample_global_pool_resolve_backend(
            g_wavetable_bank[logical].runtime, SAMPLE_GLOBAL_KIND_WAVETABLE,
            &backend) == 0U)
        return 0U;
    const sample_global_slot_t *const global =
        sample_global_pool_get_slot(g_wavetable_bank[logical].runtime);
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(backend);
    return (uint8_t)(global != NULL
        && global->state == SAMPLE_GLOBAL_STATE_READY
        && slot != NULL && slot->state == WAVETABLE_SLOT_READY
        && slot->global_slot == g_wavetable_bank[logical].runtime);
}

static void project_control_fill_default_macros(persist_control_macros_t *out)
{
    if (out == NULL) return;
    memset(out,0,sizeof(*out));
    out->hall_switch_key=PERSIST_MACRO_HALL_SCENE;
    for(uint8_t i=0U;i<PERSIST_CONTROL_MACRO_COUNT;++i)out->selected_scene[i]=i;
}
void project_control_reset_macros(void){project_control_fill_default_macros(&g_macros);}
uint8_t project_control_get_default_macros(persist_control_macros_t *out){if(out==NULL)return 0U;project_control_fill_default_macros(out);return 1U;}
void project_control_reset_asset_banks(void){memset(g_sample_bank,0,sizeof(g_sample_bank));memset(g_wavetable_bank,0,sizeof(g_wavetable_bank));memset(g_multi_bank,0,sizeof(g_multi_bank));memset(g_track_assets,0,sizeof(g_track_assets));}
void project_control_init(void){project_control_reset_macros();project_control_reset_asset_banks();audio_wave_table_projection_init();}
project_control_hall_mode_t project_control_get_hall_mode(void){return(g_macros.hall_switch_key==PERSIST_MACRO_HALL_SWITCH)?PROJECT_CONTROL_HALL_SWITCH:PROJECT_CONTROL_HALL_SCENE;}
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode){if(mode>PROJECT_CONTROL_HALL_SWITCH)return 0U;g_macros.hall_switch_key=(mode==PROJECT_CONTROL_HALL_SWITCH)?PERSIST_MACRO_HALL_SWITCH:PERSIST_MACRO_HALL_SCENE;return 1U;}
uint8_t project_control_get_macro_scene(uint8_t macro){return(macro<PERSIST_CONTROL_MACRO_COUNT)?g_macros.selected_scene[macro]:0U;}
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene){if(macro>=PERSIST_CONTROL_MACRO_COUNT||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros.selected_scene[macro]=scene;return 1U;}
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;const persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(lock>=s->lock_count){out->track=0xFFU;out->param=PARAM_COUNT;out->scene_value=0.0f;return 1U;}if(persist_key_param_from_disk(s->locks[lock].parameter,&out->param)==0U)return 0U;out->track=s->locks[lock].entity;out->scene_value=s->locks[lock].scene_value;return 1U;}
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in){if(in==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(in->track==0xFFU||in->param>=PARAM_COUNT){if(lock<s->lock_count){for(uint8_t i=lock;i+1U<s->lock_count;++i)s->locks[i]=s->locks[i+1U];--s->lock_count;}return param_macro_sync_scene_sources();}persist_control_parameter_key_t key;if(in->track>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_to_disk(in->param,&key)==0U||param_macro_lock_target_is_supported(in->track,in->param)==0U||!isfinite(in->scene_value)||in->scene_value<param_registry[in->param].min||in->scene_value>param_registry[in->param].max)return 0U;if(lock>s->lock_count)return 0U;if(lock==s->lock_count){if(s->lock_count>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;++s->lock_count;}s->locks[lock]=(persist_control_macro_lock_t){in->track,key,in->scene_value};return param_macro_sync_scene_sources();}
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock){return(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=g_macros.scenes[scene].lock_count)?1U:0U;}
uint8_t project_control_scene_has_locks(uint8_t scene){return(scene<PERSIST_CONTROL_MACRO_SCENE_COUNT&&g_macros.scenes[scene].lock_count!=0U)?1U:0U;}
uint8_t project_control_get_scene_lock_for_param(uint8_t scene,uint8_t track,param_id_t param,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param){*out=current;return 1U;}}return 0U;}
uint8_t project_control_assign_scene_lock(uint8_t scene,uint8_t track,param_id_t param,float value){if(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;project_control_macro_lock_t next={track,param,value};for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param)return project_control_set_scene_lock(scene,lock,&next);}for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock)if(project_control_scene_lock_is_empty(scene,lock)!=0U)return project_control_set_scene_lock(scene,lock,&next);return 0U;}
uint8_t project_control_clear_scene_lock(uint8_t scene,uint8_t track,param_id_t param){if(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||track>=PERSIST_CONTROL_ENTITY_COUNT||param>=PARAM_COUNT)return 0U;for(uint8_t lock=0U;lock<PERSIST_CONTROL_MACRO_LOCK_COUNT;++lock){project_control_macro_lock_t current;if(project_control_get_scene_lock(scene,lock,&current)!=0U&&current.track==track&&current.param==param){const project_control_macro_lock_t empty={0xFFU,PARAM_COUNT,0.0f};return project_control_set_scene_lock(scene,lock,&empty);}}return 0U;}
uint8_t project_control_capture_macros(persist_control_macros_t*out){if(out==NULL)return 0U;*out=g_macros;return 1U;}
const persist_control_macros_t*project_control_macros_view(void){return &g_macros;}
uint8_t project_control_apply_macros(const persist_control_macros_t*in){if(in==NULL)return 0U;for(uint8_t m=0U;m<PERSIST_CONTROL_MACRO_COUNT;++m)if(in->selected_scene[m]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;for(uint8_t scene=0U;scene<PERSIST_CONTROL_MACRO_SCENE_COUNT;++scene){if(in->scenes[scene].lock_count>PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;for(uint8_t lock=0U;lock<in->scenes[scene].lock_count;++lock){const persist_control_macro_lock_t*l=&in->scenes[scene].locks[lock];param_id_t id;if(l->entity>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_from_disk(l->parameter,&id)==0U||param_macro_lock_target_is_supported(l->entity,id)==0U||!isfinite(l->scene_value)||l->scene_value<param_registry[id].min||l->scene_value>param_registry[id].max)return 0U;}}g_macros=*in;return param_macro_sync_scene_sources();}
uint16_t project_control_asset_count(void){uint16_t n=0U;for(uint16_t i=0U;i<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++i){const sample_global_slot_t*s=sample_global_pool_get_slot(i);n+=((s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC)?1U:0U);n+=(g_sample_bank[i].used!=0U);n+=(g_wavetable_bank[i].used!=0U);}for(uint16_t i=0U;i<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++i)n+=(g_multi_bank[i].used!=0U);return n;}
uint8_t project_control_get_asset_ordinal(uint16_t ordinal,persist_control_asset_ref_t*out){if(out==NULL)return 0U;for(uint16_t i=0U;i<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++i){const sample_global_slot_t*s=sample_global_pool_get_slot(i);if(s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC){if(ordinal--==0U)return classic_asset(i,out);}}const project_control_bank_slot_t*banks[3]={g_sample_bank,g_wavetable_bank,g_multi_bank};const uint16_t caps[3]={SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS};for(uint8_t b=0U;b<3U;++b)for(uint16_t i=0U;i<caps[b];++i)if(banks[b][i].used!=0U){if(ordinal--==0U)return asset_ref_make_canonical(banks[b][i].kind,banks[b][i].canonical_path,out);}return 0U;}

uint8_t project_control_register_sample_runtime(uint32_t kind,const char*path,uint16_t runtime,uint16_t*out_logical){if((kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM)||(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS))return 0U;if(kind==PERSIST_ASSET_SAMPLE_STREAM){uint16_t found;if(classic_find(path,&found)==0U||found!=runtime)return 0U;if(out_logical!=NULL)*out_logical=runtime;return 1U;}return bank_register(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,runtime,out_logical);}
uint8_t project_control_register_wavetable_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)return 0U;return bank_register(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,PERSIST_ASSET_WAVETABLE,path,runtime,out_logical);}
uint8_t project_control_register_multi_runtime(const char*path,uint16_t runtime,uint16_t*out_logical){if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME&&runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)return 0U;uint16_t published=PROJECT_CONTROL_INVALID_RUNTIME;if(runtime!=PROJECT_CONTROL_INVALID_RUNTIME){const multi_sample_instrument_t*i=multi_sample_pool_get_instrument(runtime);if(i!=NULL&&multi_sample_pool_get_state(runtime)==MULTI_SAMPLE_INSTRUMENT_READY&&strcmp(i->index_path,path)==0)published=runtime;}return bank_register(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,path,published,out_logical);}
uint8_t project_control_find_asset(uint32_t kind,const char*path,uint16_t*out_logical){if(kind==PERSIST_ASSET_SAMPLE_STREAM)return classic_find(path,out_logical);if(kind==PERSIST_ASSET_SAMPLE_RAM)return bank_find(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,out_logical);if(kind==PERSIST_ASSET_WAVETABLE)return bank_find(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,path,out_logical);if(kind==PERSIST_ASSET_MULTI)return bank_find(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,kind,path,out_logical);return 0U;}
uint8_t project_control_begin_multi_runtime(uint16_t logical,const char*path,uint16_t runtime){uint16_t found;if(path==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||bank_find(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,path,&found)==0U||found!=logical)return 0U;g_multi_bank[logical].runtime=PROJECT_CONTROL_INVALID_RUNTIME;g_multi_bank[logical].pending_runtime=runtime;return 1U;}
project_control_asset_result_t project_control_complete_multi_runtime(uint16_t logical,const char*path,uint16_t runtime,uint8_t success)
{
    uint16_t found;
    if(path==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||runtime>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||g_multi_bank[logical].pending_runtime!=runtime||bank_find(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,path,&found)==0U||found!=logical)return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    if (success == 0U)
    {
        (void)bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED;
    }
    const multi_sample_instrument_t *const instrument =
        multi_sample_pool_get_instrument(runtime);
    if (instrument == NULL
        || multi_sample_pool_get_state(runtime) != MULTI_SAMPLE_INSTRUMENT_READY
        || strcmp(instrument->index_path, path) != 0)
    {
        (void)bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    g_multi_bank[logical].runtime=runtime;
    g_multi_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    return PROJECT_CONTROL_ASSET_READY;
}
uint8_t project_control_remove_sample(uint16_t logical){const sample_global_slot_t*s=sample_global_pool_get_slot(logical);if(s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC){sample_global_pool_clear_classic(logical);return 1U;}return bank_remove(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);}
uint8_t project_control_remove_wavetable(uint16_t logical){return bank_remove(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);}
uint8_t project_control_remove_multi(uint16_t logical){if(logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||g_multi_bank[logical].used==0U)return 0U;g_multi_bank[logical].runtime=PROJECT_CONTROL_INVALID_RUNTIME;g_multi_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;return bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical);}
uint8_t project_control_has_sample(uint16_t logical,uint32_t*out_kind){const sample_global_slot_t*s=sample_global_pool_get_slot(logical);if(s!=NULL&&s->kind==SAMPLE_GLOBAL_KIND_CLASSIC){if(out_kind!=NULL)*out_kind=PERSIST_ASSET_SAMPLE_STREAM;return 1U;}return bank_has(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_kind);}
uint16_t project_control_sample_projection_count(uint32_t kind)
{
    uint16_t count=0U;
    if(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM)return 0U;
    for(uint16_t logical=0U;logical<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++logical)
        if((kind==PERSIST_ASSET_SAMPLE_STREAM)
            ? (sample_global_pool_get_slot(logical)->kind==SAMPLE_GLOBAL_KIND_CLASSIC)
            : (g_sample_bank[logical].used!=0U&&g_sample_bank[logical].kind==kind))++count;
    return count;
}
uint8_t project_control_sample_logical_at_ordinal(uint32_t kind,uint16_t ordinal,uint16_t*out_logical)
{
    if(out_logical==NULL||(kind!=PERSIST_ASSET_SAMPLE_STREAM&&kind!=PERSIST_ASSET_SAMPLE_RAM))return 0U;
    for(uint16_t logical=0U;logical<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;++logical)
        if((kind==PERSIST_ASSET_SAMPLE_STREAM)
            ? (sample_global_pool_get_slot(logical)->kind==SAMPLE_GLOBAL_KIND_CLASSIC)
            : (g_sample_bank[logical].used!=0U&&g_sample_bank[logical].kind==kind))
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
        if((kind==PERSIST_ASSET_SAMPLE_STREAM)
            ? (sample_global_pool_get_slot(candidate)->kind==SAMPLE_GLOBAL_KIND_CLASSIC)
            : (g_sample_bank[candidate].used!=0U&&g_sample_bank[candidate].kind==kind))
        {
            if(candidate==logical){*out_ordinal=ordinal;return 1U;}
            ++ordinal;
        }
    return 0U;
}
uint8_t project_control_has_wavetable(uint16_t logical){return bank_has(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,NULL);}
uint8_t project_control_has_multi(uint16_t logical){return bank_has(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,NULL);}
uint16_t project_control_list_samples(uint32_t kind,uint16_t*out,uint16_t capacity){if(kind!=PERSIST_ASSET_SAMPLE_STREAM)return bank_list(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,kind,out,capacity);uint16_t n=0U;if(out==NULL)return 0U;for(uint16_t i=0U;i<SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS&&n<capacity;++i)if(sample_global_pool_get_slot(i)->kind==SAMPLE_GLOBAL_KIND_CLASSIC)out[n++]=i;return n;}
uint16_t project_control_list_wavetables(uint16_t*out,uint16_t capacity){return bank_list(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,0U,out,capacity);}
uint16_t project_control_list_multis(uint16_t*out,uint16_t capacity){return bank_list(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,0U,out,capacity);}
uint8_t project_control_get_logical_asset(uint32_t kind,uint16_t logical,persist_control_asset_ref_t*out){const project_control_bank_slot_t*bank=NULL;uint16_t capacity=0U;if(out==NULL)return 0U;if(kind==PERSIST_ASSET_SAMPLE_STREAM)return classic_asset(logical,out);if(kind==PERSIST_ASSET_SAMPLE_RAM){bank=g_sample_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}else if(kind==PERSIST_ASSET_WAVETABLE){bank=g_wavetable_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}else if(kind==PERSIST_ASSET_MULTI){bank=g_multi_bank;capacity=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;}if(bank==NULL||logical>=capacity||bank[logical].used==0U||bank[logical].kind!=kind)return 0U;return asset_ref_make_canonical(kind,bank[logical].canonical_path,out);}
uint8_t project_control_resolve_sample_runtime(uint16_t logical,uint16_t*out_runtime,uint32_t*out_kind){const sample_global_slot_t*direct=sample_global_pool_get_slot(logical);if(direct!=NULL&&direct->kind==SAMPLE_GLOBAL_KIND_CLASSIC){if(out_runtime==NULL)return 0U;*out_runtime=logical;if(out_kind!=NULL)*out_kind=PERSIST_ASSET_SAMPLE_STREAM;return 1U;}if(bank_resolve(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime)==0U)return 0U;const sample_global_slot_t*s=sample_global_pool_get_slot(*out_runtime);if(s==NULL||s->kind!=SAMPLE_GLOBAL_KIND_RAM)return 0U;if(out_kind!=NULL)*out_kind=PERSIST_ASSET_SAMPLE_RAM;return 1U;}
uint8_t project_control_resolve_wavetable_runtime(uint16_t logical,uint16_t*out_runtime){return bank_resolve(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical,out_runtime);}
uint8_t project_control_resolve_multi_runtime(uint16_t logical,uint16_t*out_runtime){uint16_t runtime,found;if(out_runtime==NULL||logical>=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS||bank_resolve(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,&runtime)==0U)return 0U;const multi_sample_instrument_t*i=multi_sample_pool_get_instrument(runtime);if(i==NULL||multi_sample_pool_get_state(runtime)!=MULTI_SAMPLE_INSTRUMENT_READY||bank_find(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,PERSIST_ASSET_MULTI,i->index_path,&found)==0U||found!=logical)return 0U;*out_runtime=runtime;return 1U;}

static uint8_t project_control_publish_sampler_asset(uint8_t entity,
                                                      uint16_t logical)
{
    track_runtime_resolved_track_t resolved;
    uint16_t runtime = 0U;
    if (track_runtime_resolve_track(entity, &resolved) == 0U) return 0U;
    if (resolved.descriptor.type == TRACK_RUNTIME_TYPE_MULTI)
    {
        if (project_control_resolve_multi_runtime(logical, &runtime) == 0U) return 0U;
    }
    else
    {
        uint32_t kind = 0U;
        if (project_control_resolve_sample_runtime(logical, &runtime, &kind) == 0U) return 0U;
        if (((resolved.descriptor.type == TRACK_RUNTIME_TYPE_STREAM)
                && (kind != PERSIST_ASSET_SAMPLE_STREAM))
            || ((resolved.descriptor.type == TRACK_RUNTIME_TYPE_RAM)
                && (kind != PERSIST_ASSET_SAMPLE_RAM))) return 0U;
    }
    uint64_t sample = 0U;
    if (live_clock_read_audio_sample(&sample) == 0U) return 0U;
    return control_audio_publish_param(entity, CONTROL_AUDIO_SAMPLER_ASSET,
                                       runtime, 0U, sample);
}

uint8_t project_control_track_asset_get(uint8_t entity,
                                        project_control_asset_role_t role,
                                        persist_control_asset_ref_t *out_asset)
{
    if ((entity >= BRICK_ENTITY_CAPACITY)
            || (role >= PROJECT_CONTROL_ASSET_ROLE_COUNT)
            || (out_asset == NULL)) return 0U;
    *out_asset = g_track_assets[entity][role];
    return (out_asset->path_length != 0U) ? 1U : 0U;
}

uint8_t project_control_track_asset_get_logical(uint8_t entity,
                                                project_control_asset_role_t role,
                                                uint16_t *out_logical)
{
    if ((entity >= BRICK_ENTITY_CAPACITY)
            || (role >= PROJECT_CONTROL_ASSET_ROLE_COUNT)
            || (out_logical == NULL)) return 0U;
    const persist_control_asset_ref_t *const asset = &g_track_assets[entity][role];
    char path[PROJECT_CONTROL_ASSET_PATH_BYTES];
    if ((asset->path_length == 0U)
            || (asset->path_length >= PROJECT_CONTROL_ASSET_PATH_BYTES)) return 0U;
    memcpy(path, asset->canonical_path, asset->path_length);
    path[asset->path_length] = '\0';
    return project_control_find_asset(asset->kind, path, out_logical);
}

uint8_t project_control_track_asset_select_logical(uint8_t entity,
                                                   project_control_asset_role_t role,
                                                   uint16_t logical)
{
    if ((entity >= BRICK_ENTITY_CAPACITY)
            || (role >= PROJECT_CONTROL_ASSET_ROLE_COUNT)) return 0U;
    uint32_t kind = PERSIST_ASSET_WAVETABLE;
    if (role == PROJECT_CONTROL_ASSET_SAMPLER)
    {
        track_runtime_descriptor_t descriptor;
        if (track_runtime_get_descriptor(entity, &descriptor) == 0U) return 0U;
        if (descriptor.type == TRACK_RUNTIME_TYPE_STREAM) kind = PERSIST_ASSET_SAMPLE_STREAM;
        else if (descriptor.type == TRACK_RUNTIME_TYPE_RAM) kind = PERSIST_ASSET_SAMPLE_RAM;
        else if (descriptor.type == TRACK_RUNTIME_TYPE_MULTI) kind = PERSIST_ASSET_MULTI;
        else return 0U;
    }
    persist_control_asset_ref_t asset;
    if (project_control_get_logical_asset(kind, logical, &asset) == 0U) return 0U;
    const uint8_t published = (role == PROJECT_CONTROL_ASSET_SAMPLER)
        ? project_control_publish_sampler_asset(entity, logical)
        : audio_wave_table_projection_publish_track(entity,
            (uint8_t)(role - PROJECT_CONTROL_ASSET_WAVE_OSC1), logical);
    if (published == 0U) return 0U;
    g_track_assets[entity][role] = asset;
    return 1U;
}

uint8_t project_control_track_asset_restore(uint8_t entity,
                                            project_control_asset_role_t role,
                                            const persist_control_asset_ref_t *asset)
{
    return (project_control_track_asset_restore_status(entity, role, asset)
                == PROJECT_CONTROL_ASSET_READY) ? 1U : 0U;
}

project_control_asset_result_t project_control_track_asset_restore_status(
    uint8_t entity, project_control_asset_role_t role,
    const persist_control_asset_ref_t *asset)
{
    if (asset == NULL || project_control_validate_asset(asset) == 0U
        || entity >= BRICK_ENTITY_CAPACITY || role >= PROJECT_CONTROL_ASSET_ROLE_COUNT)
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    char path[PROJECT_CONTROL_ASSET_PATH_BYTES];
    memcpy(path, asset->canonical_path, asset->path_length);
    path[asset->path_length] = '\0';
    uint16_t logical = 0U;
    /* Pattern installation only binds assets which LOAD_ASSETS has already
     * made terminal.  Loading here would create a second attempt after P3. */
    if (project_control_find_asset(asset->kind, path, &logical) == 0U)
        return PROJECT_CONTROL_ASSET_FAILED;
    if (asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
    {
        if (sample_cache_get_state(logical) == SAMPLE_CACHE_READY_PARTIAL)
            return PROJECT_CONTROL_ASSET_PENDING;
        if (sample_cache_is_ready(logical) == 0U)
            return (sample_cache_get_state(logical) == SAMPLE_CACHE_ERROR)
                ? classic_failure_result(logical)
                : PROJECT_CONTROL_ASSET_FAILED;
    }
    if (asset->kind == PERSIST_ASSET_SAMPLE_RAM)
    {
        if (g_sample_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
            return PROJECT_CONTROL_ASSET_PENDING;
        if (project_control_ram_runtime_valid(logical) == 0U)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    if (asset->kind == PERSIST_ASSET_MULTI)
    {
        uint16_t runtime;
        if (project_control_resolve_multi_runtime(logical, &runtime) == 0U)
            return (g_multi_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
                ? PROJECT_CONTROL_ASSET_PENDING
                : PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    if (asset->kind == PERSIST_ASSET_WAVETABLE)
    {
        if (g_wavetable_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
            return PROJECT_CONTROL_ASSET_PENDING;
        if (project_control_wavetable_runtime_valid(logical) == 0U)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    return (project_control_track_asset_select_logical(entity, role, logical) != 0U)
        ? PROJECT_CONTROL_ASSET_READY : PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
}

uint8_t project_control_track_assets_clear(uint8_t entity)
{
    if (entity >= BRICK_ENTITY_CAPACITY) return 0U;
    memset(g_track_assets[entity], 0, sizeof(g_track_assets[entity]));
    return 1U;
}

uint8_t project_control_begin_asset_restore(void)
{
    project_control_reset_asset_banks();
    /* P2 has already cancelled ingress and retired every live resource.  Only
     * the stream/cache catalogue and the logical catalogue need reinitialising
     * here; resource-pool resets would duplicate the retirement proof. */
    sample_cache_init();
    sample_global_pool_reset();
    return 1U;
}

uint8_t project_control_validate_asset(const persist_control_asset_ref_t *asset)
{
    return asset_ref_is_canonical(asset);
}

project_control_asset_result_t project_control_put_asset(
    const persist_control_asset_ref_t *asset)
{
    if (project_control_validate_asset(asset) == 0U)
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    char path[PROJECT_CONTROL_ASSET_PATH_BYTES];
    memcpy(path, asset->canonical_path, asset->path_length);
    path[asset->path_length] = '\0';
    uint16_t logical = PROJECT_CONTROL_INVALID_RUNTIME;
    return project_control_ensure_asset(asset->kind, path, &logical);
}

static project_control_asset_result_t classic_asset_status(uint16_t logical)
{
    if (sample_cache_get_state(logical) == SAMPLE_CACHE_READY_PARTIAL)
        return PROJECT_CONTROL_ASSET_PENDING;
    const sample_cache_slot_readiness_t readiness =
        sample_cache_get_slot_readiness(logical);
    if (readiness == SAMPLE_CACHE_SLOT_PLAYABLE)
        return PROJECT_CONTROL_ASSET_READY;
    if ((readiness == SAMPLE_CACHE_SLOT_PREPARING)
            || (readiness == SAMPLE_CACHE_SLOT_START_PENDING))
        return PROJECT_CONTROL_ASSET_PENDING;
    if (readiness == SAMPLE_CACHE_SLOT_ERROR)
    {
        const project_control_asset_result_t result = classic_failure_result(logical);
        if (result == PROJECT_CONTROL_ASSET_FAILED_INTERNAL)
            return result;
    }
    sample_global_pool_clear_classic(logical);
    return PROJECT_CONTROL_ASSET_FAILED;
}

static project_control_asset_result_t ram_start_failure_result(void)
{
    return (sampler_ram_pool_get_last_result() == SAMPLER_RAM_RESULT_INVALID_ARG
                || sampler_ram_pool_get_last_result() == SAMPLER_RAM_RESULT_REGISTER_FAIL)
        ? PROJECT_CONTROL_ASSET_FAILED_INTERNAL
        : PROJECT_CONTROL_ASSET_FAILED;
}

static project_control_asset_result_t wavetable_start_failure_result(void)
{
    return (wavetable_pool_get_last_result() == WAVETABLE_RESULT_INVALID_ARG
                || wavetable_pool_get_last_result() == WAVETABLE_RESULT_REGISTER_FAIL)
        ? PROJECT_CONTROL_ASSET_FAILED_INTERNAL
        : PROJECT_CONTROL_ASSET_FAILED;
}

static project_control_asset_result_t multi_load_result_classify(
    multi_sample_load_result_t result)
{
    switch (result)
    {
        case MULTI_SAMPLE_LOAD_INVALID_ARG:
        case MULTI_SAMPLE_LOAD_POOL_FAIL:
        case MULTI_SAMPLE_LOAD_REGISTER_FAIL:
        case MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE:
        case MULTI_SAMPLE_LOAD_CANCELLED:
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        default:
            return PROJECT_CONTROL_ASSET_FAILED;
    }
}

static project_control_asset_result_t apply_bank_asset(uint32_t kind,uint16_t logical,const char*path)
{
    persist_control_asset_ref_t ref;
    if (asset_ref_make_canonical(kind, path, &ref) == 0U)
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    char canonical_path[PROJECT_CONTROL_ASSET_PATH_BYTES];
    memcpy(canonical_path, ref.canonical_path, ref.path_length);
    canonical_path[ref.path_length] = '\0';
    uint16_t runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    if(kind==PERSIST_ASSET_SAMPLE_STREAM)
    {
        if (logical >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
                || sample_global_pool_load_classic(logical,canonical_path) == 0U)
            return PROJECT_CONTROL_ASSET_FAILED;
        return PROJECT_CONTROL_ASSET_READY;
    }
    if(kind==PERSIST_ASSET_SAMPLE_RAM)
    {
        const uint16_t backend=sampler_ram_pool_find_free_slot();
        if (backend >= SAMPLER_RAM_POOL_MAX_SLOTS)
            return PROJECT_CONTROL_ASSET_FAILED;
        if (bank_set(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
                     logical,kind,canonical_path,runtime)==0U)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        g_sample_bank[logical].pending_runtime=backend;
        if(sampler_ram_pool_load_async_begin(backend,canonical_path)==0U)
        {
            (void)bank_remove(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
            return ram_start_failure_result();
        }
        return PROJECT_CONTROL_ASSET_PENDING;
    }
    if(kind==PERSIST_ASSET_WAVETABLE)
    {
        const uint16_t backend=wavetable_pool_find_free_slot();
        if (backend >= WAVETABLE_POOL_MAX_SLOTS)
            return PROJECT_CONTROL_ASSET_FAILED;
        if (bank_set(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
                     logical,kind,canonical_path,runtime) == 0U)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        g_wavetable_bank[logical].pending_runtime=backend;
        if (wavetable_pool_load_async_begin_with_geometry(
                    backend,canonical_path,WAVETABLE_SOURCE_GEOMETRY_2048) == 0U)
        {
            (void)bank_remove(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
            return wavetable_start_failure_result();
        }
        return PROJECT_CONTROL_ASSET_PENDING;
    }
    if(kind==PERSIST_ASSET_MULTI)
    {
        if (bank_set(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,logical,
                     kind,canonical_path,runtime) == 0U)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        for(uint16_t backend=0U;backend<MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;++backend)
            if(multi_sample_pool_get_state(backend)==MULTI_SAMPLE_INSTRUMENT_EMPTY)
            {
                const multi_sample_load_result_t result =
                    multi_sample_load_instrument(logical,canonical_path,backend);
                if (result == MULTI_SAMPLE_LOAD_OK)
                    return PROJECT_CONTROL_ASSET_PENDING;
                if (result == MULTI_SAMPLE_LOAD_ALREADY_READY)
                    return PROJECT_CONTROL_ASSET_READY;
                (void)bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,
                                  logical);
                return multi_load_result_classify(result);
            }
        (void)bank_remove(g_multi_bank,MULTI_SAMPLE_POOL_MAX_INSTRUMENTS,
                          logical);
        return PROJECT_CONTROL_ASSET_FAILED;
    }
    return PROJECT_CONTROL_ASSET_FAILED;
}

project_control_asset_result_t project_control_ensure_asset(uint32_t kind,const char*path,uint16_t*out_logical)
{
    if(path==NULL||path[0]=='\0'||out_logical==NULL)return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    if(kind==PERSIST_ASSET_SAMPLE_STREAM)
    {
        if(classic_find(path,out_logical)!=0U)
            return classic_asset_status(*out_logical);
        const uint16_t slot=sample_global_pool_find_free_slot();
        if(slot==SAMPLE_GLOBAL_POOL_INVALID_INDEX||sample_global_pool_load_classic(slot,path)==0U)return PROJECT_CONTROL_ASSET_FAILED;
        *out_logical=slot;
        return classic_asset_status(slot);
    }
    project_control_bank_slot_t*bank=NULL;uint16_t capacity=0U;
    if(kind==PERSIST_ASSET_SAMPLE_RAM){bank=g_sample_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}
    else if(kind==PERSIST_ASSET_WAVETABLE){bank=g_wavetable_bank;capacity=SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS;}
    else if(kind==PERSIST_ASSET_MULTI){bank=g_multi_bank;capacity=MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;}
    else return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    if(bank_find(bank,capacity,kind,path,out_logical)!=0U)
    {
        if(bank[*out_logical].pending_runtime!=PROJECT_CONTROL_INVALID_RUNTIME)return PROJECT_CONTROL_ASSET_PENDING;
        if (bank[*out_logical].runtime == PROJECT_CONTROL_INVALID_RUNTIME)
            return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        if (kind == PERSIST_ASSET_SAMPLE_RAM)
            return (project_control_ram_runtime_valid(*out_logical) != 0U)
                ? PROJECT_CONTROL_ASSET_READY
                : PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        if (kind == PERSIST_ASSET_WAVETABLE)
            return (project_control_wavetable_runtime_valid(*out_logical) != 0U)
                ? PROJECT_CONTROL_ASSET_READY
                : PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        if (kind == PERSIST_ASSET_MULTI)
        {
            uint16_t runtime;
            return (project_control_resolve_multi_runtime(*out_logical, &runtime) != 0U)
                ? PROJECT_CONTROL_ASSET_READY
                : PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        }
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    for(uint16_t i=0U;i<capacity;++i)if(!bank[i].used){const project_control_asset_result_t result=apply_bank_asset(kind,i,path);if(bank[i].used){*out_logical=i;return result;}return result;}
    return PROJECT_CONTROL_ASSET_FAILED;
}

project_control_asset_result_t project_control_complete_ram_runtime(
    const char*path,uint16_t backend,uint16_t runtime,uint8_t success)
{
    uint16_t logical=0U;
    if(path==NULL||backend>=SAMPLER_RAM_POOL_MAX_SLOTS
        ||bank_find(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
                     PERSIST_ASSET_SAMPLE_RAM,path,&logical)==0U
        ||g_sample_bank[logical].pending_runtime!=backend)
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    if (success == 0U)
    {
        (void)bank_remove(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED;
    }
    const sampler_ram_slot_t *const slot = sampler_ram_pool_get_slot(backend);
    if (runtime >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS || slot == NULL
        || slot->state != SAMPLER_RAM_SLOT_READY
        || slot->global_slot != runtime)
    {
        (void)bank_remove(g_sample_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    g_sample_bank[logical].runtime=runtime;
    g_sample_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    return PROJECT_CONTROL_ASSET_READY;
}

project_control_asset_result_t project_control_complete_wavetable_runtime(const char*path,
                                                uint16_t backend,
                                                uint16_t runtime,
                                                uint8_t success)
{
    uint16_t logical=0U;
    if (path==NULL || backend>=WAVETABLE_POOL_MAX_SLOTS
        || bank_find(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,
                     PERSIST_ASSET_WAVETABLE,path,&logical)==0U
        || g_wavetable_bank[logical].pending_runtime!=backend)
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    if (success == 0U)
    {
        (void)bank_remove(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED;
    }
    const wavetable_slot_t *const slot = wavetable_pool_get_slot(backend);
    if (runtime >= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS || slot == NULL
        || slot->state != WAVETABLE_SLOT_READY
        || slot->global_slot != runtime)
    {
        (void)bank_remove(g_wavetable_bank,SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS,logical);
        return PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
    }
    g_wavetable_bank[logical].runtime=runtime;
    g_wavetable_bank[logical].pending_runtime=PROJECT_CONTROL_INVALID_RUNTIME;
    return PROJECT_CONTROL_ASSET_READY;
}

uint8_t project_control_asset_loads_pending(void)
{
    for (uint16_t logical = 0U; logical < SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS; ++logical)
    {
        const sample_global_slot_t *const classic =
            sample_global_pool_get_slot(logical);
        if (classic != NULL && classic->kind == SAMPLE_GLOBAL_KIND_CLASSIC
            && (sample_cache_get_state(logical) == SAMPLE_CACHE_PREPARING
                || sample_cache_get_state(logical) == SAMPLE_CACHE_PREFILLING
                || sample_cache_get_state(logical) == SAMPLE_CACHE_READY_PARTIAL))
            return 1U;
        if (g_sample_bank[logical].used != 0U
            && g_sample_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
            return 1U;
        if (g_wavetable_bank[logical].used != 0U
            && g_wavetable_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
            return 1U;
    }
    for (uint16_t logical = 0U; logical < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS; ++logical)
        if (g_multi_bank[logical].used != 0U
            && g_multi_bank[logical].pending_runtime != PROJECT_CONTROL_INVALID_RUNTIME)
            return 1U;
    return 0U;
}
