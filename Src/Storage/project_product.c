#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/wav_convert.h"
#include "ControlRT/prepared_audio_state.h"
#include "Storage/audio_recorder.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/pattern_working_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_fatal.h"
#include "Storage/boot_context_sd.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_control.h"
#include "Storage/project_storage_paths.h"
#include "UI/ui_macro_interaction.h"
#include "Storage/persistence_debug.h"
#include "Storage/asset_ref.h"
#include "App/name_contract.h"
#include "Seq/seq_engine.h"
#include "Seq/seq_runtime.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_index.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sample_global_pool.h"
#include "Sampler/sample_page_cache_config.h"
#include "Sampler/sample_page_cache_port.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

_Static_assert((2U * SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
                    + MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
                   <= PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY,
               "Project Save asset snapshot capacity is too small");
_Static_assert(SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
                   == PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY,
               "Project Restore asset capacity must match the product pool");
_Static_assert(PROJECT_PRODUCT_NAME_BYTES == PERSIST_CODEC_PROJECT_NAME_BYTES,
               "Project name capacities must remain identical");

static uint8_t g_present[PROJECT_PRODUCT_SLOT_COUNT],g_active_valid,g_active;
static project_product_metadata_t g_project_metadata[PROJECT_PRODUCT_SLOT_COUNT];
static project_product_scan_status_t g_project_scan_status[PROJECT_PRODUCT_SLOT_COUNT];
static project_product_metadata_t g_current_metadata;
typedef struct {
    uint8_t state,slot,source_open,target_open,success;
    uint32_t epoch,remaining,old_crc,new_crc,new_total,old_expected;
    uint16_t old_name_length,new_name_length;
    char name[NAME_CONTRACT_BUFFER_BYTES];
    char final_path[48],temporary_path[48],backup_path[48];
    FIL source,target;
    uint8_t buffer[4096];
} project_rename_runtime_t;
STORAGE_STATE_SDRAM static project_rename_runtime_t g_project_rename;
static void project_product_rename_service(void);
static uint8_t project_unlink_optional(const char *path_value);
static project_product_progress_t g_progress;
static project_product_result_t g_project_prepare_result_hint;
static project_product_save_error_t g_save_error;
static int32_t g_save_detail;

static void project_capture_metadata(persist_codec_project_metadata_t *out)
{
    if (out == NULL) return;
    out->active_pattern_bank = 0U;
    out->active_pattern = 0U;
    (void)pattern_live_get_active(&out->active_pattern_bank,
                                  &out->active_pattern);
}

static uint8_t project_name_normalize(const char *name,char out[NAME_CONTRACT_BUFFER_BYTES])
{
    return(name_contract_normalize(name,out)==NAME_CONTRACT_RESULT_OK)?1U:0U;
}

static uint32_t project_le32(const uint8_t *bytes)
{
    return(uint32_t)bytes[0]|((uint32_t)bytes[1]<<8U)
        |((uint32_t)bytes[2]<<16U)|((uint32_t)bytes[3]<<24U);
}

typedef enum
{
    PROJECT_SAVE_IDLE = 0,
    PROJECT_SAVE_RECONCILE_BEGIN,
    PROJECT_SAVE_RECONCILE_WAIT,
    PROJECT_SAVE_MOUNT,
    PROJECT_SAVE_MKDIR_BRICK,
    PROJECT_SAVE_MKDIR_PROJECT,
    PROJECT_SAVE_MKDIR_PATTERNS,
    PROJECT_SAVE_RECOVER,
    PROJECT_SAVE_OPEN,
    PROJECT_SAVE_QUEUE_DOCUMENT_PLACEHOLDER,
    PROJECT_SAVE_ENCODE_CORE,
    PROJECT_SAVE_QUEUE_CORE,
    PROJECT_SAVE_ENCODE_ASSETS,
    PROJECT_SAVE_QUEUE_ASSETS,
    PROJECT_SAVE_ENCODE_MACROS,
    PROJECT_SAVE_QUEUE_MACROS,
    PROJECT_SAVE_WRITE,
    PROJECT_SAVE_CRC_SEEK,
    PROJECT_SAVE_CRC_READ,
    PROJECT_SAVE_WRITE_DOCUMENT_HEADER,
    PROJECT_SAVE_SYNC,
    PROJECT_SAVE_CLOSE,
    PROJECT_SAVE_COMMIT,
    PROJECT_SAVE_PATTERN_NEXT,
    PROJECT_SAVE_PATTERN_OPEN_SOURCE,
    PROJECT_SAVE_PATTERN_OPEN_TARGET,
    PROJECT_SAVE_PATTERN_COPY,
    PROJECT_SAVE_PATTERN_SYNC,
    PROJECT_SAVE_PATTERN_CLOSE,
    PROJECT_SAVE_PATTERN_STAGE_COMMIT,
    PROJECT_SAVE_MANIFEST_NEW,
    PROJECT_SAVE_PUBLISH_NEW,
    PROJECT_SAVE_MANIFEST_PREPARED,
    PROJECT_SAVE_PUBLISH_NEXT,
    PROJECT_SAVE_MANIFEST_COMMITTED,
    PROJECT_SAVE_CLEAN_NEXT,
    PROJECT_SAVE_CLEAN_DIRECTORY,
    PROJECT_SAVE_ROLLBACK_NEXT,
    PROJECT_SAVE_CLEAN_PROJECT_CLOSE,
    PROJECT_SAVE_CLEAN_TEMP,
    PROJECT_SAVE_RESUME_MANIFEST,
    PROJECT_SAVE_DONE
} project_save_state_t;

typedef struct
{
    project_save_state_t state;
    project_save_state_t after_write;
    persistence_project_save_workspace_t *workspace;
    uint8_t *encode_scratch;
    persistent_fatfs_file_t project_file;
    persistent_fatfs_file_t pattern_source;
    persistent_fatfs_file_t pattern_target;
    persist_codec_project_metadata_t metadata;
    const uint8_t *encoded_data;
    uint32_t encoded_size;
    const uint8_t *write_data;
    uint32_t write_size;
    uint32_t write_offset;
    uint32_t file_offset;
    uint32_t crc;
    uint32_t crc_remaining;
    uint32_t media_epoch;
    uint8_t slot;
    uint8_t project_open;
    uint8_t result_ready;
    uint8_t success;
    uint8_t new_project;
    uint8_t transaction_prepared;
    uint8_t transaction_committed;
    uint8_t pattern_source_open;
    uint8_t pattern_target_open;
    uint16_t transaction_index;
    uint8_t transaction_bank;
    uint8_t transaction_pattern;
    uint8_t source_project_valid;
    uint8_t source_project_slot;
    uint8_t resume_mode;
    uint8_t resume_slot;
    uint8_t resume_base_kind;
    uint8_t resume_active_bank;
    uint8_t resume_active_pattern;
    uint32_t resume_generation;
    uint32_t dirty[8];
    uint32_t patterns[8];
    uint32_t existed[8];
    uint8_t header[32];
    uint8_t copy_buffer[SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES];
    char requested_name[PROJECT_PRODUCT_NAME_BYTES + 1U];
    char final_path[80];
    char temporary_path[84];
    char backup_path[84];
    char project_directory[64];
    char transaction_directory[72];
    char manifest_path[84];
    char source_path[96];
    char target_path[96];
    char target_temporary_path[100];
    char target_backup_path[100];
} project_save_runtime_t;

typedef struct
{
    uint8_t *data;
    uint32_t capacity;
    uint32_t position;
} project_memory_io_t;

STORAGE_STATE_SDRAM static project_save_runtime_t g_project_save;

typedef enum
{
    PROJECT_LOAD_IDLE = 0,
    /* PREPARE: the live Project and ingress are still untouched. */
    PROJECT_LOAD_PREPARE_CANONICALIZE,
    /* T_FORWARD is crossed by project_load_quiesce_request() immediately
     * before entering this state. */
    PROJECT_LOAD_FORWARD_WAIT_SAFE,
    /* FORWARD INSTALL: the previous Project is no longer recoverable. */
    PROJECT_LOAD_INSTALL_BANK,
    PROJECT_LOAD_INSTALL_WAIT_MULTI,
    PROJECT_LOAD_INSTALL_ASSETS,
    PROJECT_LOAD_INSTALL_WAIT_STREAM,
    PROJECT_LOAD_INSTALL_WAIT_RAM,
    PROJECT_LOAD_INSTALL_WAIT_WAVETABLE,
    PROJECT_LOAD_INSTALL_CONTROL,
    /* Terminal safe state.  Ingress deliberately remains closed. */
    PROJECT_LOAD_FAILED_FORWARD_MEDIA
} project_load_state_t;

typedef struct
{
    project_load_state_t state;
    persistence_project_restore_workspace_t *restore;
    uint16_t asset_index;
    uint8_t slot;
    uint8_t forward_crossed;
    uint32_t media_epoch;
    uint8_t resume_mode;
    uint8_t resume_base_kind;
    uint32_t resume_dirty[8];
} project_load_runtime_t;

#define PROJECT_PRODUCT_NO_SLOT ((uint8_t)0xFFU)

STORAGE_STATE_SDRAM static project_load_runtime_t g_project_load;

static uint8_t path(char*out,uint32_t size,uint8_t slot){return project_storage_project_file(out,size,slot);}
static uint8_t side_path(char*out,uint32_t size,uint8_t slot,const char*extension){char base[80];if(!path(base,sizeof(base),slot))return 0U;int n=snprintf(out,size,"%s.%s",base,extension);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t project_side_path_base(char*out,uint32_t size,const char*base,
                                      const char*extension)
{int n=snprintf(out,size,"%s.%s",base,extension);return(n>0&&(uint32_t)n<size)?1U:0U;}

#define PROJECT_TX_MANIFEST_BYTES 72U
#define PROJECT_TX_PHASE_PREPARED 1U
#define PROJECT_TX_PHASE_COMMITTED 2U
#define PROJECT_TX_PHASE_CANDIDATE 3U
#define PROJECT_RESUME_MANIFEST_BYTES 64U
#define PROJECT_RESUME_VERSION 1U

static uint8_t project_tx_bit(const uint32_t words[8],uint16_t index)
{return(words[index>>5U]&(UINT32_C(1)<<(index&31U)))?1U:0U;}
static void project_tx_set(uint32_t words[8],uint16_t index,uint8_t value)
{const uint32_t mask=UINT32_C(1)<<(index&31U);if(value!=0U)words[index>>5U]|=mask;else words[index>>5U]&=~mask;}
static void project_tx_le32_write(uint8_t *out,uint32_t value)
{out[0]=(uint8_t)value;out[1]=(uint8_t)(value>>8U);out[2]=(uint8_t)(value>>16U);out[3]=(uint8_t)(value>>24U);}
static uint32_t project_resume_le32(const uint8_t *in)
{return(uint32_t)in[0]|((uint32_t)in[1]<<8U)|((uint32_t)in[2]<<16U)|((uint32_t)in[3]<<24U);}
static uint8_t project_resume_manifest_build(uint8_t slot,uint32_t generation,
    uint8_t base_kind,uint8_t base_slot,uint8_t active_bank,uint8_t active_pattern,
    const uint32_t dirty[8],uint32_t project_size,uint32_t project_crc,
    uint8_t out[PROJECT_RESUME_MANIFEST_BYTES])
{
    if(slot>1U||dirty==NULL||out==NULL)return 0U;
    memset(out,0,PROJECT_RESUME_MANIFEST_BYTES);memcpy(out,"B6RS",4U);
    out[4]=PROJECT_RESUME_VERSION;out[5]=base_kind;out[6]=base_slot;out[7]=slot;
    project_tx_le32_write(&out[8],generation);out[12]=active_bank;out[13]=active_pattern;
    for(uint8_t i=0U;i<8U;++i)project_tx_le32_write(&out[16U+4U*i],dirty[i]);
    project_tx_le32_write(&out[48],project_size);project_tx_le32_write(&out[52],project_crc);
    project_tx_le32_write(&out[60],~persist_codec_crc32_update(0xFFFFFFFFUL,out,60U));
    return 1U;
}

static uint8_t project_resume_manifest_read_mounted(uint8_t slot,uint8_t out[PROJECT_RESUME_MANIFEST_BYTES])
{
    char manifest[80],project_file[80];FIL file;UINT transferred=0U;FILINFO info;
    if(slot>1U||out==NULL||!project_storage_resume_manifest(manifest,sizeof(manifest),slot)
       ||!project_storage_resume_project_file(project_file,sizeof(project_file),slot))return 0U;
    if(f_open(&file,manifest,FA_READ)!=FR_OK)return 0U;
    FRESULT fr=f_read(&file,out,PROJECT_RESUME_MANIFEST_BYTES,&transferred);
    if(f_close(&file)!=FR_OK||fr!=FR_OK||transferred!=PROJECT_RESUME_MANIFEST_BYTES
       ||memcmp(out,"B6RS",4U)!=0||out[4]!=PROJECT_RESUME_VERSION||out[7]!=slot
       ||out[5]>PATTERN_WORKING_BASE_PROJECT||out[12]>=16U||out[13]>=16U
       ||project_resume_le32(&out[60])!=~persist_codec_crc32_update(0xFFFFFFFFUL,out,60U)
       ||f_stat(project_file,&info)!=FR_OK||info.fsize!=project_resume_le32(&out[48]))return 0U;
    if(out[5]==PATTERN_WORKING_BASE_PROJECT
       &&(out[6]>=PROJECT_PRODUCT_SLOT_COUNT||g_present[out[6]]==0U))return 0U;
    return 1U;
}

static uint8_t project_resume_latest_mounted(uint8_t manifest[PROJECT_RESUME_MANIFEST_BYTES])
{
    uint8_t a[PROJECT_RESUME_MANIFEST_BYTES],b[PROJECT_RESUME_MANIFEST_BYTES];
    const uint8_t va=project_resume_manifest_read_mounted(0U,a);
    const uint8_t vb=project_resume_manifest_read_mounted(1U,b);
    if(va==0U&&vb==0U)return 0U;
    const uint8_t *selected=(va!=0U&&(vb==0U||project_resume_le32(&a[8])>=project_resume_le32(&b[8])))?a:b;
    memcpy(manifest,selected,PROJECT_RESUME_MANIFEST_BYTES);return 1U;
}

static uint8_t project_resume_write_manifest_mounted(void)
{
    uint8_t bytes[PROJECT_RESUME_MANIFEST_BYTES];char final_path[84],temporary[88],backup[88];
    FIL file;UINT written=0U;uint8_t opened=0U;memset(&file,0,sizeof(file));
    if(!project_resume_manifest_build(g_project_save.resume_slot,
        g_project_save.resume_generation,g_project_save.resume_base_kind,
        g_project_save.source_project_slot,g_project_save.resume_active_bank,
        g_project_save.resume_active_pattern,g_project_save.dirty,
        g_project_save.file_offset,~g_project_save.crc,bytes)
       ||!project_storage_resume_manifest(final_path,sizeof(final_path),g_project_save.resume_slot)
       ||!project_side_path_base(temporary,sizeof(temporary),final_path,"TMP")
       ||!project_side_path_base(backup,sizeof(backup),final_path,"BAK"))return 0U;
    (void)f_unlink(temporary);
    FRESULT fr=f_open(&file,temporary,FA_CREATE_ALWAYS|FA_WRITE);
    if(fr==FR_OK)opened=1U;
    if(fr==FR_OK)fr=f_write(&file,bytes,sizeof(bytes),&written);
    if(fr==FR_OK&&written==sizeof(bytes))fr=f_sync(&file);
    if(opened!=0U&&f_close(&file)!=FR_OK&&fr==FR_OK)fr=FR_DISK_ERR;
    if(fr==FR_OK&&written==sizeof(bytes))fr=persistent_fatfs_commit_replace(final_path,temporary,backup);
    if(fr!=FR_OK)(void)f_unlink(temporary);
    return(fr==FR_OK)?1U:0U;
}
static uint8_t project_tx_build_manifest(uint8_t slot,uint8_t phase,
                                        const uint32_t dirty[8],
                                        const uint32_t existed[8],
                                        uint8_t out[PROJECT_TX_MANIFEST_BYTES])
{
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT||dirty==NULL||existed==NULL||out==NULL)return 0U;
    memset(out,0,PROJECT_TX_MANIFEST_BYTES);memcpy(out,"B6PT",4U);
    out[4]=1U;out[5]=phase;out[6]=slot;
    for(uint8_t i=0U;i<8U;++i){project_tx_le32_write(&out[8U+4U*i],dirty[i]);project_tx_le32_write(&out[40U+4U*i],existed[i]);}
    project_tx_le32_write(&out[68],~persist_codec_crc32_update(0xFFFFFFFFUL,out,68U));
    return 1U;
}

static uint8_t project_tx_write_manifest(const char *path_value,uint8_t slot,
                                         uint8_t phase,const uint32_t dirty[8],
                                         const uint32_t existed[8])
{
    uint8_t bytes[PROJECT_TX_MANIFEST_BYTES];char temporary[100],backup[100];
    persistent_fatfs_file_t file;UINT written=0U;
    if(!project_tx_build_manifest(slot,phase,dirty,existed,bytes)
        ||!project_side_path_base(temporary,sizeof(temporary),path_value,"TMP")
        ||!project_side_path_base(backup,sizeof(backup),path_value,"BAK"))return 0U;
    (void)persistent_fatfs_recover_replace(path_value,temporary,backup);
    if(persistent_fatfs_open_write_result(&file,temporary)!=FR_OK)return 0U;
    FRESULT result=f_write(&file.file,bytes,sizeof(bytes),&written);
    if(result==FR_OK&&written==sizeof(bytes))result=f_sync(&file.file);
    const FRESULT close_result=persistent_fatfs_close_result(&file);
    if(result==FR_OK)result=close_result;
    if(result==FR_OK)result=persistent_fatfs_commit_replace(path_value,temporary,backup);
    if(result!=FR_OK)(void)f_unlink(temporary);
    return(result==FR_OK)?1U:0U;
}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return 0U;}return 1U;}
static project_product_result_t project_prepare_acquire(void)
{
    if (sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT) == 0U)
        return PROJECT_PRODUCT_RESULT_NOT_NOW;
    if (sd_access_fs_mount_if_needed() == 0U)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        return PROJECT_PRODUCT_RESULT_MEDIA_ERROR;
    }
    return PROJECT_PRODUCT_RESULT_IN_PROGRESS;
}
static uint8_t project_mkdir_path(uint8_t (*build)(char *,uint32_t))
{
    char directory[64];
    if(build==NULL||build(directory,sizeof(directory))==0U)return 0U;
    const FRESULT result=f_mkdir(directory);
    return(result==FR_OK||result==FR_EXIST)?1U:0U;
}

