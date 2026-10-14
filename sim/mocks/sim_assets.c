#include "sim_assets.h"

#include <stdio.h>
#include <string.h>

#include "Storage/wav_loader.h"
#include "Storage/project_product.h"
#include "Storage/patch_product.h"
#include "Track/track_types.h"

typedef struct { uint8_t used; char name[PROJECT_PRODUCT_NAME_BYTES + 1U]; } sim_project_t;
typedef struct { uint8_t used; patch_product_metadata_t metadata; } sim_patch_t;

static wav_loader_catalog_entry_t g_wavs[9];
static sim_project_t g_projects[PROJECT_PRODUCT_SLOT_COUNT];
static sim_patch_t g_patches[PATCH_PRODUCT_SLOT_COUNT];
static uint16_t g_current_patch;
static patch_product_operation_t g_patch_operation;
static patch_product_result_t g_patch_result;
static uint16_t g_patch_result_slot;
static uint8_t g_project_save_result;
static uint8_t g_project_save_slot;
static uint8_t g_project_rename_result;
static uint8_t g_project_rename_slot;

static void wav(unsigned i, const char *path, const char *name, uint16_t parent,
                wav_loader_catalog_entry_type_t type)
{
    (void)snprintf(g_wavs[i].path, sizeof(g_wavs[i].path), "%s", path);
    (void)snprintf(g_wavs[i].name, sizeof(g_wavs[i].name), "%s", name);
    g_wavs[i].parent_id = parent;
    g_wavs[i].type = type;
    g_wavs[i].state = WAV_LOADER_CATALOG_READY;
    g_wavs[i].size = type == WAV_LOADER_CATALOG_ENTRY_FILE ? 48000U : 0U;
}

void sim_assets_init(void)
{
    memset(g_wavs, 0, sizeof(g_wavs));
    wav(0, "0:/Samples/Drums", "Drums", WAV_LOADER_CATALOG_ROOT_PARENT, WAV_LOADER_CATALOG_ENTRY_DIR);
    wav(1, "0:/Samples/Textures", "Textures", WAV_LOADER_CATALOG_ROOT_PARENT, WAV_LOADER_CATALOG_ENTRY_DIR);
    wav(2, "0:/Samples/Kick 01.wav", "Kick 01.wav", WAV_LOADER_CATALOG_ROOT_PARENT, WAV_LOADER_CATALOG_ENTRY_FILE);
    wav(3, "0:/Samples/A very long evolving atmosphere.wav", "A very long evolving atmosphere.wav", WAV_LOADER_CATALOG_ROOT_PARENT, WAV_LOADER_CATALOG_ENTRY_FILE);
    wav(4, "0:/Samples/Drums/Kicks", "Kicks", 0U, WAV_LOADER_CATALOG_ENTRY_DIR);
    wav(5, "0:/Samples/Drums/Snare Bright.wav", "Snare Bright.wav", 0U, WAV_LOADER_CATALOG_ENTRY_FILE);
    wav(6, "0:/Samples/Drums/Hat Closed.wav", "Hat Closed.wav", 0U, WAV_LOADER_CATALOG_ENTRY_FILE);
    wav(7, "0:/Samples/Drums/Kicks/Analog Kick.wav", "Analog Kick.wav", 4U, WAV_LOADER_CATALOG_ENTRY_FILE);
    wav(8, "0:/Samples/Textures/Empty", "Empty", 1U, WAV_LOADER_CATALOG_ENTRY_DIR);

    memset(g_projects, 0, sizeof(g_projects));
    g_projects[0].used = 1U; (void)snprintf(g_projects[0].name, sizeof(g_projects[0].name), "MANUAL DEMO");
    g_projects[3].used = 1U; (void)snprintf(g_projects[3].name, sizeof(g_projects[3].name), "LIVE SET 01");

    memset(g_patches, 0, sizeof(g_patches));
    const char *names[] = { "Warm Prism", "Glass Keys", "Deep Motion", "Init Lead" };
    for (unsigned i = 0U; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        g_patches[i].used = 1U;
        (void)snprintf(g_patches[i].metadata.name, sizeof(g_patches[i].metadata.name), "%s", names[i]);
        g_patches[i].metadata.family = TRACK_FAMILY_SYNTH;
        g_patches[i].metadata.type = TRACK_TYPE_PRISM;
        g_patches[i].metadata.summary_family = TRACK_FAMILY_SYNTH;
        g_patches[i].metadata.summary_type = TRACK_TYPE_PRISM;
    }
    g_current_patch = 0U;
    g_patch_operation = PATCH_PRODUCT_OPERATION_NONE;
    g_patch_result = PATCH_PRODUCT_OK;
    g_project_save_result = g_project_rename_result = 0U;
}

