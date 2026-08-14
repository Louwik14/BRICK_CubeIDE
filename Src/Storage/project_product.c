#include "Storage/project_product.h"
#include "Storage/persistent_project_control.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/sd_access_gate.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Core/project_control.h"
#include "Sampler/multi_sample_loader.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

static uint8_t g_present[PROJECT_PRODUCT_SLOT_COUNT],g_active_valid,g_active;
static project_product_progress_t g_progress;

static uint8_t path(char*out,uint32_t size,uint8_t slot){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.B6C",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return 0U;}return 1U;}

void project_product_refresh_slots(void){memset(g_present,0,sizeof(g_present));if(!acquire())return;(void)f_mkdir("0:/BRICK");(void)f_mkdir("0:/BRICK/PROJECT");for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT;++s){char x[48];FILINFO i;if(path(x,sizeof(x),s)&&f_stat(x,&i)==FR_OK)g_present[s]=1U;}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
void project_product_init(void){memset(&g_progress,0,sizeof(g_progress));g_active_valid=0U;g_active=0U;project_product_refresh_slots();boot_context_flash_init();}
uint8_t project_product_list_slots(uint8_t*out,uint8_t cap){uint8_t n=0U;if(out==NULL)return 0U;for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT&&n<cap;++s)if(g_present[s])out[n++]=s;return n;}
uint8_t project_product_slot_present(uint8_t s){return(s<PROJECT_PRODUCT_SLOT_COUNT)?g_present[s]:0U;}

static const persist_control_pattern_t*working_get(void*ctx){(void)ctx;persist_control_pattern_record_t*r=pattern_live_project_record_workspace();return(r!=NULL&&persistent_pattern_control_capture(&r->content)==PERSIST_CODEC_OK)?&r->content:NULL;}
static const persist_control_pattern_record_t*pattern_get(void*ctx,uint16_t ordinal){(void)ctx;persist_control_pattern_record_t*r=pattern_live_project_record_workspace();return(r!=NULL&&pattern_control_bank_get_ordinal_project(ordinal,r))?r:NULL;}
static const persist_control_asset_ref_t*asset_get(void*ctx,uint16_t ordinal){persist_codec_project_workspace_t*w=ctx;return project_control_get_asset_ordinal(ordinal,&w->asset)?&w->asset:NULL;}

uint8_t project_product_save(uint8_t slot)
{
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT)return 0U;
    persist_codec_project_workspace_t *const workspace = persistence_workspace_acquire_project();
    if(workspace==NULL)return 0U;
    if(!acquire()){persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);return 0U;}
    persist_codec_project_source_t source;memset(&source,0,sizeof(source));
    persistent_project_control_capture_metadata(&source.metadata);
    source.metadata.pattern_count=pattern_control_bank_count();
    source.metadata.asset_count=project_control_asset_count();
    source.working_pattern=(persist_codec_working_pattern_provider_t){working_get,NULL};
    source.assets=(persist_codec_asset_provider_t){source.metadata.asset_count,asset_get,workspace};
    source.macros=project_control_macros_view();
    source.patterns=(persist_codec_pattern_provider_t){pattern_get,NULL};
    char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot);
    if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t sink=persistent_fatfs_sink(&f);ok=(persist_codec_encode_project(&source,&sink,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);
    if(ok){g_present[slot]=1U;g_active=slot;g_active_valid=1U;(void)boot_context_flash_commit(slot);}return ok;
}

static uint8_t begin_assets(void*ctx){(void)ctx;return project_control_begin_asset_restore();}
static uint8_t validate_asset(void*ctx,const persist_control_asset_ref_t*a){(void)ctx;return project_control_validate_asset(a);}
static uint8_t put_asset(void*ctx,const persist_control_asset_ref_t*a){(void)ctx;return project_control_put_asset(a);}
static uint8_t apply_working(void*ctx,const persist_codec_project_metadata_t*m,const persist_control_pattern_t*p){(void)ctx;return(persistent_project_control_apply_working(m,p,0U)==PERSIST_CODEC_OK)?1U:0U;}
static uint8_t apply_macros(void*ctx,const persist_control_macros_t*m){(void)ctx;return project_control_apply_macros(m);}
static uint8_t begin_patterns(void*ctx){(void)ctx;return pattern_control_bank_begin_project();}
static uint8_t put_pattern(void*ctx,const persist_control_pattern_record_t*r){(void)ctx;return pattern_control_bank_put_record_project(r);}

uint8_t project_product_load(uint8_t slot)
{
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[slot])return 0U;
    persist_codec_project_workspace_t *const workspace = persistence_workspace_acquire_project();
    if(workspace==NULL)return 0U;
    if(!acquire()){persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);return 0U;}
    char x[48];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot)&&persistent_fatfs_open_read(&f,x);g_progress=(project_product_progress_t){1U,0U,0U,1U};persist_codec_source_t source={0};if(ok)source=persistent_fatfs_source(&f);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    persist_codec_result_t r=PERSIST_CODEC_IO_ERROR;
    if(ok){persist_codec_project_consumer_t project={begin_assets,validate_asset,put_asset,apply_working,apply_macros,NULL};persist_codec_pattern_consumer_t patterns={begin_patterns,put_pattern,pattern_control_bank_commit,pattern_control_bank_abort,NULL};r=persist_codec_decode_project_progressive(&source,workspace,&project,&patterns);}
    ok=(ok&&r==PERSIST_CODEC_OK);if(source.context!=NULL)persistent_fatfs_close(&f);persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);g_progress.done=1U;g_progress.complete=(ok&&multi_sample_load_has_pending()!=0U)?0U:1U;g_progress.active=(ok&&g_progress.complete==0U)?1U:0U;if(ok){g_active=slot;g_active_valid=1U;(void)boot_context_flash_commit(slot);}return ok;
}

uint8_t project_product_delete(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;char x[48];FRESULT r=FR_INVALID_NAME;if(path(x,sizeof(x),slot))r=f_unlink(x);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);uint8_t ok=(r==FR_OK||r==FR_NO_FILE);if(ok){g_present[slot]=0U;if(g_active_valid&&g_active==slot)g_active_valid=0U;}return ok;}
uint8_t project_product_blank(void){persist_codec_project_workspace_t*w=persistence_workspace_acquire_project();if(w==NULL)return 0U;persist_control_pattern_t*p=&w->unit.pattern_record.content;uint8_t ok=(pattern_live_get_control_boot(p)&&project_control_begin_asset_restore())?1U:0U;if(ok){project_control_reset_macros();ok=pattern_control_bank_clear();}if(ok){g_active_valid=0U;ok=(persistent_pattern_control_apply(p,0U)==PERSIST_CODEC_OK)?1U:0U;}if(ok){pattern_live_set_active_state(0U,0U,0U,0U,0U);boot_context_flash_clear();}persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);return ok;}
uint8_t project_product_restore_boot(void){boot_context_flash_data_t c;if(!boot_context_flash_load(&c)||c.active_project_slot>=PROJECT_PRODUCT_SLOT_COUNT)return 0U;return project_product_load(c.active_project_slot);}
uint8_t project_product_get_progress(project_product_progress_t*out){if(out==NULL)return 0U;if(g_progress.active&&multi_sample_load_has_pending()==0U){g_progress.active=0U;g_progress.complete=1U;}*out=g_progress;return 1U;}