static uint8_t ensure_directory(void)
{
    return project_mkdir_path(project_storage_projects_root);
}

static uint8_t project_remove_tree(const char *directory)
{
    DIR dir;FILINFO info;char child[128];
    FRESULT result=f_opendir(&dir,directory);
    if(result==FR_NO_PATH||result==FR_NO_FILE)return 1U;
    if(result!=FR_OK)return 0U;
    for(;;)
    {
        result=f_readdir(&dir,&info);
        if(result!=FR_OK){(void)f_closedir(&dir);return 0U;}
        if(info.fname[0]=='\0')break;
        if(strcmp(info.fname,".")==0||strcmp(info.fname,"..")==0)continue;
        const int length=snprintf(child,sizeof(child),"%s/%s",directory,info.fname);
        if(length<=0||(uint32_t)length>=sizeof(child))
        {(void)f_closedir(&dir);return 0U;}
        if((info.fattrib&AM_DIR)!=0U)
        {
            if(project_remove_tree(child)==0U){(void)f_closedir(&dir);return 0U;}
        }
        else if(f_unlink(child)!=FR_OK)
        {(void)f_closedir(&dir);return 0U;}
    }
    (void)f_closedir(&dir);result=f_unlink(directory);
    return(result==FR_OK||result==FR_NO_PATH||result==FR_NO_FILE)?1U:0U;
}

static void project_delete_recover_all(void)
{
    for(uint8_t slot=0U;slot<PROJECT_PRODUCT_SLOT_COUNT;++slot)
    {
        char directory[64];FILINFO info;
        if(project_storage_project_delete_transaction_dir(directory,
                sizeof(directory),slot)!=0U&&f_stat(directory,&info)==FR_OK)
            (void)project_remove_tree(directory);
    }
}

static uint8_t project_tx_read_manifest(uint8_t slot,uint8_t *phase,
                                        uint32_t dirty[8],uint32_t existed[8])
{
    char manifest[84];uint8_t bytes[PROJECT_TX_MANIFEST_BYTES];FIL file;
    UINT read=0U;
    if(!project_storage_project_transaction_manifest(manifest,sizeof(manifest),slot)
        )return 0U;
    char temporary[100],backup[100];
    if(!project_side_path_base(temporary,sizeof(temporary),manifest,"TMP")
        ||!project_side_path_base(backup,sizeof(backup),manifest,"BAK")
        ||persistent_fatfs_recover_replace(manifest,temporary,backup)!=FR_OK
        ||f_open(&file,manifest,FA_READ)!=FR_OK)return 0U;
    const FRESULT result=f_read(&file,bytes,sizeof(bytes),&read);
    (void)f_close(&file);
    if(result!=FR_OK||read!=sizeof(bytes)||memcmp(bytes,"B6PT",4U)!=0
        ||bytes[4]!=1U||bytes[6]!=slot
        ||(bytes[5]!=PROJECT_TX_PHASE_PREPARED
            &&bytes[5]!=PROJECT_TX_PHASE_COMMITTED
            &&bytes[5]!=PROJECT_TX_PHASE_CANDIDATE)
        ||project_le32(&bytes[68])!=
            ~persist_codec_crc32_update(0xFFFFFFFFUL,bytes,68U))return 0U;
    *phase=bytes[5];
    for(uint8_t i=0U;i<8U;++i){dirty[i]=project_le32(&bytes[8U+4U*i]);existed[i]=project_le32(&bytes[40U+4U*i]);}
    return 1U;
}

static void project_update_recover_all(void)
{
    for(uint8_t slot=0U;slot<PROJECT_PRODUCT_SLOT_COUNT;++slot)
    {
        uint8_t phase=0U;uint32_t dirty[8]={0},existed[8]={0};
        char transaction[72];
        if(!project_storage_project_transaction_dir(transaction,sizeof(transaction),slot)
            ||project_tx_read_manifest(slot,&phase,dirty,existed)==0U)continue;
        if(phase==PROJECT_TX_PHASE_CANDIDATE)continue;
        persist_debug_begin(PERSIST_DBG_OP_PROJECT_SAVE,0U,slot);
        persist_debug_project(PERSIST_DBG_PROJECT_PHASE_SAVE_CLEANUP,0U,phase);
        persist_debug_details(phase,slot,0U,0U);
        for(uint16_t item=0U;item<=256U;++item)
        {
            if(item!=0U&&project_tx_bit(dirty,(uint16_t)(item-1U))==0U)continue;
            char final_path[96],backup_path[96],staged_path[96];FILINFO info;
            uint8_t built=0U;
            if(item==0U)
                built=(uint8_t)(path(final_path,sizeof(final_path),slot)
                    &&project_storage_project_transaction_backup_file(backup_path,sizeof(backup_path),slot)
                    &&project_storage_project_transaction_file(staged_path,sizeof(staged_path),slot));
            else
            {const uint16_t index=(uint16_t)(item-1U);const uint8_t bank=(uint8_t)(index>>4U),pattern=(uint8_t)(index&15U);
                built=(uint8_t)(project_storage_pattern_file(final_path,sizeof(final_path),slot,bank,pattern)
                    &&project_storage_project_transaction_pattern_backup(backup_path,sizeof(backup_path),slot,bank,pattern)
                    &&project_storage_project_transaction_pattern_file(staged_path,sizeof(staged_path),slot,bank,pattern));}
            if(built==0U)continue;
            if(phase==PROJECT_TX_PHASE_PREPARED)
            {
                if(f_stat(backup_path,&info)==FR_OK)
                {(void)f_unlink(final_path);(void)f_rename(backup_path,final_path);}
                else if(item!=0U&&project_tx_bit(existed,(uint16_t)(item-1U))==0U)
                    (void)f_unlink(final_path);
            }
            else if(phase==PROJECT_TX_PHASE_COMMITTED)
            {
                (void)f_unlink(backup_path);
                if(item!=0U)
                {char working[96],temporary[100],working_backup[100];if(project_storage_working_pattern_file(working,sizeof(working),(uint8_t)((item-1U)>>4U),(uint8_t)((item-1U)&15U)))
                    {(void)f_unlink(working);if(project_side_path_base(temporary,sizeof(temporary),working,"TMP"))(void)f_unlink(temporary);if(project_side_path_base(working_backup,sizeof(working_backup),working,"BAK"))(void)f_unlink(working_backup);}}
            }
            (void)staged_path;
        }
        (void)project_remove_tree(transaction);
    }
}

static void project_creation_recover_all(void)
{
    for(uint8_t slot=0U;slot<PROJECT_PRODUCT_SLOT_COUNT;++slot)
    {
        char final_directory[48],candidate[72],project_file[80],patterns[80];
        FILINFO final_info,candidate_info,project_info,patterns_info;
        if(!project_storage_project_dir(final_directory,sizeof(final_directory),slot)
            ||!project_storage_project_transaction_dir(candidate,sizeof(candidate),slot)
            ||!project_storage_project_transaction_file(project_file,sizeof(project_file),slot)
            ||!project_storage_project_transaction_patterns_dir(patterns,sizeof(patterns),slot))continue;
        const uint8_t final_exists=(f_stat(final_directory,&final_info)==FR_OK)?1U:0U;
        const uint8_t candidate_exists=(f_stat(candidate,&candidate_info)==FR_OK)?1U:0U;
        if(candidate_exists==0U)continue;
        uint8_t phase=0U;uint32_t dirty[8],existed[8];
        const uint8_t candidate_complete=project_tx_read_manifest(slot,&phase,
            dirty,existed);
        if(final_exists==0U&&candidate_complete!=0U
            &&phase==PROJECT_TX_PHASE_CANDIDATE
            &&f_stat(project_file,&project_info)==FR_OK
            &&f_stat(patterns,&patterns_info)==FR_OK)
        {
            char manifest[84];
            if(project_storage_project_transaction_manifest(manifest,
                    sizeof(manifest),slot)
                &&project_unlink_optional(manifest)
                &&f_rename(candidate,final_directory)==FR_OK)continue;
        }
        (void)project_remove_tree(candidate);
    }
}

static project_product_scan_status_t project_product_scan_file(
    uint8_t slot,const char *file_path)
{
    g_project_metadata[slot].name[0]='\0';
    FIL file;UINT read=0U;uint8_t prefix[PERSIST_CODEC_HEADER_BYTES+10U+PERSIST_CODEC_PROJECT_NAME_BYTES];
    if(f_open(&file,file_path,FA_READ)!=FR_OK)return PROJECT_PRODUCT_SCAN_OPEN_FAILED;
    if(f_read(&file,prefix,sizeof(prefix),&read)!=FR_OK
        ||read<PERSIST_CODEC_HEADER_BYTES+10U)
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_HEADER_INVALID;}
    if(prefix[0]!='B'||prefix[1]!='6'||prefix[2]!='C'||prefix[3]!='P'
        ||prefix[5]!=0U||prefix[6]!=PERSIST_CODEC_DOCUMENT_PROJECT
        ||prefix[7]!=0U||prefix[8]!=3U||prefix[9]!=0U
        ||prefix[10]!=0U||prefix[11]!=0U||project_le32(&prefix[12])!=f_size(&file)
        ||prefix[24]!=0x01U||prefix[25]!=0x20U
        ||prefix[26]!=3U||prefix[27]!=0U)
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_HEADER_INVALID;}
    if((prefix[4]!=PERSIST_CODEC_VERSION)
        &&(prefix[4]!=PERSIST_CODEC_PREVIOUS_VERSION))
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_VERSION_INVALID;}
    if(project_le32(&prefix[20])!=
        ~persist_codec_crc32_update(0xFFFFFFFFUL,prefix,20U))
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_CRC_INVALID;}
    const uint16_t length=(uint16_t)((uint16_t)prefix[32]|((uint16_t)prefix[33]<<8U));
    if((length==0U)||(length>PERSIST_CODEC_PROJECT_NAME_BYTES)
            ||(read<(UINT)(PERSIST_CODEC_HEADER_BYTES+10U+length)))
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_METADATA_INVALID;}
    char candidate[NAME_CONTRACT_BUFFER_BYTES]={0};char normalized[NAME_CONTRACT_BUFFER_BYTES];
    memcpy(candidate,&prefix[34],length);
    if(!project_name_normalize(candidate,normalized)||(strlen(normalized)!=length)
            ||memcmp(candidate,normalized,length)!=0)
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_METADATA_INVALID;}
    uint8_t buffer[256];uint32_t crc=0xFFFFFFFFUL;
    if(f_lseek(&file,PERSIST_CODEC_HEADER_BYTES)!=FR_OK)
    {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_OPEN_FAILED;}
    uint32_t remaining=(uint32_t)f_size(&file)-PERSIST_CODEC_HEADER_BYTES;
    while(remaining!=0U)
    {
        const UINT chunk=(remaining>sizeof(buffer))?(UINT)sizeof(buffer):(UINT)remaining;
        if(f_read(&file,buffer,chunk,&read)!=FR_OK||read!=chunk)
        {(void)f_close(&file);return PROJECT_PRODUCT_SCAN_OPEN_FAILED;}
        crc=persist_codec_crc32_update(crc,buffer,chunk);remaining-=chunk;
    }
    (void)f_close(&file);
    if(project_le32(&prefix[16])!=~crc)return PROJECT_PRODUCT_SCAN_CRC_INVALID;
    memcpy(g_project_metadata[slot].name,normalized,sizeof(g_project_metadata[slot].name));
    return PROJECT_PRODUCT_SCAN_ACCEPTED;
}

void project_product_refresh_slots(void){if((project_replacement_is_active()!=0U&&g_project_load.state!=PROJECT_LOAD_FAILED_FORWARD_MEDIA)||project_product_save_busy()!=0U||project_product_load_busy()!=0U||project_product_rename_busy()!=0U)return;memset(g_present,0,sizeof(g_present));memset(g_project_metadata,0,sizeof(g_project_metadata));memset(g_project_scan_status,0,sizeof(g_project_scan_status));if(!acquire())return;if(!ensure_directory()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return;}(void)project_mkdir_path(project_storage_internal_root);(void)project_mkdir_path(project_storage_transactions_root);(void)project_mkdir_path(project_storage_project_transaction_root);project_delete_recover_all();project_update_recover_all();project_creation_recover_all();for(uint8_t s=0U;s<PROJECT_PRODUCT_SLOT_COUNT;++s){char d[48],x[48],p[64],tmp[48],bak[48];FILINFO i;if(!project_storage_project_dir(d,sizeof(d),s)||!path(x,sizeof(x),s)||!project_storage_patterns_dir(p,sizeof(p),s)||!side_path(tmp,sizeof(tmp),s,"TMP")||!side_path(bak,sizeof(bak),s,"BAK"))continue;if(f_stat(d,&i)!=FR_OK||(i.fattrib&AM_DIR)==0U){g_project_scan_status[s]=PROJECT_PRODUCT_SCAN_DIRECTORY_ABSENT;continue;}(void)persistent_fatfs_recover_replace(x,tmp,bak);if(f_stat(x,&i)!=FR_OK||(i.fattrib&AM_DIR)!=0U){g_project_scan_status[s]=PROJECT_PRODUCT_SCAN_FILE_ABSENT;continue;}if(f_stat(p,&i)!=FR_OK||(i.fattrib&AM_DIR)==0U){g_project_scan_status[s]=PROJECT_PRODUCT_SCAN_PATTERNS_ABSENT;continue;}g_project_scan_status[s]=project_product_scan_file(s,x);if(g_project_scan_status[s]==PROJECT_PRODUCT_SCAN_ACCEPTED)g_present[s]=1U;}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
void project_product_init(void){memset(&g_progress,0,sizeof(g_progress));memset(&g_project_save,0,sizeof(g_project_save));memset(&g_project_load,0,sizeof(g_project_load));memset(&g_project_rename,0,sizeof(g_project_rename));memset(g_project_metadata,0,sizeof(g_project_metadata));memset(&g_current_metadata,0,sizeof(g_current_metadata));g_active_valid=0U;g_active=0U;project_product_refresh_slots();}
uint8_t project_product_list_slots(uint8_t*out,uint8_t cap){uint8_t n=0U;if(out==NULL)return 0U;for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT&&n<cap;++s)if(g_present[s]&&g_project_metadata[s].name[0]!='\0')out[n++]=s;return n;}
uint8_t project_product_metadata(uint8_t s,project_product_metadata_t*out){if(out==NULL||s>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[s])return 0U;*out=g_project_metadata[s];return 1U;}
project_product_scan_status_t project_product_scan_status(uint8_t s){return(s<PROJECT_PRODUCT_SLOT_COUNT)?g_project_scan_status[s]:PROJECT_PRODUCT_SCAN_SLOT_ABSENT;}
uint8_t project_product_current_metadata(project_product_metadata_t*out){if(out==NULL||g_active_valid==0U)return 0U;*out=g_current_metadata;return 1U;}

static uint8_t project_memory_write(void *context,const uint8_t *data,uint32_t length)
{
    project_memory_io_t *const memory=context;
    if(memory==NULL||data==NULL||memory->position>memory->capacity
            ||length>memory->capacity-memory->position)return 0U;
    memcpy(&memory->data[memory->position],data,length);memory->position+=length;return 1U;
}