uint16_t wav_loader_catalog_count(void) { return (uint16_t)(sizeof(g_wavs) / sizeof(g_wavs[0])); }
uint16_t wav_loader_catalog_child_count(uint16_t parent)
{ uint16_t n=0U; for(unsigned i=0U;i<wav_loader_catalog_count();++i) if(g_wavs[i].parent_id==parent)++n; return n; }
uint16_t wav_loader_catalog_get_child_index(uint16_t parent, uint16_t child)
{ for(uint16_t i=0U,n=0U;i<wav_loader_catalog_count();++i) if(g_wavs[i].parent_id==parent && n++==child)return i; return WAV_LOADER_CATALOG_ROOT_PARENT; }
const wav_loader_catalog_entry_t *wav_loader_catalog_get(uint16_t index)
{ return index < wav_loader_catalog_count() ? &g_wavs[index] : NULL; }
const wav_loader_catalog_entry_t *wav_loader_catalog_get_child(uint16_t parent, uint16_t child)
{ const uint16_t i=wav_loader_catalog_get_child_index(parent,child); return i==WAV_LOADER_CATALOG_ROOT_PARENT?NULL:&g_wavs[i]; }
uint8_t wav_loader_catalog_find_path(const char *path,uint16_t *out,wav_loader_catalog_entry_t *entry)
{ for(uint16_t i=0U;i<wav_loader_catalog_count();++i)if(path&&strcmp(path,g_wavs[i].path)==0){if(out)*out=i;if(entry)*entry=g_wavs[i];return 1U;}return 0U; }
uint8_t wav_loader_catalog_loaded(void){return 1U;} uint8_t wav_loader_catalog_stale(void){return 0U;}
uint8_t wav_loader_catalog_view_busy(void){return 0U;} uint8_t wav_loader_catalog_last_sd_busy(void){return 0U;}
uint8_t wav_loader_catalog_last_io_error(void){return 0U;} uint8_t wav_loader_catalog_truncated(void){return 0U;}
uint8_t wav_loader_catalog_path_truncated(void){return 0U;} void wav_loader_catalog_refresh(void){}
wav_loader_catalog_view_service_result_t wav_loader_catalog_view_service(void){return WAV_LOADER_CATALOG_VIEW_PUBLISHED;}

void project_product_refresh_slots(void){}
uint8_t project_product_list_slots(uint8_t *out,uint8_t capacity)
{ uint8_t n=0U; for(uint8_t i=0U;i<PROJECT_PRODUCT_SLOT_COUNT&&n<capacity;++i)if(g_projects[i].used)out[n++]=i;return n; }
uint8_t project_product_metadata(uint8_t slot,project_product_metadata_t *out)
{if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_projects[slot].used||!out)return 0U;(void)snprintf(out->name,sizeof(out->name),"%s",g_projects[slot].name);return 1U;}
uint8_t project_product_delete(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_projects[slot].used)return 0U;g_projects[slot].used=0U;return 1U;}
uint8_t project_product_load(uint8_t slot){return slot<PROJECT_PRODUCT_SLOT_COUNT&&g_projects[slot].used;}
uint8_t project_product_load_busy(void){return 0U;} uint8_t project_product_save_busy(void){return 0U;} uint8_t project_product_rename_busy(void){return 0U;}
uint8_t project_product_new_blank(void){return 1U;}
uint8_t project_product_save_existing(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_projects[slot].used)return 0U;g_project_save_slot=slot;g_project_save_result=1U;return 1U;}
uint8_t project_product_save_as_named(const char *name,uint8_t *slot){if(!name||!slot)return 0U;for(uint8_t i=0U;i<PROJECT_PRODUCT_SLOT_COUNT;++i)if(!g_projects[i].used){g_projects[i].used=1U;(void)snprintf(g_projects[i].name,sizeof(g_projects[i].name),"%s",name);*slot=i;g_project_save_slot=i;g_project_save_result=1U;return 1U;}return 0U;}
uint8_t project_product_save_take_result(uint8_t *slot,uint8_t *success){if(!g_project_save_result)return 0U;g_project_save_result=0U;if(slot)*slot=g_project_save_slot;if(success)*success=1U;return 1U;}
project_product_save_error_t project_product_save_last_error(void){return PROJECT_PRODUCT_SAVE_ERROR_NONE;}
uint8_t project_product_rename(uint8_t slot,const char *name){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_projects[slot].used||!name)return 0U;(void)snprintf(g_projects[slot].name,sizeof(g_projects[slot].name),"%s",name);g_project_rename_slot=slot;g_project_rename_result=1U;return 1U;}
uint8_t project_product_rename_take_result(uint8_t *slot,uint8_t *success){if(!g_project_rename_result)return 0U;g_project_rename_result=0U;if(slot)*slot=g_project_rename_slot;if(success)*success=1U;return 1U;}

