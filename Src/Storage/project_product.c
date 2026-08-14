#include "Storage/project_product.h"
#include "Storage/persistent_project_control.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/sd_access_gate.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Core/project_control.h"
#include "Sampler/multi_sample_loader.h"
#include "ff.h"
#include "Storage/memory_layout.h"
#include <stdio.h>
#include <string.h>
static uint8_t g_present[PROJECT_PRODUCT_SLOT_COUNT],g_active_valid,g_active;
static uint32_t g_counter;
static project_product_progress_t g_progress;
STORAGE_STATE_SDRAM static persist_codec_project_workspace_t g_workspace;
static uint8_t path(char*out,uint32_t size,uint8_t slot){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.B6C",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t count_path(char*out,uint32_t size,uint8_t slot){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.CNT",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return 0U;}return 1U;}
void project_product_refresh_slots(void){memset(g_present,0,sizeof(g_present));if(!acquire())return;(void)f_mkdir("0:/BRICK");(void)f_mkdir("0:/BRICK/PROJECT");for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT;++s){char x[48];FILINFO i;if(path(x,sizeof(x),s)&&f_stat(x,&i)==FR_OK)g_present[s]=1U;}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
void project_product_init(void){memset(&g_progress,0,sizeof(g_progress));g_active_valid=0U;g_active=0U;g_counter=0U;project_product_refresh_slots();boot_context_flash_init();}
uint8_t project_product_list_slots(uint8_t*out,uint8_t cap){uint8_t n=0U;if(out==NULL)return 0U;for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT&&n<cap;++s)if(g_present[s])out[n++]=s;return n;}
uint8_t project_product_slot_present(uint8_t s){return(s<PROJECT_PRODUCT_SLOT_COUNT)?g_present[s]:0U;}
static const persist_control_pattern_record_t*provider_get(void*ctx,uint16_t ordinal){(void)ctx;persist_control_pattern_record_t*r=pattern_live_project_record_workspace();return(r!=NULL&&pattern_control_bank_get_ordinal_project(ordinal,r))?r:NULL;}
static uint8_t write_counter(uint8_t slot,uint32_t value){char x[48];FIL f;UINT n=0U;if(!count_path(x,sizeof(x),slot)||f_open(&f,x,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return 0U;uint8_t ok=(f_write(&f,&value,sizeof(value),&n)==FR_OK&&n==sizeof(value)&&f_sync(&f)==FR_OK);(void)f_close(&f);return ok;}
static uint32_t read_counter(uint8_t slot){char x[48];FIL f;UINT n=0U;uint32_t v=0U;if(count_path(x,sizeof(x),slot)&&f_open(&f,x,FA_READ)==FR_OK){(void)f_read(&f,&v,sizeof(v),&n);(void)f_close(&f);}return(n==sizeof(v))?v:0U;}
uint8_t project_product_save(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;persist_control_project_t*p=&g_workspace.unit.project;persist_codec_result_t r=persistent_project_control_capture(p);p->pattern_count=pattern_control_bank_count();char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=(r==PERSIST_CODEC_OK)&&path(x,sizeof(x),slot);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_pattern_provider_t provider={provider_get,NULL};persist_codec_sink_t sink=persistent_fatfs_sink(&f);ok=(persist_codec_encode_project(p,&provider,&sink,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}if(ok){++g_counter;ok=write_counter(slot,g_counter);}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);if(ok){g_present[slot]=1U;g_active=slot;g_active_valid=1U;(void)boot_context_flash_commit(slot);}return ok;}
static uint8_t apply_core(void*ctx,const persist_control_project_t*p){(void)ctx;if(!project_control_validate_assets(p->assets,p->asset_count)||!pattern_control_bank_begin_project())return 0U;persistent_project_apply_context_t c={0U,PERSIST_CODEC_OK};return persistent_project_control_apply_progressive_core(&c,p);}
static uint8_t put(void*ctx,const persist_control_pattern_record_t*r){(void)ctx;return pattern_control_bank_put_record_project(r);}
uint8_t project_product_load(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[slot]||!acquire())return 0U;char x[48];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot)&&persistent_fatfs_open_read(&f,x);g_progress=(project_product_progress_t){1U,0U,0U,1U};persist_codec_source_t source={0};persist_codec_result_t r=PERSIST_CODEC_IO_ERROR;if(ok){source=persistent_fatfs_source(&f);uint32_t bytes=0U;r=persist_codec_prevalidate_project(&source,&bytes);}uint32_t loaded_counter=read_counter(slot);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);if(ok&&r==PERSIST_CODEC_OK){persist_codec_project_applier_t a={apply_core,NULL};persist_codec_pattern_consumer_t c={put,pattern_control_bank_commit,pattern_control_bank_abort,NULL};r=persist_codec_decode_project_progressive(&source,&g_workspace,&a,&c);}ok=(ok&&r==PERSIST_CODEC_OK);if(source.context!=NULL)persistent_fatfs_close(&f);g_progress.done=1U;g_progress.complete=(ok&&multi_sample_load_has_pending()!=0U)?0U:1U;g_progress.active=(ok&&g_progress.complete==0U)?1U:0U;if(ok){g_active=slot;g_active_valid=1U;g_counter=loaded_counter;(void)boot_context_flash_commit(slot);}return ok;}
uint8_t project_product_delete(uint8_t slot){if(slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;char x[48],c[48];FRESULT r=FR_INVALID_NAME;if(path(x,sizeof(x),slot))r=f_unlink(x);if(count_path(c,sizeof(c),slot))(void)f_unlink(c);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);uint8_t ok=(r==FR_OK||r==FR_NO_FILE);if(ok){g_present[slot]=0U;if(g_active_valid&&g_active==slot)g_active_valid=0U;}return ok;}
uint8_t project_product_blank(void){persist_control_pattern_t*p=&g_workspace.unit.project.working_pattern;if(!pattern_live_get_control_boot(p)||!project_control_apply_assets(NULL,0U,p))return 0U;project_control_reset_macros();for(uint8_t b=0;b<16U;++b)for(uint8_t s=0;s<16U;++s)if(pattern_control_bank_present(b,s))(void)pattern_control_bank_delete(b,s);g_active_valid=0U;if(persistent_pattern_control_apply(p,0U)!=PERSIST_CODEC_OK)return 0U;pattern_live_set_active_state(0U,0U,0U,0U,0U);return 1U;}
uint8_t project_product_restore_boot(void){boot_context_flash_data_t c;if(!boot_context_flash_load(&c)||c.active_project_slot>=PROJECT_PRODUCT_SLOT_COUNT)return 0U;return project_product_load(c.active_project_slot);}
uint8_t project_product_get_progress(project_product_progress_t*out){if(out==NULL)return 0U;if(g_progress.active&&multi_sample_load_has_pending()==0U){g_progress.active=0U;g_progress.complete=1U;}*out=g_progress;return 1U;}