static sd_scheduler_background_admission_t project_save_admit(
    sd_scheduler_background_kind_t kind,uint32_t bytes)
{
    const sd_scheduler_background_request_t request={bytes,g_project_save.media_epoch,kind};
    return sd_scheduler_runtime_background_try_begin(&request);
}

static void project_save_set_active_identity(void)
{
    g_present[g_project_save.slot]=1U;
    memset(&g_current_metadata,0,sizeof(g_current_metadata));
    memcpy(g_current_metadata.name,g_project_save.metadata.name,
           g_project_save.metadata.name_length);
    g_project_metadata[g_project_save.slot]=g_current_metadata;
    g_active=g_project_save.slot;
    g_active_valid=1U;
}

static void project_save_finish(uint8_t success)
{
    if(g_project_save.workspace!=NULL)
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);
    g_project_save.workspace=NULL;g_project_save.encode_scratch=NULL;g_project_save.project_open=0U;
    g_project_save.success=(success!=0U)?1U:0U;g_project_save.result_ready=1U;
    g_project_save.state=PROJECT_SAVE_DONE;g_progress.active=0U;g_progress.complete=1U;g_progress.result=(success!=0U)?PROJECT_PRODUCT_RESULT_SUCCESS:PROJECT_PRODUCT_RESULT_FAILED;
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_DONE,g_progress.done,
                          (uint32_t)g_progress.result);
    if(success!=0U)persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS,0);
    else persist_debug_error(PERSIST_DBG_STAGE_FAIL,(g_save_error!=PROJECT_PRODUCT_SAVE_ERROR_NONE)?(int32_t)g_save_error:PERSIST_DBG_ERROR_INTERNAL);
    if(success!=0U&&g_project_save.resume_mode==0U){project_save_set_active_identity();(void)boot_context_sd_commit(g_project_save.slot);}
}

static uint8_t project_save_pattern_paths(uint8_t bank,uint8_t pattern)
{
    const uint16_t index=(uint16_t)bank*16U+pattern;
    const uint8_t source_ok=(project_tx_bit(g_project_save.dirty,index)!=0U)
        ?project_storage_working_pattern_file(g_project_save.source_path,
            sizeof(g_project_save.source_path),bank,pattern)
        :(uint8_t)(g_project_save.source_project_valid!=0U
            &&project_storage_pattern_file(g_project_save.source_path,
                sizeof(g_project_save.source_path),
                g_project_save.source_project_slot,bank,pattern));
    return(uint8_t)(source_ok
        &&project_storage_project_transaction_pattern_file(
            g_project_save.target_path,sizeof(g_project_save.target_path),
            g_project_save.slot,bank,pattern)
        &&project_side_path_base(g_project_save.target_temporary_path,
            sizeof(g_project_save.target_temporary_path),
            g_project_save.target_path,"TMP")
        &&project_side_path_base(g_project_save.target_backup_path,
            sizeof(g_project_save.target_backup_path),
            g_project_save.target_path,"BAK"));
}

static uint8_t project_save_publish_paths(uint16_t item,char *final_path,
                                          uint32_t final_capacity,
                                          char *staged_path,
                                          uint32_t staged_capacity,
                                          char *backup_path,
                                          uint32_t backup_capacity)
{
    if(item==0U)
        return(uint8_t)(path(final_path,final_capacity,g_project_save.slot)
            &&project_storage_project_transaction_file(staged_path,
                staged_capacity,g_project_save.slot)
            &&project_storage_project_transaction_backup_file(backup_path,
                backup_capacity,g_project_save.slot));
    const uint16_t index=(uint16_t)(item-1U);
    const uint8_t bank=(uint8_t)(index>>4U),pattern=(uint8_t)(index&15U);
    return(uint8_t)(project_storage_pattern_file(final_path,final_capacity,
                g_project_save.slot,bank,pattern)
        &&project_storage_project_transaction_pattern_file(staged_path,
                staged_capacity,g_project_save.slot,bank,pattern)
        &&project_storage_project_transaction_pattern_backup(backup_path,
                backup_capacity,g_project_save.slot,bank,pattern));
}

static uint8_t project_unlink_optional(const char *path_value)
{
    const FRESULT result=f_unlink(path_value);
    return(result==FR_OK||result==FR_NO_FILE||result==FR_NO_PATH)?1U:0U;
}

static void project_save_fail(project_product_save_error_t error,int32_t detail)
{
    if(g_save_error==PROJECT_PRODUCT_SAVE_ERROR_NONE){g_save_error=error;g_save_detail=detail;}
    persist_debug_details((uint32_t)error,(uint32_t)detail,g_project_save.file_offset,g_project_save.encoded_size);
    persist_debug_error(g_project_save.project_open?PERSIST_DBG_STAGE_WRITE:PERSIST_DBG_STAGE_OPEN,(int32_t)error);
    g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_CLEANUP;
    if(g_project_save.pattern_source_open!=0U)
    {(void)persistent_fatfs_close_result(&g_project_save.pattern_source);g_project_save.pattern_source_open=0U;}
    if(g_project_save.pattern_target_open!=0U)
    {(void)persistent_fatfs_close_result(&g_project_save.pattern_target);g_project_save.pattern_target_open=0U;}
    if(g_project_save.transaction_prepared!=0U
            &&g_project_save.transaction_committed==0U)
    {g_project_save.transaction_index=0U;g_project_save.state=PROJECT_SAVE_ROLLBACK_NEXT;}
    else if(g_project_save.project_open!=0U)g_project_save.state=PROJECT_SAVE_CLEAN_PROJECT_CLOSE;
    else g_project_save.state=PROJECT_SAVE_CLEAN_TEMP;
}

static uint8_t project_save_abort_now(project_product_save_error_t error,
                                      int32_t detail)
{
    if(g_save_error==PROJECT_PRODUCT_SAVE_ERROR_NONE)
    {
        g_save_error=error;
        g_save_detail=detail;
    }
    const uint8_t gate_acquired=sd_access_gate_try_acquire(
        SD_ACCESS_CLIENT_BACKGROUND);
    if(gate_acquired==0U)return 0U;
    if(g_project_save.project_open!=0U)
    {
        (void)persistent_fatfs_close_result(&g_project_save.project_file);
    }
    g_project_save.project_open=0U;
    if(g_project_save.pattern_source_open!=0U)
        (void)persistent_fatfs_close_result(&g_project_save.pattern_source);
    if(g_project_save.pattern_target_open!=0U)
        (void)persistent_fatfs_close_result(&g_project_save.pattern_target);
    g_project_save.pattern_source_open=0U;
    g_project_save.pattern_target_open=0U;
    if(g_project_save.transaction_prepared==0U)
    {
        (void)f_unlink(g_project_save.temporary_path);
        (void)project_remove_tree(g_project_save.transaction_directory);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_BACKGROUND);
    project_save_finish(0U);
    return 1U;
}

static void project_save_queue_write(const uint8_t *data,uint32_t size,project_save_state_t next)
{
    g_project_save.write_data=data;g_project_save.write_size=size;g_project_save.write_offset=0U;
    g_project_save.after_write=next;g_project_save.state=PROJECT_SAVE_WRITE;
}

static uint8_t project_save_encode_core(void)
{
    g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_ENCODE;
    persist_debug_stage(PERSIST_DBG_STAGE_ENCODE,0);
    if(g_project_save.encode_scratch==NULL){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY,0);return 0U;}
    project_memory_io_t memory={g_project_save.encode_scratch,
        sizeof(g_project_save.workspace->encode_scratch),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_core_payload(
        &g_project_save.metadata,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_CORE,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=g_project_save.encode_scratch;
    g_project_save.encoded_size=bytes;return 1U;
}

static uint8_t project_save_encode_assets(void)
{
    g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_ENCODE;
    persist_debug_stage(PERSIST_DBG_STAGE_ENCODE,0);
    if(g_project_save.encode_scratch==NULL){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY,0);return 0U;}
    project_memory_io_t memory={g_project_save.encode_scratch,
        sizeof(g_project_save.workspace->encode_scratch),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_assets_payload(
        g_project_save.workspace->assets,g_project_save.metadata.asset_count,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_ASSETS,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=g_project_save.encode_scratch;
    g_project_save.encoded_size=bytes;return 1U;
}

static uint8_t project_save_encode_macros(void)
{
    g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_ENCODE;
    persist_debug_stage(PERSIST_DBG_STAGE_ENCODE,0);
    if(g_project_save.encode_scratch==NULL){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY,0);return 0U;}
    project_memory_io_t memory={g_project_save.encode_scratch,
        sizeof(g_project_save.workspace->encode_scratch),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_macros_payload(
        &g_project_save.workspace->macros,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_MACROS,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=g_project_save.encode_scratch;
    g_project_save.encoded_size=bytes;return 1U;
}

static uint8_t project_save_capture_current(void)
{
    persistence_project_save_workspace_t *const workspace=
        persistence_workspace_acquire_project_save();
    if(workspace==NULL){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY;g_save_detail=0;return 0U;}
    g_project_save.workspace=workspace;
    g_project_save.encode_scratch=workspace->encode_scratch;
    project_capture_metadata(&g_project_save.metadata);
    g_project_save.metadata.name_length=(uint16_t)strlen(g_project_save.requested_name);
    memcpy(g_project_save.metadata.name,g_project_save.requested_name,
           g_project_save.metadata.name_length);
    g_project_save.metadata.asset_count=project_control_asset_count();
    if(g_project_save.metadata.asset_count>PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY
            ||!project_control_capture_macros(&workspace->macros))
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;g_save_detail=0;goto capture_fail;}
    for(uint16_t i=0U;i<g_project_save.metadata.asset_count;++i)
        if(!project_control_get_asset_ordinal(i,&workspace->assets[i]))
        {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;g_save_detail=(int32_t)i;goto capture_fail;}
    pattern_working_bank_copy_dirty(g_project_save.dirty);
    memcpy(g_project_save.patterns,g_project_save.dirty,
           sizeof(g_project_save.patterns));
    if(g_project_save.new_project!=0U&&g_project_save.source_project_valid!=0U)
        for(uint16_t index=0U;index<256U;++index)
            if(pattern_control_bank_present((uint8_t)(index>>4U),
                                            (uint8_t)(index&15U))!=0U)
                project_tx_set(g_project_save.patterns,index,1U);
    return 1U;
capture_fail:
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);
    g_project_save.workspace=NULL;g_project_save.encode_scratch=NULL;
    return 0U;
}

uint8_t project_product_save_named(uint8_t slot,const char *name)
{
    persist_debug_begin(PERSIST_DBG_OP_PROJECT_SAVE,0U,slot);
    persist_debug_stage(PERSIST_DBG_STAGE_POLICY,0);
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_SAVE_SNAPSHOT,0U,0U);
    g_save_error=PROJECT_PRODUCT_SAVE_ERROR_NONE;g_save_detail=0;
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT;persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);return 0U;}
    if(g_present[slot]!=0U&&(g_active_valid==0U||g_active!=slot))
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT;persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);return 0U;}
    if(seq_runtime_is_running()!=0U||seq_runtime_is_start_pending()!=0U){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_TRANSPORT_ACTIVE;persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);return 0U;}
    char normalized[NAME_CONTRACT_BUFFER_BYTES];if(!project_name_normalize(name,normalized)){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_INVALID_NAME;persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);return 0U;}
    if(project_product_rename_busy()!=0U || project_replacement_is_active()!=0U
        || project_product_save_busy()!=0U||project_product_load_busy()!=0U
        || pattern_load_is_pending()!=0U)
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY;persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);return 0U;}
    memset(&g_project_save,0,sizeof(g_project_save));g_project_save.slot=slot;
    g_project_save.new_project=(g_present[slot]==0U)?1U:0U;
    g_project_save.source_project_valid=
        pattern_control_bank_active_project(&g_project_save.source_project_slot);
    memcpy(g_project_save.requested_name,normalized,strlen(normalized)+1U);
    if(!project_storage_project_dir(g_project_save.project_directory,
            sizeof(g_project_save.project_directory),slot)
        ||!project_storage_project_transaction_dir(
                    g_project_save.transaction_directory,
                    sizeof(g_project_save.transaction_directory),slot)
        ||!project_storage_project_transaction_file(
                    g_project_save.final_path,sizeof(g_project_save.final_path),slot)
        ||!project_storage_project_transaction_manifest(
                    g_project_save.manifest_path,sizeof(g_project_save.manifest_path),slot)
        ||!project_side_path_base(g_project_save.temporary_path,
                    sizeof(g_project_save.temporary_path),g_project_save.final_path,"TMP")
        ||!project_side_path_base(g_project_save.backup_path,
                    sizeof(g_project_save.backup_path),g_project_save.final_path,"BAK"))
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT;memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    g_project_save.media_epoch=sd_access_media_epoch();
    g_project_save.state=PROJECT_SAVE_RECONCILE_BEGIN;
    persist_debug_details(0U,g_project_save.metadata.asset_count,
        g_project_save.media_epoch,0U);
    g_progress=(project_product_progress_t){.active=1U,.total=1U,
        .result=PROJECT_PRODUCT_RESULT_IN_PROGRESS};return 1U;
}

uint8_t project_product_save_as_named(const char *name,uint8_t *out_slot)
{
    if(out_slot==NULL)return 0U;
    for(uint8_t slot=0U;slot<PROJECT_PRODUCT_SLOT_COUNT;++slot)
        if(g_present[slot]==0U
            &&g_project_scan_status[slot]==PROJECT_PRODUCT_SCAN_DIRECTORY_ABSENT)
        {
            if(project_product_save_named(slot,name)==0U)return 0U;
            *out_slot=slot;
            return 1U;
        }
    return 0U;
}

uint8_t project_product_save_existing(uint8_t slot)
{
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT||g_present[slot]==0U
        ||g_project_metadata[slot].name[0]=='\0'||g_active_valid==0U
        ||g_active!=slot)return 0U;
    return project_product_save_named(slot,g_project_metadata[slot].name);
}

uint8_t project_product_save_busy(void){return(g_project_save.state!=PROJECT_SAVE_IDLE)?1U:0U;}

uint8_t project_product_save_take_result(uint8_t *slot,uint8_t *success)
{
    if(g_project_save.state!=PROJECT_SAVE_DONE||g_project_save.result_ready==0U
       ||g_project_save.resume_mode!=0U)return 0U;
    const uint8_t recovery_required=(uint8_t)(g_project_save.success==0U
        &&g_project_save.transaction_prepared!=0U);
    if(slot!=NULL)*slot=g_project_save.slot;
    if(success!=NULL)*success=g_project_save.success;
    memset(&g_project_save,0,sizeof(g_project_save));
    if(recovery_required!=0U)project_product_refresh_slots();
    return 1U;
}

