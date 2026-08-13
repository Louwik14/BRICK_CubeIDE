#include "Core/project_control.h"
#include "Storage/memory_layout.h"
#include "Storage/persistent_key_catalog.h"
#include "Param/param_macro.h"
#include <string.h>

#define PROJECT_CONTROL_ASSET_PATH_BYTES (PERSIST_CONTROL_ASSET_PATH_BYTES + 1U)
typedef struct { uint8_t used; uint32_t kind; char path[PROJECT_CONTROL_ASSET_PATH_BYTES]; } project_control_entity_asset_t;
CONTROL_STATE_SDRAM static persist_control_macros_t g_macros;
CONTROL_STATE_SDRAM static project_control_entity_asset_t g_assets[PERSIST_CONTROL_ENTITY_COUNT];

static uint8_t text_copy(char*dst,const char*src){uint16_t i=0U;if(dst==NULL||src==NULL)return 0U;while(i+1U<PROJECT_CONTROL_ASSET_PATH_BYTES&&src[i]!='\0'){dst[i]=src[i];++i;}dst[i]='\0';return src[i]=='\0';}
static uint32_t asset_id(uint8_t entity){return(uint32_t)entity+1U;}
void project_control_init(void){memset(&g_macros,0,sizeof(g_macros));memset(g_assets,0,sizeof(g_assets));g_macros.hall_switch_key=PERSIST_MACRO_HALL_SCENE;for(uint8_t i=0U;i<PERSIST_CONTROL_MACRO_COUNT;++i)g_macros.selected_scene[i]=i;}
project_control_hall_mode_t project_control_get_hall_mode(void){return(g_macros.hall_switch_key==PERSIST_MACRO_HALL_SWITCH)?PROJECT_CONTROL_HALL_SWITCH:PROJECT_CONTROL_HALL_SCENE;}
uint8_t project_control_set_hall_mode(project_control_hall_mode_t mode){if(mode>PROJECT_CONTROL_HALL_SWITCH)return 0U;g_macros.hall_switch_key=(mode==PROJECT_CONTROL_HALL_SWITCH)?PERSIST_MACRO_HALL_SWITCH:PERSIST_MACRO_HALL_SCENE;return 1U;}
uint8_t project_control_get_macro_scene(uint8_t macro){return(macro<PERSIST_CONTROL_MACRO_COUNT)?g_macros.selected_scene[macro]:0U;}
uint8_t project_control_set_macro_scene(uint8_t macro,uint8_t scene){if(macro>=PERSIST_CONTROL_MACRO_COUNT||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros.selected_scene[macro]=scene;return 1U;}
uint8_t project_control_get_scene_lock(uint8_t scene,uint8_t lock,project_control_macro_lock_t*out){if(out==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;const persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(lock>=s->lock_count){out->track=0xFFU;out->param=PARAM_COUNT;out->scene_value=0.0f;return 1U;}if(persist_key_param_from_disk(s->locks[lock].parameter,&out->param)==0U)return 0U;out->track=s->locks[lock].entity;out->scene_value=s->locks[lock].value.f32;return 1U;}
uint8_t project_control_set_scene_lock(uint8_t scene,uint8_t lock,const project_control_macro_lock_t*in){if(in==NULL||scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;persist_control_macro_scene_t*s=&g_macros.scenes[scene];if(in->track==0xFFU||in->param>=PARAM_COUNT){if(lock<s->lock_count){for(uint8_t i=lock;i+1U<s->lock_count;++i)s->locks[i]=s->locks[i+1U];--s->lock_count;}param_macro_sync_scene_sources();return 1U;}persist_control_parameter_key_t key;if(in->track>=PERSIST_CONTROL_ENTITY_COUNT||persist_key_param_to_disk(in->param,&key)==0U)return 0U;if(lock>s->lock_count)return 0U;if(lock==s->lock_count){if(s->lock_count>=PERSIST_CONTROL_MACRO_LOCK_COUNT)return 0U;++s->lock_count;}s->locks[lock]=(persist_control_macro_lock_t){in->track,key,PERSIST_VALUE_FLOAT32,{.f32=in->scene_value}};param_macro_sync_scene_sources();return 1U;}
uint8_t project_control_scene_lock_is_empty(uint8_t scene,uint8_t lock){return(scene>=PERSIST_CONTROL_MACRO_SCENE_COUNT||lock>=g_macros.scenes[scene].lock_count)?1U:0U;}
uint8_t project_control_capture_macros(persist_control_macros_t*out){if(out==NULL)return 0U;*out=g_macros;return 1U;}
uint8_t project_control_apply_macros(const persist_control_macros_t*in){if(in==NULL)return 0U;for(uint8_t m=0U;m<PERSIST_CONTROL_MACRO_COUNT;++m)if(in->selected_scene[m]>=PERSIST_CONTROL_MACRO_SCENE_COUNT)return 0U;g_macros=*in;param_macro_sync_scene_sources();return 1U;}
uint8_t project_control_set_entity_asset(uint8_t entity,uint32_t kind,const char*path){if(entity>=PERSIST_CONTROL_ENTITY_COUNT||path==NULL||path[0]=='\0')return 0U;project_control_entity_asset_t next={.used=1U,.kind=kind};if(text_copy(next.path,path)==0U)return 0U;g_assets[entity]=next;return 1U;}
uint8_t project_control_clear_entity_asset(uint8_t entity){if(entity>=PERSIST_CONTROL_ENTITY_COUNT)return 0U;memset(&g_assets[entity],0,sizeof(g_assets[entity]));return 1U;}
uint8_t project_control_get_entity_asset(uint8_t entity,persist_control_asset_ref_t*out){if(entity>=PERSIST_CONTROL_ENTITY_COUNT||out==NULL||g_assets[entity].used==0U)return 0U;memset(out,0,sizeof(*out));out->id=asset_id(entity);out->kind=g_assets[entity].kind;out->path_length=(uint16_t)strlen(g_assets[entity].path);memcpy(out->path,g_assets[entity].path,out->path_length);return 1U;}
uint16_t project_control_capture_assets(persist_control_asset_ref_t*out,uint16_t capacity){uint16_t n=0U;if(out==NULL)return 0U;for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT&&n<capacity;++e)if(project_control_get_entity_asset(e,&out[n])!=0U)++n;return n;}
uint8_t project_control_apply_assets(const persist_control_asset_ref_t*assets,uint16_t count,const persist_control_pattern_t*pattern){if((count!=0U&&assets==NULL)||pattern==NULL)return 0U;memset(g_assets,0,sizeof(g_assets));for(uint8_t e=0U;e<PERSIST_CONTROL_ENTITY_COUNT;++e){uint32_t id=pattern->entities[e].asset;if(id==0U)continue;uint16_t i;for(i=0U;i<count&&assets[i].id!=id;++i){}if(i==count)return 0U;char path[PROJECT_CONTROL_ASSET_PATH_BYTES];uint16_t n=assets[i].path_length;if(n>=sizeof(path))return 0U;memcpy(path,assets[i].path,n);path[n]='\0';if(project_control_set_entity_asset(e,assets[i].kind,path)==0U)return 0U;}return 1U;}