patch_product_slot_state_t patch_product_slot_state(uint16_t slot){return slot<PATCH_PRODUCT_SLOT_COUNT&&g_patches[slot].used?PATCH_PRODUCT_SLOT_VALID:PATCH_PRODUCT_SLOT_EMPTY;}
uint8_t patch_product_metadata(uint16_t slot,patch_product_metadata_t *out){if(patch_product_slot_state(slot)!=PATCH_PRODUCT_SLOT_VALID||!out)return 0U;*out=g_patches[slot].metadata;return 1U;}
uint16_t patch_product_first_empty(void){for(uint16_t i=0U;i<PATCH_PRODUCT_SLOT_COUNT;++i)if(!g_patches[i].used)return i;return PATCH_PRODUCT_INVALID_SLOT;}
void patch_product_set_current(uint16_t slot){if(slot<PATCH_PRODUCT_SLOT_COUNT)g_current_patch=slot;} uint16_t patch_product_get_current(void){return g_current_patch;}
static patch_product_result_t patch_queue(patch_product_operation_t op,uint16_t slot){g_patch_operation=op;g_patch_result_slot=slot;g_patch_result=PATCH_PRODUCT_OK;return PATCH_PRODUCT_PENDING;}
patch_product_result_t patch_product_load_begin(uint16_t slot,uint16_t mask){(void)mask;return patch_product_slot_state(slot)==PATCH_PRODUCT_SLOT_VALID?patch_queue(PATCH_PRODUCT_OPERATION_LOAD,slot):PATCH_PRODUCT_EMPTY;}
patch_product_result_t patch_product_preview_begin(uint16_t slot){return patch_product_slot_state(slot)==PATCH_PRODUCT_SLOT_VALID?patch_queue(PATCH_PRODUCT_OPERATION_PREVIEW,slot):PATCH_PRODUCT_EMPTY;}
patch_product_result_t patch_product_save_prepare(uint8_t entity,uint16_t slot,const char *name){(void)entity;(void)name;return slot<PATCH_PRODUCT_SLOT_COUNT?PATCH_PRODUCT_OK:PATCH_PRODUCT_INVALID;}
patch_product_result_t patch_product_save_submit(uint16_t slot,const char *name){if(slot>=PATCH_PRODUCT_SLOT_COUNT||!name)return PATCH_PRODUCT_INVALID;g_patches[slot].used=1U;(void)snprintf(g_patches[slot].metadata.name,sizeof(g_patches[slot].metadata.name),"%s",name);g_patches[slot].metadata.family=TRACK_FAMILY_SYNTH;g_patches[slot].metadata.type=TRACK_TYPE_PRISM;return patch_queue(PATCH_PRODUCT_OPERATION_SAVE,slot);}
void patch_product_save_cancel_prepare(void){}
patch_product_result_t patch_product_overwrite(uint8_t entity,uint16_t slot){(void)entity;return patch_product_slot_state(slot)==PATCH_PRODUCT_SLOT_VALID?patch_queue(PATCH_PRODUCT_OPERATION_OVERWRITE,slot):PATCH_PRODUCT_EMPTY;}
patch_product_result_t patch_product_rename_begin(uint16_t slot,const char *name){if(patch_product_slot_state(slot)!=PATCH_PRODUCT_SLOT_VALID||!name)return PATCH_PRODUCT_EMPTY;(void)snprintf(g_patches[slot].metadata.name,sizeof(g_patches[slot].metadata.name),"%s",name);return patch_queue(PATCH_PRODUCT_OPERATION_RENAME,slot);}
patch_product_result_t patch_product_delete(uint16_t slot,uint16_t *next){if(patch_product_slot_state(slot)!=PATCH_PRODUCT_SLOT_VALID)return PATCH_PRODUCT_EMPTY;g_patches[slot].used=0U;if(next)*next=patch_product_first_empty();return PATCH_PRODUCT_OK;}
patch_product_result_t patch_product_clear(uint8_t entity){(void)entity;g_current_patch=PATCH_PRODUCT_INVALID_SLOT;return PATCH_PRODUCT_OK;}
uint8_t patch_product_result_pending(patch_product_operation_t op){return g_patch_operation==op;}
uint8_t patch_product_take_result(patch_product_operation_t *op,uint16_t *slot,patch_product_result_t *result){if(g_patch_operation==PATCH_PRODUCT_OPERATION_NONE)return 0U;if(op)*op=g_patch_operation;if(slot)*slot=g_patch_result_slot;if(result)*result=g_patch_result;g_patch_operation=PATCH_PRODUCT_OPERATION_NONE;return 1U;}
const char *patch_product_result_label(patch_product_result_t result){return result==PATCH_PRODUCT_OK?"OK":result==PATCH_PRODUCT_PENDING?"WORKING":"FAILED";}