uint8_t project_product_resume_save_begin(void)
{
    if(project_product_save_busy()!=0U||project_product_load_busy()!=0U
       ||project_product_rename_busy()!=0U||pattern_load_is_pending()!=0U
       ||seq_runtime_is_running()!=0U||seq_runtime_is_start_pending()!=0U)return 0U;
    if(project_load_allowed()==0U)return 0U;
    uint8_t previous[PROJECT_RESUME_MANIFEST_BYTES]={0};
    if(!acquire())return 0U;
    const uint8_t previous_valid=project_resume_latest_mounted(previous);
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    memset(&g_project_save,0,sizeof(g_project_save));
    g_project_save.resume_mode=1U;
    g_project_save.resume_slot=(previous_valid!=0U)?(uint8_t)(previous[7]^1U):0U;
    g_project_save.resume_generation=(previous_valid!=0U)
        ?project_resume_le32(&previous[8])+1U:1U;
    g_project_save.resume_base_kind=(uint8_t)pattern_working_bank_base_kind();
    g_project_save.source_project_slot=0xFFU;
    g_project_save.source_project_valid=pattern_control_bank_active_project(
        &g_project_save.source_project_slot);
    if(g_project_save.resume_base_kind==PATTERN_WORKING_BASE_PROJECT
       &&g_project_save.source_project_valid==0U){memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    (void)pattern_live_get_active(&g_project_save.resume_active_bank,
                                  &g_project_save.resume_active_pattern);
    if(g_active_valid!=0U)memcpy(g_project_save.requested_name,
        g_current_metadata.name,strlen(g_current_metadata.name)+1U);
    if(!project_storage_resume_root(g_project_save.project_directory,
            sizeof(g_project_save.project_directory))
       ||!project_storage_resume_slot_dir(g_project_save.transaction_directory,
            sizeof(g_project_save.transaction_directory),g_project_save.resume_slot)
       ||!project_storage_resume_project_file(g_project_save.final_path,
            sizeof(g_project_save.final_path),g_project_save.resume_slot)
       ||!project_side_path_base(g_project_save.temporary_path,
            sizeof(g_project_save.temporary_path),g_project_save.final_path,"TMP")
       ||!project_side_path_base(g_project_save.backup_path,
            sizeof(g_project_save.backup_path),g_project_save.final_path,"BAK"))
    {memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    g_project_save.media_epoch=sd_access_media_epoch();
    g_project_save.state=PROJECT_SAVE_RECONCILE_BEGIN;
    g_progress=(project_product_progress_t){.active=1U,.total=1U,
        .result=PROJECT_PRODUCT_RESULT_IN_PROGRESS};
    return 1U;
}

uint8_t project_product_resume_save_take_result(uint8_t *success)
{
    if(g_project_save.state!=PROJECT_SAVE_DONE||g_project_save.result_ready==0U
       ||g_project_save.resume_mode==0U)return 0U;
    if(success!=NULL)*success=g_project_save.success;
    memset(&g_project_save,0,sizeof(g_project_save));
    return 1U;
}

uint8_t project_product_rename_busy(void)
{
    return (g_project_rename.state != 0U && g_project_rename.state != 4U) ? 1U : 0U;
}

uint8_t project_product_rename(uint8_t slot,const char *name)
{
    char normalized[NAME_CONTRACT_BUFFER_BYTES];
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT || g_present[slot]==0U
        || g_project_metadata[slot].name[0]=='\0'
        || project_product_rename_busy()!=0U || project_product_save_busy()!=0U
        || project_product_load_busy()!=0U || project_replacement_is_active()!=0U
        || project_name_normalize(name,normalized)==0U) return 0U;
    memset(&g_project_rename,0,sizeof(g_project_rename));
    g_project_rename.slot=slot;
    g_project_rename.epoch=sd_access_media_epoch();
    memcpy(g_project_rename.name,normalized,sizeof(g_project_rename.name));
    g_project_rename.new_name_length=(uint16_t)strlen(normalized);
    if(!path(g_project_rename.final_path,sizeof(g_project_rename.final_path),slot)
        || !side_path(g_project_rename.temporary_path,sizeof(g_project_rename.temporary_path),slot,"TMP")
        || !side_path(g_project_rename.backup_path,sizeof(g_project_rename.backup_path),slot,"BAK"))return 0U;
    g_project_rename.state=1U;
    return 1U;
}

uint8_t project_product_rename_take_result(uint8_t *slot,uint8_t *success)
{
    if(g_project_rename.state!=4U)return 0U;
    if(slot!=NULL)*slot=g_project_rename.slot;
    if(success!=NULL)*success=g_project_rename.success;
    memset(&g_project_rename,0,sizeof(g_project_rename));
    return 1U;
}

static uint8_t project_rename_write(FIL *file,const uint8_t *data,UINT size)
{
    UINT written=0U;
    return (f_write(file,data,size,&written)==FR_OK && written==size)?1U:0U;
}

static void project_rename_finish(uint8_t success)
{
    if(g_project_rename.source_open!=0U)
    { (void)f_close(&g_project_rename.source);g_project_rename.source_open=0U; }
    if(g_project_rename.target_open!=0U)
    { (void)f_close(&g_project_rename.target);g_project_rename.target_open=0U; }
    if(success==0U) (void)f_unlink(g_project_rename.temporary_path);
    else {
        memset(&g_project_metadata[g_project_rename.slot],0,
               sizeof(g_project_metadata[g_project_rename.slot]));
        memcpy(g_project_metadata[g_project_rename.slot].name,
               g_project_rename.name,g_project_rename.new_name_length);
        if(g_active_valid!=0U && g_active==g_project_rename.slot)
            g_current_metadata=g_project_metadata[g_project_rename.slot];
    }
    g_project_rename.success=success;
    g_project_rename.state=4U;
}

static void project_product_rename_service(void)
{
    if(project_product_rename_busy()==0U)return;
    const sd_scheduler_background_request_t request={
        (g_project_rename.state==2U)?sizeof(g_project_rename.buffer):0U,
        g_project_rename.epoch,
        (g_project_rename.state==2U)?SD_SCHEDULER_BACKGROUND_DATA:SD_SCHEDULER_BACKGROUND_METADATA};
    const sd_scheduler_background_admission_t admission=
        sd_scheduler_runtime_background_try_begin(&request);
    if(admission==SD_SCHEDULER_BACKGROUND_NOT_NOW)return;
    if(admission!=SD_SCHEDULER_BACKGROUND_GO)
    {project_rename_finish(0U);return;}
    uint8_t ok=1U;
    UINT read=0U;
    if(g_project_rename.state==1U)
    {
        uint8_t prefix[34U],old_name[PROJECT_PRODUCT_NAME_BYTES];
        uint32_t source_size=0U,core_length=0U;
        if(persistent_fatfs_recover_replace(g_project_rename.final_path,
                g_project_rename.temporary_path,g_project_rename.backup_path)!=FR_OK
            || f_open(&g_project_rename.source,g_project_rename.final_path,FA_READ)!=FR_OK)
            ok=0U;
        else g_project_rename.source_open=1U;
        if(ok!=0U)
        {
            source_size=(uint32_t)f_size(&g_project_rename.source);
            if(f_read(&g_project_rename.source,prefix,sizeof(prefix),&read)!=FR_OK
                ||read!=sizeof(prefix))ok=0U;
        }
        if(ok!=0U)
        {
            core_length=project_le32(&prefix[28]);
            g_project_rename.old_name_length=(uint16_t)prefix[32]
                |((uint16_t)prefix[33]<<8U);
            g_project_rename.old_expected=project_le32(&prefix[16]);
            if(memcmp(prefix,"B6PC",4U)!=0
                ||(prefix[4]!=PERSIST_CODEC_VERSION
                    &&prefix[4]!=PERSIST_CODEC_PREVIOUS_VERSION)
                ||prefix[6]!=PERSIST_CODEC_DOCUMENT_PROJECT
                ||prefix[8]!=3U ||prefix[24]!=1U ||prefix[25]!=0x20U
                ||prefix[26]!=3U ||prefix[27]!=0U
                ||project_le32(&prefix[20])!=
                    ~persist_codec_crc32_update(0xFFFFFFFFUL,prefix,20U)
                ||project_le32(&prefix[12])!=source_size
                ||g_project_rename.old_name_length==0U
                ||g_project_rename.old_name_length>PROJECT_PRODUCT_NAME_BYTES
                ||core_length<2U+g_project_rename.old_name_length
                ||source_size<34U+g_project_rename.old_name_length)ok=0U;
        }
        if(ok!=0U && (f_read(&g_project_rename.source,old_name,
                g_project_rename.old_name_length,&read)!=FR_OK
                ||read!=g_project_rename.old_name_length))ok=0U;
        if(ok!=0U)
        {
            const int32_t delta=(int32_t)g_project_rename.new_name_length
                -(int32_t)g_project_rename.old_name_length;
            g_project_rename.new_total=(uint32_t)((int32_t)source_size+delta);
            if(g_project_rename.new_total>PERSIST_CODEC_PROJECT_DOCUMENT_MAX_BYTES
                ||f_open(&g_project_rename.target,g_project_rename.temporary_path,
                    FA_CREATE_ALWAYS|FA_WRITE|FA_READ)!=FR_OK)ok=0U;
            else g_project_rename.target_open=1U;
        }
        if(ok!=0U)
        {
            uint8_t new_prefix[10U];
            memcpy(new_prefix,&prefix[24],sizeof(new_prefix));
            core_length=(uint32_t)((int32_t)core_length
                +(int32_t)g_project_rename.new_name_length
                -(int32_t)g_project_rename.old_name_length);
            for(uint8_t i=0U;i<4U;++i)new_prefix[4U+i]=(uint8_t)(core_length>>(8U*i));
            new_prefix[8]=(uint8_t)g_project_rename.new_name_length;
            new_prefix[9]=0U;
            g_project_rename.old_crc=persist_codec_crc32_update(0xFFFFFFFFUL,
                &prefix[24],10U);
            g_project_rename.old_crc=persist_codec_crc32_update(
                g_project_rename.old_crc,old_name,g_project_rename.old_name_length);
            g_project_rename.new_crc=persist_codec_crc32_update(0xFFFFFFFFUL,
                new_prefix,sizeof(new_prefix));
            g_project_rename.new_crc=persist_codec_crc32_update(
                g_project_rename.new_crc,(const uint8_t*)g_project_rename.name,
                g_project_rename.new_name_length);
            memset(prefix,0,PERSIST_CODEC_HEADER_BYTES);
            if(project_rename_write(&g_project_rename.target,prefix,PERSIST_CODEC_HEADER_BYTES)==0U
                ||project_rename_write(&g_project_rename.target,new_prefix,sizeof(new_prefix))==0U
                ||project_rename_write(&g_project_rename.target,
                    (const uint8_t*)g_project_rename.name,g_project_rename.new_name_length)==0U)
                ok=0U;
            g_project_rename.remaining=source_size-34U-g_project_rename.old_name_length;
            if(ok!=0U)g_project_rename.state=2U;
        }
    }
    else if(g_project_rename.state==2U)
    {
        UINT chunk=(g_project_rename.remaining>sizeof(g_project_rename.buffer))
            ?sizeof(g_project_rename.buffer):(UINT)g_project_rename.remaining;
        if(chunk!=0U)
        {
            if(f_read(&g_project_rename.source,g_project_rename.buffer,chunk,&read)!=FR_OK
                ||read!=chunk ||project_rename_write(&g_project_rename.target,
                    g_project_rename.buffer,chunk)==0U)ok=0U;
            if(ok!=0U)
            {
                g_project_rename.old_crc=persist_codec_crc32_update(
                    g_project_rename.old_crc,g_project_rename.buffer,chunk);
                g_project_rename.new_crc=persist_codec_crc32_update(
                    g_project_rename.new_crc,g_project_rename.buffer,chunk);
                g_project_rename.remaining-=chunk;
            }
        }
        if(ok!=0U && g_project_rename.remaining==0U)g_project_rename.state=3U;
    }
    else if(g_project_rename.state==3U)
    {
        uint8_t header[PERSIST_CODEC_HEADER_BYTES];
        if(~g_project_rename.old_crc!=g_project_rename.old_expected
            ||persist_codec_build_project_document_header(g_project_rename.new_total,
                ~g_project_rename.new_crc,header)==0U
            ||f_lseek(&g_project_rename.target,0U)!=FR_OK
            ||project_rename_write(&g_project_rename.target,header,sizeof(header))==0U
            ||f_sync(&g_project_rename.target)!=FR_OK)ok=0U;
        if(ok!=0U)
        {
            if(f_close(&g_project_rename.target)!=FR_OK)ok=0U;
            g_project_rename.target_open=0U;
            if(f_close(&g_project_rename.source)!=FR_OK)ok=0U;
            g_project_rename.source_open=0U;
            if(ok!=0U && persistent_fatfs_commit_replace(g_project_rename.final_path,
                g_project_rename.temporary_path,g_project_rename.backup_path)!=FR_OK)ok=0U;
            if(ok!=0U)project_rename_finish(1U);
        }
    }
    if(ok==0U)project_rename_finish(0U);
    sd_scheduler_runtime_background_end();
}

void project_product_save_service(void)
{
    project_product_rename_service();
    if(g_project_save.state==PROJECT_SAVE_IDLE||g_project_save.state==PROJECT_SAVE_DONE)return;

    g_persist_dbg.project_progress=g_progress.done;
    g_persist_dbg.detail=(uint32_t)g_project_save.state;

    if(g_project_save.state==PROJECT_SAVE_RECONCILE_BEGIN)
    {
        g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_SNAPSHOT;
        persist_debug_stage(PERSIST_DBG_STAGE_CANDIDATE,0);
        if(pattern_live_reconcile_active_begin()!=0U)
            g_project_save.state=PROJECT_SAVE_RECONCILE_WAIT;
        return;
    }
    if(g_project_save.state==PROJECT_SAVE_RECONCILE_WAIT)
    {
        uint8_t success=0U;
        if(pattern_live_reconcile_active_take_result(&success)==0U)return;
        if(success==0U)
        {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,0);return;}
        if(project_save_capture_current()!=0U)g_project_save.state=PROJECT_SAVE_MOUNT;
        else project_save_fail(g_save_error,g_save_detail);
        return;
    }

    switch(g_project_save.state)
    {
        case PROJECT_SAVE_QUEUE_DOCUMENT_PLACEHOLDER:
            memset(g_project_save.header,0,PERSIST_CODEC_HEADER_BYTES);
            project_save_queue_write(g_project_save.header,PERSIST_CODEC_HEADER_BYTES,PROJECT_SAVE_ENCODE_CORE);
            return;
        case PROJECT_SAVE_ENCODE_CORE:
            if(project_save_encode_core())project_save_queue_write(g_project_save.header,8U,PROJECT_SAVE_QUEUE_CORE);
            return;
        case PROJECT_SAVE_QUEUE_CORE:
            project_save_queue_write(g_project_save.encoded_data,g_project_save.encoded_size,PROJECT_SAVE_ENCODE_ASSETS);
            return;
        case PROJECT_SAVE_ENCODE_ASSETS:
            if(project_save_encode_assets())project_save_queue_write(g_project_save.header,8U,PROJECT_SAVE_QUEUE_ASSETS);
            return;
        case PROJECT_SAVE_QUEUE_ASSETS:
            project_save_queue_write(g_project_save.encoded_data,g_project_save.encoded_size,PROJECT_SAVE_ENCODE_MACROS);
            return;
        case PROJECT_SAVE_ENCODE_MACROS:
            if(project_save_encode_macros())project_save_queue_write(g_project_save.header,8U,PROJECT_SAVE_QUEUE_MACROS);
            return;
        case PROJECT_SAVE_QUEUE_MACROS:
            project_save_queue_write(g_project_save.encoded_data,g_project_save.encoded_size,PROJECT_SAVE_CRC_SEEK);
            return;
        default:
            break;
    }

    uint32_t chunk=0U;sd_scheduler_background_kind_t kind=SD_SCHEDULER_BACKGROUND_METADATA;
    if(g_project_save.state==PROJECT_SAVE_WRITE)
    {chunk=g_project_save.write_size-g_project_save.write_offset;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    else if(g_project_save.state==PROJECT_SAVE_CRC_READ)
    {chunk=g_project_save.crc_remaining;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    else if(g_project_save.state==PROJECT_SAVE_PATTERN_COPY)
    {chunk=g_project_save.pattern_source.size-g_project_save.file_offset;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    const sd_scheduler_background_admission_t admission=project_save_admit(kind,chunk);
    if(admission==SD_SCHEDULER_BACKGROUND_NOT_NOW)return;
    if(admission!=SD_SCHEDULER_BACKGROUND_GO)
    {(void)project_save_abort_now(PROJECT_PRODUCT_SAVE_ERROR_MEDIA_CHANGED,0);return;}

    FRESULT fr=FR_OK;UINT transferred=0U;
    switch(g_project_save.state)
    {
        case PROJECT_SAVE_MOUNT:
            g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_MOUNT;
            persist_debug_stage(PERSIST_DBG_STAGE_MOUNT,0);
            if(!sd_access_fs_mount_if_needed())project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY,0);
            else g_project_save.state=PROJECT_SAVE_MKDIR_BRICK;
            break;
        case PROJECT_SAVE_MKDIR_BRICK:
        {
            const uint8_t made=(g_project_save.resume_mode!=0U)
                ?(uint8_t)(project_mkdir_path(project_storage_internal_root)
                    &&project_mkdir_path(project_storage_resume_root))
                :(uint8_t)(project_mkdir_path(project_storage_internal_root)
                    &&project_mkdir_path(project_storage_transactions_root)
                    &&project_mkdir_path(project_storage_project_transaction_root));
            if(!made)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)FR_DISK_ERR);
            else
            {
                if(g_project_save.resume_mode==0U)
                    (void)project_remove_tree(g_project_save.transaction_directory);
                g_project_save.state=PROJECT_SAVE_MKDIR_PROJECT;
            }
            break;
        }
        case PROJECT_SAVE_MKDIR_PROJECT:
        {
            char directory[64];
            memcpy(directory,g_project_save.transaction_directory,
                   strlen(g_project_save.transaction_directory)+1U);
            if(directory[0]=='\0')
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)FR_INVALID_NAME);
            else{fr=f_mkdir(directory);if(fr!=FR_OK&&fr!=FR_EXIST)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)fr);else if(g_project_save.resume_mode!=0U){char manifest[84];if(project_storage_resume_manifest(manifest,sizeof(manifest),g_project_save.resume_slot))(void)project_unlink_optional(manifest);g_project_save.state=PROJECT_SAVE_RECOVER;}else g_project_save.state=PROJECT_SAVE_MKDIR_PATTERNS;}
            break;
        }
        case PROJECT_SAVE_MKDIR_PATTERNS:
        {
            char directory[64];
            uint8_t built=project_storage_project_transaction_patterns_dir(
                directory,sizeof(directory),g_project_save.slot);
            if(!built)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)FR_INVALID_NAME);
            else{fr=f_mkdir(directory);if(fr!=FR_OK&&fr!=FR_EXIST)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_RECOVER;}
            break;
        }
        case PROJECT_SAVE_RECOVER:
            fr=persistent_fatfs_recover_replace(g_project_save.final_path,g_project_save.temporary_path,g_project_save.backup_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_OPEN;
            break;
        case PROJECT_SAVE_OPEN:
            g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_OPEN;
            persist_debug_stage(PERSIST_DBG_STAGE_OPEN,0);
            memset(&g_project_save.project_file,0,sizeof(g_project_save.project_file));
            fr=f_open(&g_project_save.project_file.file,g_project_save.temporary_path,
                      FA_CREATE_ALWAYS|FA_WRITE|FA_READ);
            g_project_save.project_file.last_result=fr;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_OPEN,(int32_t)fr);
            else{g_project_save.project_open=1U;g_project_save.file_offset=0U;g_project_save.state=PROJECT_SAVE_QUEUE_DOCUMENT_PLACEHOLDER;}
            break;
        case PROJECT_SAVE_WRITE:
            g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_WRITE;
            persist_debug_stage(PERSIST_DBG_STAGE_WRITE,0);
            fr=f_write(&g_project_save.project_file.file,
                &g_project_save.write_data[g_project_save.write_offset],chunk,&transferred);
            if(fr!=FR_OK||transferred!=chunk)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(fr!=FR_OK)?(int32_t)fr:-1);
            else{g_project_save.write_offset+=chunk;g_project_save.file_offset+=chunk;g_progress.done=g_project_save.file_offset;if(g_project_save.write_offset==g_project_save.write_size)g_project_save.state=g_project_save.after_write;}
            break;
        case PROJECT_SAVE_CRC_SEEK:
            fr=f_lseek(&g_project_save.project_file.file,PERSIST_CODEC_HEADER_BYTES);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)fr);
            else{g_project_save.crc=0xFFFFFFFFUL;g_project_save.crc_remaining=g_project_save.file_offset-PERSIST_CODEC_HEADER_BYTES;g_project_save.state=PROJECT_SAVE_CRC_READ;}
            break;
        case PROJECT_SAVE_CRC_READ:
            fr=f_read(&g_project_save.project_file.file,g_project_save.workspace->encode_scratch,chunk,&transferred);
            if(fr!=FR_OK||transferred!=chunk)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(fr!=FR_OK)?(int32_t)fr:-1);
            else{g_project_save.crc=persist_codec_crc32_update(g_project_save.crc,g_project_save.workspace->encode_scratch,chunk);g_project_save.crc_remaining-=chunk;if(g_project_save.crc_remaining==0U)g_project_save.state=PROJECT_SAVE_WRITE_DOCUMENT_HEADER;}
            break;
        case PROJECT_SAVE_WRITE_DOCUMENT_HEADER:
            if(!persist_codec_build_project_document_header(g_project_save.file_offset,~g_project_save.crc,g_project_save.header))
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);break;}
            fr=f_lseek(&g_project_save.project_file.file,0U);
            if(fr==FR_OK)fr=f_write(&g_project_save.project_file.file,g_project_save.header,PERSIST_CODEC_HEADER_BYTES,&transferred);
            if(fr!=FR_OK||transferred!=PERSIST_CODEC_HEADER_BYTES)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_SYNC;
            break;
        case PROJECT_SAVE_SYNC:
            g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_SYNC;
            persist_debug_stage(PERSIST_DBG_STAGE_WRITE,0);
            fr=f_sync(&g_project_save.project_file.file);if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_CLOSE;
            break;
        case PROJECT_SAVE_CLOSE:
            persist_debug_stage(PERSIST_DBG_STAGE_CLOSE,0);
            fr=persistent_fatfs_close_result(&g_project_save.project_file);g_project_save.project_open=0U;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CLOSE,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_COMMIT;
            break;
        case PROJECT_SAVE_COMMIT:
            g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_SAVE_REPLACE;
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_COMMIT,0);
            fr=persistent_fatfs_commit_replace(g_project_save.final_path,g_project_save.temporary_path,g_project_save.backup_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);
            else
            {
                g_project_save.transaction_index=0U;
                g_project_save.state=(g_project_save.resume_mode!=0U)
                    ?PROJECT_SAVE_RESUME_MANIFEST:PROJECT_SAVE_PATTERN_NEXT;
            }
            break;
        case PROJECT_SAVE_RESUME_MANIFEST:
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_COMMIT,0);
            if(project_resume_write_manifest_mounted()==0U)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)FR_DISK_ERR);
            else
            {
                g_project_save.transaction_committed=1U;
                g_persist_dbg.commit_done=1U;
                g_progress.total=g_progress.done=g_project_save.file_offset;
                project_save_finish(1U);
            }
            break;
        case PROJECT_SAVE_PATTERN_NEXT:
        {
            while(g_project_save.transaction_index<256U
                &&project_tx_bit(g_project_save.patterns,
                    g_project_save.transaction_index)==0U)
                ++g_project_save.transaction_index;
            if(g_project_save.transaction_index>=256U)
            {g_project_save.state=(g_project_save.new_project!=0U)
                ?PROJECT_SAVE_MANIFEST_NEW:PROJECT_SAVE_MANIFEST_PREPARED;break;}
            const uint16_t index=g_project_save.transaction_index;
            g_project_save.transaction_bank=(uint8_t)(index>>4U);
            g_project_save.transaction_pattern=(uint8_t)(index&15U);
            if(project_save_pattern_paths(g_project_save.transaction_bank,
                    g_project_save.transaction_pattern)==0U)
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT,(int32_t)index);break;}
            if(g_project_save.new_project==0U)
            {
                FILINFO info;char saved[96];
                if(!project_storage_pattern_file(saved,sizeof(saved),g_project_save.slot,
                        g_project_save.transaction_bank,g_project_save.transaction_pattern))
                {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT,(int32_t)index);break;}
                fr=f_stat(saved,&info);
                if(fr!=FR_OK&&fr!=FR_NO_FILE&&fr!=FR_NO_PATH)
                {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)fr);break;}
                project_tx_set(g_project_save.existed,index,(fr==FR_OK)?1U:0U);
            }
            g_project_save.state=PROJECT_SAVE_PATTERN_OPEN_SOURCE;
            break;
        }
        case PROJECT_SAVE_PATTERN_OPEN_SOURCE:
            if(!persistent_fatfs_open_read(&g_project_save.pattern_source,
                    g_project_save.source_path))
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,
                                  (int32_t)g_project_save.transaction_index);
            else{g_project_save.pattern_source_open=1U;g_project_save.state=PROJECT_SAVE_PATTERN_OPEN_TARGET;}
            break;
        case PROJECT_SAVE_PATTERN_OPEN_TARGET:
            fr=persistent_fatfs_open_write_result(&g_project_save.pattern_target,
                    g_project_save.target_temporary_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_OPEN,(int32_t)fr);
            else{g_project_save.pattern_target_open=1U;g_project_save.file_offset=0U;g_project_save.state=PROJECT_SAVE_PATTERN_COPY;}
            break;
        case PROJECT_SAVE_PATTERN_COPY:
        {
            UINT read=0U,written=0U;
            fr=f_read(&g_project_save.pattern_source.file,
                      g_project_save.copy_buffer,chunk,&read);
            if(fr==FR_OK&&read==chunk)
                fr=f_write(&g_project_save.pattern_target.file,
                           g_project_save.copy_buffer,chunk,&written);
            if(fr!=FR_OK||read!=chunk||written!=chunk)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)fr);
            else
            {g_project_save.file_offset+=chunk;if(g_project_save.file_offset==g_project_save.pattern_source.size)g_project_save.state=PROJECT_SAVE_PATTERN_SYNC;}
            break;
        }
        case PROJECT_SAVE_PATTERN_SYNC:
            fr=f_sync(&g_project_save.pattern_target.file);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)fr);
            else g_project_save.state=PROJECT_SAVE_PATTERN_CLOSE;
            break;
        case PROJECT_SAVE_PATTERN_CLOSE:
            fr=persistent_fatfs_close_result(&g_project_save.pattern_target);
            g_project_save.pattern_target_open=0U;
            if(fr==FR_OK)fr=persistent_fatfs_close_result(&g_project_save.pattern_source);
            g_project_save.pattern_source_open=0U;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CLOSE,(int32_t)fr);
            else g_project_save.state=PROJECT_SAVE_PATTERN_STAGE_COMMIT;
            break;
        case PROJECT_SAVE_PATTERN_STAGE_COMMIT:
            fr=persistent_fatfs_commit_replace(g_project_save.target_path,
                    g_project_save.target_temporary_path,
                    g_project_save.target_backup_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);
            else{++g_project_save.transaction_index;g_project_save.state=PROJECT_SAVE_PATTERN_NEXT;}
            break;
        case PROJECT_SAVE_MANIFEST_NEW:
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_STAGE,0);
            if(project_tx_write_manifest(g_project_save.manifest_path,
                    g_project_save.slot,PROJECT_TX_PHASE_CANDIDATE,
                    g_project_save.patterns,g_project_save.existed)==0U)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)FR_DISK_ERR);
            else g_project_save.state=PROJECT_SAVE_PUBLISH_NEW;
            break;
        case PROJECT_SAVE_PUBLISH_NEW:
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_COMMIT,0);
            fr=project_unlink_optional(g_project_save.manifest_path)
                ?f_rename(g_project_save.transaction_directory,
                          g_project_save.project_directory)
                :FR_DISK_ERR;
            if(fr!=FR_OK)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);
            else
            {
                g_project_save.transaction_committed=1U;
                g_persist_dbg.commit_done=1U;
                pattern_control_bank_publish_project(g_project_save.slot,
                                                       g_project_save.patterns);
                project_save_set_active_identity();
                g_project_save.transaction_index=0U;
                g_project_save.state=PROJECT_SAVE_CLEAN_NEXT;
            }
            break;
        case PROJECT_SAVE_MANIFEST_PREPARED:
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_STAGE,0);
            if(project_tx_write_manifest(g_project_save.manifest_path,
                    g_project_save.slot,PROJECT_TX_PHASE_PREPARED,
                    g_project_save.dirty,g_project_save.existed)==0U)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)FR_DISK_ERR);
            else
            {g_project_save.transaction_prepared=1U;g_project_save.transaction_index=0U;g_project_save.state=PROJECT_SAVE_PUBLISH_NEXT;}
            break;
        case PROJECT_SAVE_PUBLISH_NEXT:
        {
            while(g_project_save.transaction_index>0U
                &&g_project_save.transaction_index<=256U
                &&project_tx_bit(g_project_save.dirty,
                    (uint16_t)(g_project_save.transaction_index-1U))==0U)
                ++g_project_save.transaction_index;
            if(g_project_save.transaction_index>256U)
            {g_project_save.state=PROJECT_SAVE_MANIFEST_COMMITTED;break;}
            char final_value[96],staged[96],backup[96];FILINFO info;
            if(!project_save_publish_paths(g_project_save.transaction_index,
                    final_value,sizeof(final_value),staged,sizeof(staged),
                    backup,sizeof(backup)))
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT,0);break;}
            (void)project_unlink_optional(backup);
            const uint8_t existed=(g_project_save.transaction_index==0U)
                ?1U:project_tx_bit(g_project_save.existed,
                    (uint16_t)(g_project_save.transaction_index-1U));
            if(existed!=0U&&f_stat(final_value,&info)==FR_OK)
                fr=f_rename(final_value,backup);
            else fr=FR_OK;
            if(fr==FR_OK)fr=f_rename(staged,final_value);
            if(fr!=FR_OK)
            {if(existed!=0U)(void)f_rename(backup,final_value);project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);}
            else ++g_project_save.transaction_index;
            break;
        }
        case PROJECT_SAVE_MANIFEST_COMMITTED:
            persist_debug_stage(PERSIST_DBG_STAGE_BANK_COMMIT,0);
            if(project_tx_write_manifest(g_project_save.manifest_path,
                    g_project_save.slot,PROJECT_TX_PHASE_COMMITTED,
                    g_project_save.dirty,g_project_save.existed)==0U)
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)FR_DISK_ERR);
            else
            {g_project_save.transaction_committed=1U;g_persist_dbg.commit_done=1U;g_project_save.transaction_index=0U;g_project_save.state=PROJECT_SAVE_CLEAN_NEXT;}
            break;
        case PROJECT_SAVE_CLEAN_NEXT:
        {
            while(g_project_save.transaction_index>0U
                &&g_project_save.transaction_index<=256U
                &&project_tx_bit(g_project_save.dirty,
                    (uint16_t)(g_project_save.transaction_index-1U))==0U)
                ++g_project_save.transaction_index;
            if(g_project_save.transaction_index>256U)
            {g_project_save.state=PROJECT_SAVE_CLEAN_DIRECTORY;break;}
            char final_value[96],staged[96],backup[96];
            if(project_save_publish_paths(g_project_save.transaction_index,
                    final_value,sizeof(final_value),staged,sizeof(staged),
                    backup,sizeof(backup))) (void)project_unlink_optional(backup);
            if(g_project_save.transaction_index!=0U)
            {
                const uint16_t index=(uint16_t)(g_project_save.transaction_index-1U);
                char working[96],temporary[100],working_backup[100];
                if(project_storage_working_pattern_file(working,sizeof(working),
                        (uint8_t)(index>>4U),(uint8_t)(index&15U)))
                {if(project_side_path_base(temporary,sizeof(temporary),working,"TMP"))(void)project_unlink_optional(temporary);
                 if(project_side_path_base(working_backup,sizeof(working_backup),working,"BAK"))(void)project_unlink_optional(working_backup);
                 (void)project_unlink_optional(working);}
                pattern_working_bank_mark_clean((uint8_t)(index>>4U),(uint8_t)(index&15U));
                pattern_control_bank_mark_present((uint8_t)(index>>4U),(uint8_t)(index&15U));
            }
            ++g_project_save.transaction_index;
            break;
        }
        case PROJECT_SAVE_CLEAN_DIRECTORY:
            (void)project_remove_tree(g_project_save.transaction_directory);
            g_progress.total=g_progress.done=g_project_save.file_offset;
            project_save_finish(1U);
            break;
        case PROJECT_SAVE_ROLLBACK_NEXT:
        {
            while(g_project_save.transaction_index>0U
                &&g_project_save.transaction_index<=256U
                &&project_tx_bit(g_project_save.dirty,
                    (uint16_t)(g_project_save.transaction_index-1U))==0U)
                ++g_project_save.transaction_index;
            if(g_project_save.transaction_index>256U)
            {(void)project_remove_tree(g_project_save.transaction_directory);project_save_finish(0U);break;}
            char final_value[96],staged[96],backup[96];FILINFO info;
            if(project_save_publish_paths(g_project_save.transaction_index,
                    final_value,sizeof(final_value),staged,sizeof(staged),
                    backup,sizeof(backup)))
            {
                if(f_stat(backup,&info)==FR_OK)
                {(void)project_unlink_optional(final_value);(void)f_rename(backup,final_value);}
                else if(g_project_save.transaction_index!=0U
                    &&project_tx_bit(g_project_save.existed,
                        (uint16_t)(g_project_save.transaction_index-1U))==0U)
                    (void)project_unlink_optional(final_value);
            }
            ++g_project_save.transaction_index;
            break;
        }
        case PROJECT_SAVE_CLEAN_PROJECT_CLOSE:
            (void)persistent_fatfs_close_result(&g_project_save.project_file);g_project_save.project_open=0U;g_project_save.state=PROJECT_SAVE_CLEAN_TEMP;
            break;
        case PROJECT_SAVE_CLEAN_TEMP:
            (void)f_unlink(g_project_save.temporary_path);
            (void)project_remove_tree(g_project_save.transaction_directory);
            project_save_finish(0U);
            break;
        default:
            project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);
            break;
    }
    sd_scheduler_runtime_background_end();
}

project_product_save_error_t project_product_save_last_error(void){return g_save_error;}
int32_t project_product_save_last_detail(void){return g_save_detail;}

enum
{
    PROJECT_FATAL_QUIESCE = 1U,
    PROJECT_FATAL_ASSET_COMPLETION,
    PROJECT_FATAL_ASSET_REGISTRATION,
    PROJECT_FATAL_PENDING_AT_COMMIT,
    PROJECT_FATAL_PATTERN_APPLY,
    PROJECT_FATAL_ASSET_RESET,
    PROJECT_FATAL_STATE,
    PROJECT_FATAL_BLANK_BUILD,
    PROJECT_FATAL_CANONICALIZE
};

#define PROJECT_PRODUCT_FATAL(message, context) \
    BRICK_FATAL_CONTEXT((message), BRICK_FATAL_PROJECT_COMMIT, \
        g_project_load.asset_index, (context), g_project_load.asset_index, \
        (g_project_load.restore != NULL) \
            ? g_project_load.restore->asset_count : 0U)

static uint8_t begin_assets(void*ctx){persistence_project_restore_workspace_t*w=ctx;if(w==NULL)return 0U;w->asset_count=0U;memset(w->assets,0,sizeof(w->assets));return 1U;}
static persist_control_asset_ref_t *asset_target(void*ctx,uint16_t ordinal){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||ordinal>=PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY)return NULL;return &w->assets[ordinal];}
static uint8_t validate_asset(void*ctx,const persist_control_asset_ref_t*a){persistence_project_restore_workspace_t*w=ctx;uint32_t index=(w!=NULL&&a>=w->assets&&a<&w->assets[PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY])?(uint32_t)(a-w->assets):UINT32_MAX;persist_debug_object(PERSIST_DBG_OBJECT_ASSET,index,(a!=NULL)?a->kind:0U);const uint8_t ok=project_control_validate_asset(a);if(ok==0U)persist_debug_validation_fail(PERSIST_DBG_VALIDATION_ASSET_REFERENCE,PERSIST_DBG_ERROR_VALIDATE,UINT32_MAX,0U,0U,0U,0U,0U,0U,0U);return ok;}
static uint8_t apply_metadata(void*ctx,const persist_codec_project_metadata_t*m){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL||m->asset_count>PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY)return 0U;if(m->name_length!=0U){char source[NAME_CONTRACT_BUFFER_BYTES]={0},normalized[NAME_CONTRACT_BUFFER_BYTES];if(m->name_length>PERSIST_CODEC_PROJECT_NAME_BYTES)return 0U;memcpy(source,m->name,m->name_length);if(!project_name_normalize(source,normalized)||strlen(normalized)!=m->name_length||memcmp(source,normalized,m->name_length)!=0)return 0U;}w->metadata=*m;w->asset_count=m->asset_count;return 1U;}
static uint8_t apply_macros(void*ctx,const persist_control_macros_t*m){persist_debug_object(PERSIST_DBG_OBJECT_MACROS,0U,0U);persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL)return 0U;w->macros=*m;w->macros_valid=1U;return 1U;}
static uint8_t project_product_pattern_assets_resolved(
    const persistence_project_restore_workspace_t *restore,
    const persist_control_pattern_t *pattern);
static uint8_t project_product_prevalidate_candidate(
    const persistence_project_restore_workspace_t *restore);
static uint8_t project_product_build_default_candidate(
    persistence_project_restore_workspace_t *restore)
{
    if (restore == NULL)
        PROJECT_PRODUCT_FATAL("PROJECT_BLANK_ARGUMENT_INVALID",
                              PROJECT_FATAL_BLANK_BUILD);
    memset(restore,0,sizeof(*restore));
    if (pattern_live_build_slot_default(&restore->working_pattern,0U,0U) == 0U
        || project_control_get_default_macros(&restore->macros) == 0U)
        PROJECT_PRODUCT_FATAL("PROJECT_BLANK_DEFAULT_BUILD_FAILED",
                              PROJECT_FATAL_BLANK_BUILD);

    restore->metadata.active_pattern_bank=0U;
    restore->metadata.active_pattern=0U;
    restore->metadata.asset_count=0U;
    restore->working_valid=1U;
    restore->macros_valid=1U;
    if(persist_codec_validate_macros(&restore->macros)!=PERSIST_CODEC_OK
        ||project_product_prevalidate_candidate(restore)==0U)
        PROJECT_PRODUCT_FATAL("PROJECT_BLANK_DEFAULT_INVALID",
                              PROJECT_FATAL_BLANK_BUILD);
    return 1U;
}

static void project_product_cross_forward(void)
{
    if (g_project_load.restore == NULL)
        PROJECT_PRODUCT_FATAL("PROJECT_FORWARD_WITHOUT_CANDIDATE",
                              PROJECT_FATAL_STATE);
    g_project_load.asset_index = 0U;
    g_project_load.media_epoch = sd_access_media_epoch();
    g_project_load.state = PROJECT_LOAD_FORWARD_WAIT_SAFE;
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_LOAD_WAIT_QUIESCE, 0U,
                          (uint32_t)g_project_load.state);
    if (g_project_load.forward_crossed == 0U)
    {
        /* The ingress close performed by this call is the one and only
         * Project T_FORWARD.  Nothing below may restore the previous live
         * Project or reopen ingress except successful FINALIZE. */
        project_load_quiesce_request();
        g_project_load.forward_crossed = 1U;
    }
    else
    {
        /* Explicit retry from FAILED_FORWARD_MEDIA: quiesce remains owned and
         * ingress has never reopened. */
        project_load_quiesce_request();
    }
    pattern_live_cancel_recall();
    g_persist_dbg.cancel_reason=PERSIST_DBG_CANCEL_PROJECT_REPLACEMENT;
}

static void project_product_begin_prepare(
    persistence_project_restore_workspace_t *restore,
    uint8_t slot,
    uint8_t forward_already_crossed)
{
    g_project_load.restore=restore;
    g_project_load.asset_index=0U;
    g_project_load.slot=slot;
    g_project_load.forward_crossed=forward_already_crossed;
    g_project_load.media_epoch=sd_access_media_epoch();
    g_project_load.state=PROJECT_LOAD_PREPARE_CANONICALIZE;
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_LOAD_STAGED,0U,
                          (uint32_t)g_project_load.state);
}

uint8_t project_product_load_busy(void)
{
    return (g_project_load.state != PROJECT_LOAD_IDLE
        && g_project_load.state != PROJECT_LOAD_FAILED_FORWARD_MEDIA) ? 1U : 0U;
}

static void project_discard_restore_workspace(
    persistence_project_restore_workspace_t *restore)
{
    if (restore == NULL) return;
    persistent_pattern_control_abort_prepared(&restore->prepared_pattern);
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
}

static void project_product_discard_prepared_candidate(
    persistence_project_restore_workspace_t *restore,
    project_product_result_t result,
    uint8_t recovery_forward)
{
    project_discard_restore_workspace(restore);
    if (recovery_forward != 0U)
    {
        /* A rejected recovery candidate cannot undo the already crossed
         * boundary.  Preserve the safe terminal state and closed ingress. */
        g_project_load.state=PROJECT_LOAD_FAILED_FORWARD_MEDIA;
        g_project_load.restore=NULL;
        g_progress=(project_product_progress_t){.complete=1U,
            .result=PROJECT_PRODUCT_RESULT_FAILED_FORWARD_MEDIA};
        return;
    }
    g_progress=(project_product_progress_t){
        .complete=(result==PROJECT_PRODUCT_RESULT_NOT_NOW)?0U:1U,
        .result=result};
}

static uint8_t project_product_asset_equal(
    const persist_control_asset_ref_t *a,
    const persist_control_asset_ref_t *b)
{
    return (uint8_t)((a != NULL) && (b != NULL) && (a->kind == b->kind)
        && (a->path_length == b->path_length)
        && (memcmp(a->canonical_path, b->canonical_path, a->path_length) == 0));
}

static uint8_t project_product_pattern_assets_resolved(
    const persistence_project_restore_workspace_t *restore,
    const persist_control_pattern_t *pattern)
{
    if ((restore == NULL) || (pattern == NULL)) return 0U;
    for (uint8_t entity = 0U; entity < PERSIST_CONTROL_ENTITY_COUNT; ++entity)
        for (uint8_t role = 0U; role < pattern->entities[entity].asset_count; ++role)
        {
            uint8_t found = 0U;
            for (uint16_t index = 0U; index < restore->asset_count; ++index)
                if (project_product_asset_equal(
                        &pattern->entities[entity].assets[role],
                        &restore->assets[index]) != 0U)
                {
                    found = 1U;
                    break;
                }
            if (found == 0U) return 0U;
        }
    return 1U;
}

static uint8_t project_product_prevalidate_candidate(
    const persistence_project_restore_workspace_t *restore)
{
    uint16_t classic_slots = 0U, ram_slots = 0U, wavetable_slots = 0U;
    uint16_t multi_instruments = 0U;
    if ((restore == NULL)
        || (project_product_pattern_assets_resolved(
                restore, &restore->working_pattern) == 0U)) return 0U;
    for (uint16_t index = 0U; index < restore->asset_count; ++index)
    {
        const persist_control_asset_ref_t *const asset = &restore->assets[index];
        if (project_control_validate_asset(asset) == 0U) return 0U;
        for(uint16_t previous=0U;previous<index;++previous)
            if(project_product_asset_equal(asset,&restore->assets[previous])!=0U)
                return 0U;
        if (asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
            ++classic_slots;
        else if (asset->kind == PERSIST_ASSET_SAMPLE_RAM)
            ++ram_slots;
        else if (asset->kind == PERSIST_ASSET_WAVETABLE)
            ++wavetable_slots;
        else if (asset->kind == PERSIST_ASSET_MULTI)
            ++multi_instruments;
        else return 0U;
    }
    return (uint8_t)((classic_slots + ram_slots + wavetable_slots
                           + multi_instruments <= SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS)
        && (ram_slots <= SAMPLER_RAM_POOL_MAX_SLOTS)
        && (wavetable_slots <= WAVETABLE_POOL_MAX_SLOTS)
        && (multi_instruments <= MULTI_SAMPLE_POOL_MAX_INSTRUMENTS));
}

static void project_product_reject_prepare(project_product_result_t result)
{
    persistence_project_restore_workspace_t *const restore=g_project_load.restore;
    if (g_project_load.forward_crossed != 0U)
        PROJECT_PRODUCT_FATAL("PROJECT_PREPARE_REJECT_AFTER_FORWARD",
                              PROJECT_FATAL_STATE);
    project_discard_restore_workspace(restore);
    memset(&g_project_load,0,sizeof(g_project_load));
    g_progress=(project_product_progress_t){.complete=1U,.result=result};
}

static void project_product_enter_failed_forward_media(void)
{
    persistence_project_restore_workspace_t *const restore=g_project_load.restore;
    if (g_project_load.forward_crossed == 0U)
        PROJECT_PRODUCT_FATAL("PROJECT_FORWARD_MEDIA_BEFORE_BOUNDARY",
                              PROJECT_FATAL_STATE);
    persist_debug_error(PERSIST_DBG_STAGE_READ,PERSIST_DBG_ERROR_FILESYSTEM);
    sampler_ram_pool_load_async_cancel();
    wavetable_pool_load_async_cancel();
    multi_sample_cancel_all_loads();
    /* Retire any replacement payload that became READY before the media loss.
     * This is forward cleanup, never restoration of the previous Project. */
    sampler_ram_pool_retire_all();
    wavetable_pool_retire_all();
    multi_sample_pool_retire_all();
    if (restore != NULL)
    {
        persistent_pattern_control_abort_prepared(&restore->prepared_pattern);
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
    }
    memset(&g_project_load,0,sizeof(g_project_load));
    g_project_load.state=PROJECT_LOAD_FAILED_FORWARD_MEDIA;
    g_project_load.forward_crossed=1U;
    g_project_load.slot=PROJECT_PRODUCT_NO_SLOT;
    g_progress.done=g_progress.total;
    g_progress.complete=1U;
    g_progress.active=0U;
    g_progress.result=PROJECT_PRODUCT_RESULT_FAILED_FORWARD_MEDIA;
    /* Deliberately no project_load_quiesce_end(): ingress stays closed until
     * an explicit replacement successfully reaches FINALIZE. */
}

static uint8_t project_product_asset_loads_pending(void)
{
    return (uint8_t)(project_control_asset_loads_pending() != 0U
        || sampler_ram_pool_load_async_busy() != 0U
        || wavetable_pool_load_async_busy() != 0U
        || multi_sample_load_has_pending() != 0U
        || sample_cache_has_pending_sd_work() != 0U);
}

static void project_product_load_finish_success(void)
{
    persistence_project_restore_workspace_t *const restore=g_project_load.restore;
    const uint8_t slot=g_project_load.slot;
    if (g_project_load.forward_crossed == 0U || restore == NULL)
        PROJECT_PRODUCT_FATAL("PROJECT_FINALIZE_STATE_INVALID",
                              PROJECT_FATAL_STATE);
    if (g_project_load.asset_index < restore->asset_count
        || project_product_asset_loads_pending() != 0U)
        PROJECT_PRODUCT_FATAL("PROJECT_FINALIZE_WITH_PENDING_ASSET",
                              PROJECT_FATAL_PENDING_AT_COMMIT);
    if ((slot != PROJECT_PRODUCT_NO_SLOT) && (restore != NULL))
    {
        memset(&g_current_metadata, 0, sizeof(g_current_metadata));
        memcpy(g_current_metadata.name, restore->metadata.name,
               restore->metadata.name_length);
        g_project_metadata[slot] = g_current_metadata;
    }
    else if (slot == PROJECT_PRODUCT_NO_SLOT)
        memset(&g_current_metadata, 0, sizeof(g_current_metadata));
    if(restore!=NULL)persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
    memset(&g_project_load,0,sizeof(g_project_load));
    g_progress.done=g_progress.total;
    g_progress.complete=1U;
    g_progress.active=0U;
    if (slot != PROJECT_PRODUCT_NO_SLOT)
    {
        g_active=slot;
        g_active_valid=1U;
        (void)boot_context_sd_commit(slot);
    }
    else
    {
        g_active=0U;
        g_active_valid=0U;
        boot_context_sd_clear();
    }
    g_progress.result=PROJECT_PRODUCT_RESULT_SUCCESS;
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_DONE,g_progress.done,
                          (uint32_t)g_progress.result);
    persist_debug_stage(PERSIST_DBG_STAGE_SUCCESS,0);
    project_load_quiesce_end();
}

static uint8_t project_ram_result_internal(sampler_ram_result_t result)
{
    return (uint8_t)(result == SAMPLER_RAM_RESULT_INVALID_ARG
        || result == SAMPLER_RAM_RESULT_POOL_FULL
        || result == SAMPLER_RAM_RESULT_GLOBAL_SLOT_FULL
        || result == SAMPLER_RAM_RESULT_PATH_TOO_LONG
        || result == SAMPLER_RAM_RESULT_REGISTER_FAIL
        || result == SAMPLER_RAM_RESULT_TRANSPORT_ACTIVE
        || result == SAMPLER_RAM_RESULT_RECORDER_ACTIVE);
}

static uint8_t project_wavetable_result_internal(wavetable_result_t result)
{
    return (uint8_t)(result == WAVETABLE_RESULT_INVALID_ARG
        || result == WAVETABLE_RESULT_POOL_FULL
        || result == WAVETABLE_RESULT_GLOBAL_SLOT_FULL
        || result == WAVETABLE_RESULT_PATH_TOO_LONG
        || result == WAVETABLE_RESULT_SD_BUSY
        || result == WAVETABLE_RESULT_REGISTER_FAIL
        || result == WAVETABLE_RESULT_TRANSPORT_ACTIVE
        || result == WAVETABLE_RESULT_RECORDER_ACTIVE);
}

static uint8_t project_multi_result_internal(multi_sample_load_result_t result)
{
    return (uint8_t)(result == MULTI_SAMPLE_LOAD_INVALID_ARG
        || result == MULTI_SAMPLE_LOAD_SD_BUSY
        || result == MULTI_SAMPLE_LOAD_POOL_FAIL
        || result == MULTI_SAMPLE_LOAD_PATH_TOO_LONG
        || result == MULTI_SAMPLE_LOAD_REGISTER_FAIL
        || result == MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE
        || result == MULTI_SAMPLE_LOAD_CANCELLED);
}

static uint8_t project_product_asset_needs_sample_canonicalization(
    const persist_control_asset_ref_t *asset)
{
    return (uint8_t)((asset != NULL)
        && ((asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
            || (asset->kind == PERSIST_ASSET_SAMPLE_RAM)));
}

static uint8_t project_product_wav_convert_error_is_media(
    wav_convert_error_t error)
{
    return (uint8_t)(error == WAV_CONVERT_ERROR_MOUNT_FAIL
        || error == WAV_CONVERT_ERROR_WRITE_FAIL
        || error == WAV_CONVERT_ERROR_SYNC_FAIL
        || error == WAV_CONVERT_ERROR_CLOSE_FAIL
        || error == WAV_CONVERT_ERROR_REPLACE_FAIL
        || error == WAV_CONVERT_ERROR_NO_SPACE);
}

static void project_product_prepare_media_failure(void)
{
    if (g_project_load.forward_crossed != 0U)
        project_product_enter_failed_forward_media();
    else
        project_product_reject_prepare(PROJECT_PRODUCT_RESULT_MEDIA_ERROR);
}

static void project_product_load_service_canonicalize(
    persistence_project_restore_workspace_t *restore)
{
    if (restore == NULL)
        PROJECT_PRODUCT_FATAL("PROJECT_CANONICALIZE_WITHOUT_CANDIDATE",
                              PROJECT_FATAL_STATE);

    if (wav_convert_is_active() != 0U)
    {
        wav_convert_service(65536U);
        if (wav_convert_is_active() != 0U) return;

        if (wav_convert_get_state() != WAV_CONVERT_STATE_DONE)
        {
            const wav_convert_error_t error=wav_convert_get_last_error();
            wav_convert_clear_finished();
            if (error == WAV_CONVERT_ERROR_INVALID_ARG)
                PROJECT_PRODUCT_FATAL("PROJECT_CANONICALIZE_ARGUMENT_INVALID",
                                      PROJECT_FATAL_CANONICALIZE);
            if (project_product_wav_convert_error_is_media(error) != 0U)
            {
                project_product_prepare_media_failure();
                return;
            }
            /* The referenced external asset is not convertible.  Keep the
             * reference; LOAD_ASSETS will publish it as UNAVAILABLE. */
            ++g_project_load.asset_index;
            return;
        }
        g_project_load.media_epoch = sd_access_media_epoch();
        wav_convert_clear_finished();
        ++g_project_load.asset_index;
    }

    if (sd_access_storage_status() != SD_STORAGE_STATUS_READY
        || sd_access_media_epoch() != g_project_load.media_epoch)
    {
        project_product_prepare_media_failure();
        return;
    }

    while (g_project_load.asset_index < restore->asset_count)
    {
        const persist_control_asset_ref_t *const asset =
            &restore->assets[g_project_load.asset_index];
        if (project_product_asset_needs_sample_canonicalization(asset) != 0U)
        {
            char path_value[PERSIST_CONTROL_ASSET_PATH_BYTES];
            memcpy(path_value, asset->canonical_path, asset->path_length);
            path_value[asset->path_length] = '\0';
            const wav_convert_path_status_t path_status =
                wav_convert_path_canonical_status(path_value, NULL);
            if (path_status == WAV_CONVERT_PATH_BUSY) return;
            if (path_status == WAV_CONVERT_PATH_NEEDS_CANONICAL)
            {
                if (wav_convert_start_destructive_canonical_project(path_value) == 0U)
                {
                    const wav_convert_error_t error = wav_convert_get_last_error();
                    wav_convert_clear_finished();
                    if (error == WAV_CONVERT_ERROR_BUSY) return;
                    if (error == WAV_CONVERT_ERROR_INVALID_ARG)
                        PROJECT_PRODUCT_FATAL(
                            "PROJECT_CANONICALIZE_START_INVALID",
                            PROJECT_FATAL_CANONICALIZE);
                    if (project_product_wav_convert_error_is_media(error) != 0U)
                    {
                        project_product_prepare_media_failure();
                        return;
                    }
                    /* Missing/invalid external asset: defer to UNAVAILABLE. */
                    ++g_project_load.asset_index;
                    return;
                }
                return;
            }
        }
        ++g_project_load.asset_index;
    }

    g_project_load.asset_index = 0U;
    project_product_cross_forward();
}

void project_product_load_service(void)
{
    persistence_project_restore_workspace_t *const restore=g_project_load.restore;
    if(g_project_load.state==PROJECT_LOAD_IDLE
        ||g_project_load.state==PROJECT_LOAD_FAILED_FORWARD_MEDIA)return;
    g_persist_dbg.project_progress=g_progress.done;
    g_persist_dbg.detail=(uint32_t)g_project_load.state;
    if (g_project_load.state == PROJECT_LOAD_PREPARE_CANONICALIZE)
    {
        project_product_load_service_canonicalize(restore);
        return;
    }
    if (sd_access_storage_status() != SD_STORAGE_STATUS_READY
        || sd_access_media_epoch() != g_project_load.media_epoch)
    {
        if (g_project_load.state == PROJECT_LOAD_FORWARD_WAIT_SAFE
            && project_load_quiesce_safe() == 0U)
        {
            if (project_load_quiesce_failed() != 0U)
                PROJECT_PRODUCT_FATAL("PROJECT_LOAD_QUIESCE_FAILED",
                                      PROJECT_FATAL_QUIESCE);
            return;
        }
        project_product_enter_failed_forward_media();
        return;
    }
    if (g_project_load.state == PROJECT_LOAD_FORWARD_WAIT_SAFE
        && project_load_quiesce_failed() != 0U)
    {
        PROJECT_PRODUCT_FATAL("PROJECT_LOAD_QUIESCE_FAILED",
                              PROJECT_FATAL_QUIESCE);
    }
    if(g_project_load.state==PROJECT_LOAD_INSTALL_WAIT_MULTI)
    {
        if(multi_sample_load_has_pending()!=0U)return;
        const persist_control_asset_ref_t *const asset =
            &restore->assets[g_project_load.asset_index];
        char path_value[PERSIST_CONTROL_ASSET_PATH_BYTES];
        memcpy(path_value,asset->canonical_path,asset->path_length);
        path_value[asset->path_length]='\0';
        uint16_t logical=0U,runtime=0U;
        multi_sample_load_status_t load_status;
        multi_sample_get_load_status(&load_status);
        if (project_control_find_asset(PERSIST_ASSET_MULTI,path_value,&logical)==0U
                || project_control_resolve_multi_runtime(logical,&runtime)==0U)
        {
            if (project_multi_result_internal(load_status.last_error)
                || load_status.last_error == MULTI_SAMPLE_LOAD_OK)
            {
                PROJECT_PRODUCT_FATAL(
                    "PROJECT_MULTI_ASSET_COMPLETION_FAILED",
                    PROJECT_FATAL_ASSET_COMPLETION);
            }
            ++g_progress.asset_warning_count;
        }
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_INSTALL_ASSETS;
        return;
    }
    if(g_project_load.state==PROJECT_LOAD_INSTALL_WAIT_STREAM)
    {
        const project_control_asset_result_t result = project_control_put_asset(
            &restore->assets[g_project_load.asset_index]);
        if(result==PROJECT_CONTROL_ASSET_PENDING)return;
        if(result==PROJECT_CONTROL_ASSET_FAILED_INTERNAL)
        {
            PROJECT_PRODUCT_FATAL("PROJECT_STREAM_ASSET_REGISTRATION_FAILED",
                                  PROJECT_FATAL_ASSET_REGISTRATION);
        }
        if(result==PROJECT_CONTROL_ASSET_FAILED)++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_INSTALL_ASSETS;
    }
    if(restore==NULL)
        PROJECT_PRODUCT_FATAL("PROJECT_INSTALL_WITHOUT_CANDIDATE",
                              PROJECT_FATAL_STATE);
    if (g_project_load.state == PROJECT_LOAD_FORWARD_WAIT_SAFE)
    {
        if (project_load_quiesce_safe() == 0U) return;
        g_project_load.asset_index = 0U;
        g_project_load.state = PROJECT_LOAD_INSTALL_BANK;
    }
    if (g_project_load.state == PROJECT_LOAD_INSTALL_BANK)
    {
        sd_scheduler_runtime_exclusive_request();
        if (sd_scheduler_runtime_exclusive_try_begin() == 0U) return;
        if (g_project_load.resume_mode!=0U)
        {
            const uint8_t activated=(g_project_load.resume_base_kind==PATTERN_WORKING_BASE_PROJECT)
                ?pattern_control_bank_activate_resume_project(g_project_load.slot,
                    g_project_load.resume_dirty)
                :pattern_control_bank_activate_resume_blank(g_project_load.resume_dirty);
            if(activated==0U)
            {
                sd_scheduler_runtime_exclusive_end();
                project_product_enter_failed_forward_media();
                return;
            }
        }
        else if (g_project_load.slot!=PROJECT_PRODUCT_NO_SLOT
            &&pattern_control_bank_activate_project(g_project_load.slot)==0U)
        {
            sd_scheduler_runtime_exclusive_end();
            project_product_enter_failed_forward_media();
            return;
        }
        if(g_project_load.slot==PROJECT_PRODUCT_NO_SLOT)
            pattern_control_bank_deactivate_project();
        g_persist_dbg.commit_done=1U;
        g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_LOAD_ASSETS;
        persist_debug_stage(PERSIST_DBG_STAGE_BANK_COMMIT,0);
        if (project_control_begin_asset_restore() == 0U)
        {
            sd_scheduler_runtime_exclusive_end();
            PROJECT_PRODUCT_FATAL("PROJECT_ASSET_RESET_FAILED",
                                  PROJECT_FATAL_ASSET_RESET);
        }
        g_project_load.state = PROJECT_LOAD_INSTALL_ASSETS;
        g_progress=(project_product_progress_t){.active=1U,
            .total=(uint32_t)restore->asset_count+1U,
            .result=PROJECT_PRODUCT_RESULT_IN_PROGRESS};
        sd_scheduler_runtime_exclusive_end();
        return;
    }
    if(g_project_load.state==PROJECT_LOAD_INSTALL_WAIT_RAM)
    {
        sampler_ram_result_t result=SAMPLER_RAM_RESULT_INVALID_ARG;
        uint16_t backend=SAMPLER_RAM_POOL_INVALID_SLOT;
        uint16_t runtime=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const char *path_value=NULL;
        if(sampler_ram_pool_load_async_take_result(&result,&backend,&runtime,&path_value)==0U)return;
        const project_control_asset_result_t completion =
            project_control_complete_ram_runtime(path_value,backend,runtime,
                (result==SAMPLER_RAM_RESULT_OK)?1U:0U);
        if (completion == PROJECT_CONTROL_ASSET_FAILED_INTERNAL
            || project_ram_result_internal(result) != 0U)
        {
            PROJECT_PRODUCT_FATAL("PROJECT_RAM_ASSET_COMPLETION_FAILED",
                                  PROJECT_FATAL_ASSET_COMPLETION);
        }
        if (result!=SAMPLER_RAM_RESULT_OK) ++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_INSTALL_ASSETS;
    }
    if (g_project_load.state == PROJECT_LOAD_INSTALL_WAIT_WAVETABLE)
    {
        wavetable_result_t result = WAVETABLE_RESULT_INVALID_ARG;
        uint16_t backend = UINT16_MAX;
        uint16_t runtime = SAMPLE_GLOBAL_POOL_INVALID_INDEX;
        const char *path_value = NULL;
        if (wavetable_pool_load_async_take_result(&result, &backend,
                                                  &runtime, &path_value) == 0U)
            return;
        (void)backend;
        project_control_asset_result_t completion = PROJECT_CONTROL_ASSET_FAILED_INTERNAL;
        if (path_value != NULL)
            completion = project_control_complete_wavetable_runtime(
                path_value, backend, runtime,
                (result == WAVETABLE_RESULT_OK) ? 1U : 0U);
        if (completion == PROJECT_CONTROL_ASSET_FAILED_INTERNAL
            || project_wavetable_result_internal(result) != 0U)
        {
            PROJECT_PRODUCT_FATAL("PROJECT_WAVETABLE_ASSET_COMPLETION_FAILED",
                                  PROJECT_FATAL_ASSET_COMPLETION);
        }
        if (result != WAVETABLE_RESULT_OK) ++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state = PROJECT_LOAD_INSTALL_ASSETS;
    }
    if(g_project_load.state==PROJECT_LOAD_INSTALL_ASSETS)
    {
        g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_LOAD_ASSETS;
        persist_debug_stage(PERSIST_DBG_STAGE_VALIDATE,0);
        persist_debug_details(g_project_load.asset_index,restore->asset_count,
                              g_progress.asset_warning_count,g_persist_dbg.commit_done);
        if(g_project_load.asset_index<restore->asset_count)
        {
            persist_debug_object(PERSIST_DBG_OBJECT_ASSET,
                g_project_load.asset_index,
                restore->assets[g_project_load.asset_index].kind);
            const project_control_asset_result_t result = project_control_put_asset(
                &restore->assets[g_project_load.asset_index]);
            if(result==PROJECT_CONTROL_ASSET_PENDING)
            {
                const uint32_t kind=restore->assets[g_project_load.asset_index].kind;
                g_project_load.state=(kind==PERSIST_ASSET_SAMPLE_STREAM)
                    ? PROJECT_LOAD_INSTALL_WAIT_STREAM
                    : (kind==PERSIST_ASSET_WAVETABLE
                        ? PROJECT_LOAD_INSTALL_WAIT_WAVETABLE
                        : (kind==PERSIST_ASSET_MULTI
                            ? PROJECT_LOAD_INSTALL_WAIT_MULTI
                            : PROJECT_LOAD_INSTALL_WAIT_RAM));
                return;
            }
            if(result==PROJECT_CONTROL_ASSET_FAILED_INTERNAL)
            {
                PROJECT_PRODUCT_FATAL("PROJECT_ASSET_REGISTRATION_FAILED",
                                      PROJECT_FATAL_ASSET_REGISTRATION);
            }
            if(result==PROJECT_CONTROL_ASSET_FAILED)
                ++g_progress.asset_warning_count;
            ++g_project_load.asset_index;
            ++g_progress.done;
            return;
        }
        g_project_load.state=PROJECT_LOAD_INSTALL_CONTROL;
    }
    if(g_project_load.state==PROJECT_LOAD_INSTALL_CONTROL)
    {
        g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_LOAD_APPLY;
        persist_debug_stage(PERSIST_DBG_STAGE_APPLY,0);
        if (project_product_asset_loads_pending() != 0U)
        {
            PROJECT_PRODUCT_FATAL("PROJECT_ASSET_PENDING_AT_COMMIT",
                                  PROJECT_FATAL_PENDING_AT_COMMIT);
        }
        g_persist_dbg.audio_publish_result=1U;
        g_persist_dbg.seq_publish_result=1U;
        if(prepared_audio_control_begin_install(
                restore->prepared_pattern.audio_slot,
                restore->prepared_pattern.audio_generation)==0U)
            PROJECT_PRODUCT_FATAL("PROJECT_AUDIO_BEGIN_FAILED_AFTER_PREPARED",
                                       PROJECT_FATAL_PATTERN_APPLY);
        persistent_pattern_control_commit_prepared_control(
            &restore->prepared_pattern);
        if(project_control_apply_macros(&restore->macros)==0U)
            PROJECT_PRODUCT_FATAL("PROJECT_MACRO_COMMIT_FAILED",
                                  PROJECT_FATAL_PATTERN_APPLY);
        /* Publish a fresh stopped SEQ epoch before AUDIO installs the
         * replacement programs.  No terminal event from the previous
         * Project may be interpreted against the new engine map. */
        persistent_pattern_control_commit_prepared_seq(
            &restore->prepared_pattern,0U);
        g_persist_dbg.seq_publish_result=2U;
        prepared_audio_control_end_install();
        if(prepared_audio_control_publish(
                restore->prepared_pattern.audio_slot,
                restore->prepared_pattern.audio_generation,
                CONTROL_AUDIO_STATE_PROJECT)==0U)
            PROJECT_PRODUCT_FATAL("PROJECT_AUDIO_COMMIT_FAILED",
                                  PROJECT_FATAL_PATTERN_APPLY);
        g_persist_dbg.audio_publish_result=2U;
        persist_debug_publication(1U,1U);
        pattern_live_publish_active(restore->metadata.active_pattern_bank,
            restore->metadata.active_pattern);
        persistent_pattern_control_sync_ui_after_commit();
        ui_macro_interaction_reset();
        restore->prepared_pattern.control_prepared=0U;
        restore->prepared_pattern.audio_prepared=0U;
        restore->prepared_pattern.pattern=NULL;
        project_product_load_finish_success();
        return;
    }
    PROJECT_PRODUCT_FATAL("PROJECT_LOAD_STATE_INVALID",PROJECT_FATAL_STATE);
}

static uint8_t project_product_load_internal(uint8_t slot,
    const char *override_path,const uint8_t *resume_manifest)
{
    const uint8_t recovery_forward=(uint8_t)(
        g_project_load.state==PROJECT_LOAD_FAILED_FORWARD_MEDIA);
    persist_debug_begin(PERSIST_DBG_OP_PROJECT_LOAD,0U,slot);
    persist_debug_stage(PERSIST_DBG_STAGE_POLICY,0);
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_LOAD_DECODE,0U,0U);
    if (project_product_save_busy()!=0U || project_product_load_busy()!=0U
        || project_product_rename_busy()!=0U
        || ((project_replacement_is_active()!=0U)&&(recovery_forward==0U))
        || project_load_allowed()==0U)
    {
        persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);
        if(recovery_forward==0U)
            g_progress=(project_product_progress_t){
                .result=PROJECT_PRODUCT_RESULT_NOT_NOW};
        return 0U;
    }
    if((resume_manifest==NULL&&(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[slot]))
       ||(resume_manifest!=NULL&&slot!=PROJECT_PRODUCT_NO_SLOT
          &&(slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[slot])))
    {
        persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);
        if(recovery_forward==0U)
            g_progress=(project_product_progress_t){.complete=1U,
                .result=PROJECT_PRODUCT_RESULT_INVALID_DOCUMENT};
        return 0U;
    }

    persistence_project_restore_workspace_t *const restore =
        persistence_workspace_acquire_project_restore();
    if (restore == NULL)
    {
        persist_debug_error(PERSIST_DBG_STAGE_WORKSPACE,
                            PERSIST_DBG_ERROR_WORKSPACE);
        if(recovery_forward==0U)
            g_progress=(project_product_progress_t){
                .result=PROJECT_PRODUCT_RESULT_NOT_NOW};
        return 0U;
    }
    persist_codec_project_workspace_t *const workspace =
        &restore->scratch.codec_scratch;
    memset(restore, 0, sizeof(*restore));
    g_progress=(project_product_progress_t){.active=1U,.total=1U,
        .result=PROJECT_PRODUCT_RESULT_IN_PROGRESS};
    g_project_prepare_result_hint=PROJECT_PRODUCT_RESULT_INVALID_DOCUMENT;
    const uint32_t prepare_media_epoch=sd_access_media_epoch();

    char project_path[48];
    persistent_fatfs_file_t file;
    uint8_t ok = (override_path!=NULL)
        ?(uint8_t)(strlen(override_path)<sizeof(project_path))
        :path(project_path, sizeof(project_path), slot);
    if(ok!=0U&&override_path!=NULL)memcpy(project_path,override_path,strlen(override_path)+1U);
    if(ok==0U)
        PROJECT_PRODUCT_FATAL("PROJECT_LOAD_PATH_BUILD_FAILED",
                              PROJECT_FATAL_STATE);
    uint8_t gate_acquired = 0U;
    const project_product_result_t acquire_result=project_prepare_acquire();
    gate_acquired=(acquire_result==PROJECT_PRODUCT_RESULT_IN_PROGRESS)?1U:0U;
    ok=gate_acquired;
    if (gate_acquired == 0U)
    {
        project_product_discard_prepared_candidate(restore,acquire_result,
                                                   recovery_forward);
        return 0U;
    }
    if ((ok != 0U) && (persistent_fatfs_open_read(&file, project_path) == 0U))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        gate_acquired=0U;
        ok = 0U;
    }
    persist_codec_source_t source = {0};
    uint8_t resume_active_loaded=0U;
    if (ok != 0U) source = persistent_fatfs_source(&file);
    persist_codec_result_t result = PERSIST_CODEC_IO_ERROR;
    if (ok != 0U)
    {
        persist_debug_object(PERSIST_DBG_OBJECT_PROJECT,slot,0U);
        persist_debug_stage(PERSIST_DBG_STAGE_DECODE,0);
        persist_codec_project_consumer_t project = {
            .begin_assets=begin_assets,
            .asset_target=asset_target,
            .validate_asset=validate_asset,
            .apply_metadata=apply_metadata,
            .apply_macros=apply_macros,
            .context=restore,
            .asset_capacity=PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY};
        result = persist_codec_decode_project_current(&source,workspace,&project);
        persist_debug_filesystem((int32_t)file.last_result,
                                 (uint32_t)f_tell(&file.file));
    }
    persist_debug_details((uint32_t)result,(ok!=0U)?file.size:0U,
                          restore->asset_count,0U);
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest!=NULL)
    {
        uint8_t header[PERSIST_CODEC_HEADER_BYTES];UINT read=0U;
        if(f_lseek(&file.file,0U)!=FR_OK
           ||f_read(&file.file,header,sizeof(header),&read)!=FR_OK
           ||read!=sizeof(header)
           ||project_resume_le32(&header[16])!=project_resume_le32(&resume_manifest[52]))
            result=PERSIST_CODEC_BAD_CRC;
    }
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest!=NULL)
    {
        for(uint16_t index=0U;index<256U&&result==PERSIST_CODEC_OK;++index)
        {
            if((project_resume_le32(&resume_manifest[16U+4U*(index>>5U)])
                &(UINT32_C(1)<<(index&31U)))==0U)continue;
            char working_path[96],working_tmp[100],working_bak[100];
            persistent_fatfs_file_t working_file;
            if(!project_storage_working_pattern_file(working_path,sizeof(working_path),
                    (uint8_t)(index>>4U),(uint8_t)(index&15U))
               ||!project_side_path_base(working_tmp,sizeof(working_tmp),working_path,"TMP")
               ||!project_side_path_base(working_bak,sizeof(working_bak),working_path,"BAK")
               ||persistent_fatfs_recover_replace(working_path,working_tmp,working_bak)!=FR_OK
               ||!persistent_fatfs_open_read(&working_file,working_path))
            {result=PERSIST_CODEC_IO_ERROR;break;}
            persist_codec_source_t working_source=persistent_fatfs_source(&working_file);
            result=persist_codec_decode_pattern(&working_source,
                (persist_codec_pattern_staging_t *)&restore->working_pattern);
            if(persistent_fatfs_close_result(&working_file)!=FR_OK)
                result=PERSIST_CODEC_IO_ERROR;
        }
    }
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest!=NULL
       &&(restore->metadata.active_pattern_bank!=resume_manifest[12]
          ||restore->metadata.active_pattern!=resume_manifest[13]))
        result=PERSIST_CODEC_BAD_SECTION;
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest!=NULL)
    {
        const uint16_t active_index=(uint16_t)restore->metadata.active_pattern_bank*16U
            +restore->metadata.active_pattern;
        if((project_resume_le32(&resume_manifest[16U+4U*(active_index>>5U)])
            &(UINT32_C(1)<<(active_index&31U)))!=0U)
        {
            char working_path[96];persistent_fatfs_file_t working_file;
            if(project_storage_working_pattern_file(working_path,sizeof(working_path),
                    restore->metadata.active_pattern_bank,restore->metadata.active_pattern)
               &&persistent_fatfs_open_read(&working_file,working_path))
            {
                persist_codec_source_t working_source=persistent_fatfs_source(&working_file);
                result=persist_codec_decode_pattern(&working_source,
                    (persist_codec_pattern_staging_t *)&restore->working_pattern);
                if(persistent_fatfs_close_result(&working_file)!=FR_OK)
                    result=PERSIST_CODEC_IO_ERROR;
                if(result==PERSIST_CODEC_OK)resume_active_loaded=1U;
            }
            else result=PERSIST_CODEC_IO_ERROR;
        }
    }
    if (source.context != NULL
            && persistent_fatfs_close_result(&file)!=FR_OK)ok=0U;
    if (gate_acquired != 0U)
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest==NULL)
    {
        if(pattern_control_bank_validate_project(slot)==0U)
            result=PERSIST_CODEC_IO_ERROR;
    }
    if(ok!=0U&&result==PERSIST_CODEC_OK&&resume_manifest!=NULL
       &&slot!=PROJECT_PRODUCT_NO_SLOT&&pattern_control_bank_validate_project(slot)==0U)
        result=PERSIST_CODEC_IO_ERROR;
    if(ok!=0U&&result==PERSIST_CODEC_OK)
    {
        pattern_control_bank_project_load_result_t pattern_result=
            PATTERN_CONTROL_BANK_PROJECT_LOAD_EMPTY;
        const uint16_t active_index=(uint16_t)restore->metadata.active_pattern_bank*16U
            +restore->metadata.active_pattern;
        if(resume_manifest!=NULL
           &&(project_resume_le32(&resume_manifest[16U+4U*(active_index>>5U)])
              &(UINT32_C(1)<<(active_index&31U)))!=0U)
        {
            pattern_result=(resume_active_loaded!=0U)
                ?PATTERN_CONTROL_BANK_PROJECT_LOAD_OK
                :PATTERN_CONTROL_BANK_PROJECT_LOAD_ERROR;
        }
        else if(slot!=PROJECT_PRODUCT_NO_SLOT)
            pattern_result=pattern_control_bank_load_project(slot,
                restore->metadata.active_pattern_bank,
                restore->metadata.active_pattern,&restore->working_pattern);
        if(pattern_result==PATTERN_CONTROL_BANK_PROJECT_LOAD_OK)
        {
            restore->working_valid=1U;
        }
        else if(pattern_result==PATTERN_CONTROL_BANK_PROJECT_LOAD_EMPTY)
        {
            if(pattern_live_build_slot_default(&restore->working_pattern,
                    restore->metadata.active_pattern_bank,
                    restore->metadata.active_pattern)==0U)
                result=PERSIST_CODEC_IO_ERROR;
            else
            {
                restore->working_valid=1U;
            }
        }
        else result=PERSIST_CODEC_IO_ERROR;
    }
    if(ok==0U || (result==PERSIST_CODEC_IO_ERROR
        && (file.last_result!=FR_OK
            ||sd_access_storage_status()!=SD_STORAGE_STATUS_READY
            ||sd_access_media_epoch()!=prepare_media_epoch
            ||g_project_prepare_result_hint==PROJECT_PRODUCT_RESULT_MEDIA_ERROR)))
    {
        persist_debug_error(PERSIST_DBG_STAGE_READ,PERSIST_DBG_ERROR_FILESYSTEM);
        project_product_discard_prepared_candidate(restore,
            PROJECT_PRODUCT_RESULT_MEDIA_ERROR,recovery_forward);
        return 0U;
    }
    if(result!=PERSIST_CODEC_OK
        ||restore->working_valid==0U||restore->macros_valid==0U
        ||restore->asset_count>PERSISTENCE_PROJECT_RESTORE_ASSET_CAPACITY
        ||persist_codec_validate_macros(&restore->macros)!=PERSIST_CODEC_OK
        ||project_product_prevalidate_candidate(restore)==0U)
    {
        persist_debug_error(PERSIST_DBG_STAGE_DECODE,(int32_t)result);
        project_product_discard_prepared_candidate(restore,
            PROJECT_PRODUCT_RESULT_INVALID_DOCUMENT,recovery_forward);
        return 0U;
    }
    const persist_codec_result_t pattern_prepare=
        persistent_pattern_control_prepare(&restore->working_pattern,
            &restore->prepared_pattern,restore->scratch.groove_build.track);
    if(pattern_prepare!=PERSIST_CODEC_OK)
    {
        persist_debug_error(PERSIST_DBG_STAGE_VALIDATE,(int32_t)pattern_prepare);
        project_product_discard_prepared_candidate(restore,
            (pattern_prepare==PERSIST_CODEC_IO_ERROR)
                ?PROJECT_PRODUCT_RESULT_NOT_NOW
                :PROJECT_PRODUCT_RESULT_INVALID_DOCUMENT,
            recovery_forward);
        return 0U;
    }
    /* P1 ends here: Project metadata and the active Pattern are prepared while
     * the live Project remains untouched. */
    g_persist_dbg.project_phase=PERSIST_DBG_PROJECT_PHASE_LOAD_STAGED;
    project_product_begin_prepare(restore,slot,recovery_forward);
    if(resume_manifest!=NULL)
    {
        g_project_load.resume_mode=1U;
        g_project_load.resume_base_kind=resume_manifest[5];
        for(uint8_t i=0U;i<8U;++i)
            g_project_load.resume_dirty[i]=project_resume_le32(&resume_manifest[16U+4U*i]);
    }
    persist_debug_stage(PERSIST_DBG_STAGE_BANK_STAGE,0);
    return 1U;
}

uint8_t project_product_load(uint8_t slot)
{
    return project_product_load_internal(slot,NULL,NULL);
}

uint8_t project_product_delete(uint8_t slot)
{
    if(project_replacement_is_active()!=0U||project_product_save_busy()!=0U
        ||project_product_load_busy()!=0U||project_product_rename_busy()!=0U
        ||slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;
    char source[48],transaction[64];FRESULT result=FR_INVALID_NAME;
    if(project_storage_project_dir(source,sizeof(source),slot)
        &&project_storage_project_delete_transaction_dir(transaction,
            sizeof(transaction),slot))
    {
        (void)project_mkdir_path(project_storage_internal_root);
        (void)project_mkdir_path(project_storage_transactions_root);
        (void)project_remove_tree(transaction);
        result=f_rename(source,transaction);
        if(result==FR_OK)(void)project_remove_tree(transaction);
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    if(result!=FR_OK && result!=FR_NO_FILE && result!=FR_NO_PATH)return 0U;
    g_present[slot]=0U;
    memset(&g_project_metadata[slot],0,sizeof(g_project_metadata[slot]));
    if(g_active_valid!=0U && g_active==slot)
    {
        g_active_valid=0U;
        pattern_control_bank_deactivate_project();
        memset(&g_current_metadata,0,sizeof(g_current_metadata));
        boot_context_sd_clear();
    }
    return 1U;
}

uint8_t project_product_new_blank(void)
{
    const uint8_t recovery_forward=(uint8_t)(
        g_project_load.state==PROJECT_LOAD_FAILED_FORWARD_MEDIA);
    persist_debug_begin(PERSIST_DBG_OP_PROJECT_BLANK,0U,0U);
    persist_debug_project(PERSIST_DBG_PROJECT_PHASE_BLANK_BUILD,0U,0U);
    if(((project_replacement_is_active()!=0U)&&(recovery_forward==0U))
       ||project_product_save_busy()!=0U
       ||project_product_rename_busy()!=0U
       ||project_product_load_busy()!=0U||project_load_allowed()==0U)
    {
        persist_debug_error(PERSIST_DBG_STAGE_POLICY,PERSIST_DBG_ERROR_POLICY);
        if(recovery_forward==0U)
            g_progress=(project_product_progress_t){
                .result=PROJECT_PRODUCT_RESULT_NOT_NOW};
        return 0U;
    }
    persistence_project_restore_workspace_t *const restore=
        persistence_workspace_acquire_project_restore();
    if(restore==NULL){persist_debug_error(PERSIST_DBG_STAGE_WORKSPACE,PERSIST_DBG_ERROR_WORKSPACE);if(recovery_forward==0U)g_progress=(project_product_progress_t){.result=PROJECT_PRODUCT_RESULT_NOT_NOW};return 0U;}
    uint8_t built=0U;
    const project_product_result_t acquire_result=project_prepare_acquire();
    if(acquire_result==PROJECT_PRODUCT_RESULT_IN_PROGRESS)
    {
        built=project_product_build_default_candidate(restore);
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    }
    else
    {
        project_product_discard_prepared_candidate(restore,acquire_result,
                                                   recovery_forward);
        return 0U;
    }
    if(built==0U)
    {
        project_product_discard_prepared_candidate(restore,
            PROJECT_PRODUCT_RESULT_MEDIA_ERROR,recovery_forward);
        return 0U;
    }
    if(project_product_prevalidate_candidate(restore)==0U)
        PROJECT_PRODUCT_FATAL("PROJECT_BLANK_PREVALIDATION_FAILED",
                              PROJECT_FATAL_BLANK_BUILD);
    const persist_codec_result_t pattern_prepare=
        persistent_pattern_control_prepare(&restore->working_pattern,
            &restore->prepared_pattern,restore->scratch.groove_build.track);
    if(pattern_prepare!=PERSIST_CODEC_OK)
    {
        if(pattern_prepare!=PERSIST_CODEC_IO_ERROR)
            PROJECT_PRODUCT_FATAL("PROJECT_BLANK_PATTERN_PREPARE_FAILED",
                                  PROJECT_FATAL_BLANK_BUILD);
        project_product_discard_prepared_candidate(restore,
            PROJECT_PRODUCT_RESULT_NOT_NOW,recovery_forward);
        return 0U;
    }
    g_progress=(project_product_progress_t){.active=1U,.total=1U,
        .result=PROJECT_PRODUCT_RESULT_IN_PROGRESS};
    project_product_begin_prepare(restore,PROJECT_PRODUCT_NO_SLOT,
                                  recovery_forward);
    return 1U;
}

static project_product_boot_restore_result_t project_product_restore_boot_defaults(void)
{
    g_active=0U;
    g_active_valid=0U;
    memset(&g_current_metadata,0,sizeof(g_current_metadata));
    pattern_control_bank_deactivate_project();
    boot_context_sd_clear();
    g_progress=(project_product_progress_t){.complete=1U,
        .result=PROJECT_PRODUCT_RESULT_SUCCESS};
    return PROJECT_PRODUCT_BOOT_RESTORE_DEFAULTS_READY;
}

project_product_boot_restore_result_t project_product_restore_boot(void)
{
    if (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        return PROJECT_PRODUCT_BOOT_RESTORE_FAILED;

    uint8_t resume[PROJECT_RESUME_MANIFEST_BYTES];
    char resume_project[80];
    uint8_t resume_valid=0U;
    if(acquire())
    {
        resume_valid=project_resume_latest_mounted(resume);
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    }
    if(resume_valid!=0U
       &&project_storage_resume_project_file(resume_project,sizeof(resume_project),resume[7]))
    {
        const uint8_t base_slot=(resume[5]==PATTERN_WORKING_BASE_PROJECT)
            ?resume[6]:PROJECT_PRODUCT_NO_SLOT;
        if(project_product_load_internal(base_slot,resume_project,resume)!=0U)
            return PROJECT_PRODUCT_BOOT_RESTORE_PROJECT_READY;
    }

    boot_context_sd_data_t context;
    if (!boot_context_sd_load(&context))
        return project_product_restore_boot_defaults();
    if (context.active_project_slot >= PROJECT_PRODUCT_SLOT_COUNT
        || g_present[context.active_project_slot] == 0U
        || g_project_metadata[context.active_project_slot].name[0]=='\0')
        return project_product_restore_boot_defaults();
    return (project_product_load(context.active_project_slot) != 0U)
        ? PROJECT_PRODUCT_BOOT_RESTORE_PROJECT_READY
        : PROJECT_PRODUCT_BOOT_RESTORE_FAILED;
}
uint8_t project_product_get_progress(project_product_progress_t*out){if(out==NULL)return 0U;*out=g_progress;return 1U;}
