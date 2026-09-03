#include "Storage/project_product.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/audio_recorder.h"
#include "Sampler/sample_stream_transport.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/sd_access_gate.h"
#include "Platform/memory_layout.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/project_control.h"
#include "Storage/asset_ref.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/sample_cache.h"
#include "Sampler/sampler_ram_pool.h"
#include "Sampler/wavetable_pool.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

_Static_assert((2U * SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
                    + MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
                   <= PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY,
               "Project Save asset snapshot capacity is too small");

static uint8_t g_present[PROJECT_PRODUCT_SLOT_COUNT];
static project_product_progress_t g_progress;
static project_product_save_error_t g_save_error;
static volatile project_product_command_t g_storage_request;
static volatile uint8_t g_storage_slot;
static int32_t g_save_detail;

static void project_capture_metadata(persist_codec_project_metadata_t *out)
{
    if (out == NULL) return;
    out->active_pattern_bank = 0U;
    out->active_pattern = 0U;
    (void)pattern_live_get_active(&out->active_pattern_bank,
                                  &out->active_pattern);
}

typedef enum
{
    PROJECT_SAVE_IDLE = 0,
    PROJECT_SAVE_MOUNT,
    PROJECT_SAVE_MKDIR_BRICK,
    PROJECT_SAVE_MKDIR_PROJECT,
    PROJECT_SAVE_RECOVER,
    PROJECT_SAVE_OPEN,
    PROJECT_SAVE_QUEUE_DOCUMENT_PLACEHOLDER,
    PROJECT_SAVE_ENCODE_CORE,
    PROJECT_SAVE_QUEUE_CORE,
    PROJECT_SAVE_ENCODE_ASSETS,
    PROJECT_SAVE_QUEUE_ASSETS,
    PROJECT_SAVE_ENCODE_MACROS,
    PROJECT_SAVE_QUEUE_MACROS,
    PROJECT_SAVE_QUEUE_BANK_HEADER,
    PROJECT_SAVE_QUEUE_BANK_COUNT,
    PROJECT_SAVE_NEXT_PATTERN,
    PROJECT_SAVE_OPEN_PATTERN,
    PROJECT_SAVE_READ_PATTERN,
    PROJECT_SAVE_CLOSE_PATTERN,
    PROJECT_SAVE_DECODE_PATTERN,
    PROJECT_SAVE_ENCODE_PATTERN,
    PROJECT_SAVE_QUEUE_PATTERN,
    PROJECT_SAVE_WRITE,
    PROJECT_SAVE_PATCH_BANK_HEADER,
    PROJECT_SAVE_CRC_SEEK,
    PROJECT_SAVE_CRC_READ,
    PROJECT_SAVE_WRITE_DOCUMENT_HEADER,
    PROJECT_SAVE_SYNC,
    PROJECT_SAVE_CLOSE,
    PROJECT_SAVE_COMMIT,
    PROJECT_SAVE_CLEAN_PATTERN_CLOSE,
    PROJECT_SAVE_CLEAN_PROJECT_CLOSE,
    PROJECT_SAVE_CLEAN_TEMP,
    PROJECT_SAVE_DONE
} project_save_state_t;

typedef struct
{
    project_save_state_t state;
    project_save_state_t after_write;
    persistence_project_save_workspace_t *workspace;
    persistent_fatfs_file_t project_file;
    persistent_fatfs_file_t pattern_file;
    persist_codec_project_metadata_t metadata;
    const uint8_t *encoded_data;
    uint32_t encoded_size;
    const uint8_t *write_data;
    uint32_t write_size;
    uint32_t write_offset;
    uint32_t file_offset;
    uint32_t bank_header_offset;
    uint32_t pattern_encoded_size;
    uint32_t pattern_read_offset;
    uint32_t crc;
    uint32_t crc_remaining;
    uint32_t media_epoch;
    uint16_t pattern_ordinal;
    uint8_t slot;
    uint8_t expected_bank;
    uint8_t expected_pattern;
    uint8_t project_open;
    uint8_t pattern_open;
    uint8_t result_ready;
    uint8_t success;
    uint8_t header[32];
    char final_path[48];
    char temporary_path[48];
    char backup_path[48];
    char pattern_path[48];
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
    PROJECT_LOAD_WAIT_SAFE,
    PROJECT_LOAD_WAIT_MULTI,
    PROJECT_LOAD_ASSETS,
    PROJECT_LOAD_WAIT_STREAM,
    PROJECT_LOAD_WAIT_RAM,
    PROJECT_LOAD_WAIT_WAVETABLE,
    PROJECT_LOAD_COMMIT,
    PROJECT_LOAD_FAILED
} project_load_state_t;

typedef struct
{
    volatile project_load_state_t state;
    uint16_t asset_index;
    uint16_t asset_count;
    uint8_t slot;
    uint8_t result;
    uint8_t quiesce_requested;
} project_load_runtime_t;

static persistence_project_restore_workspace_t *g_project_load_workspace;
static volatile uint8_t g_project_load_control_ready;
static volatile uint8_t g_project_load_control_done;
static volatile uint8_t g_project_load_control_result;

static uint8_t project_product_asset_valid(const persist_control_asset_ref_t *asset);

#define PROJECT_PRODUCT_NO_SLOT ((uint8_t)0xFFU)

STORAGE_STATE_SDRAM static project_load_runtime_t g_project_load;

static uint8_t path(char*out,uint32_t size,uint8_t slot){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.B6C",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t side_path(char*out,uint32_t size,uint8_t slot,const char*extension){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.%s",slot,extension);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return 0U;}return 1U;}
static uint8_t ensure_directory(void){FRESULT r=f_mkdir("0:/BRICK");if(r!=FR_OK&&r!=FR_EXIST)return 0U;r=f_mkdir("0:/BRICK/PROJECT");return(r==FR_OK||r==FR_EXIST)?1U:0U;}

void project_product_refresh_slots(void){if(project_replacement_is_active()!=0U||project_product_save_busy()!=0U||project_product_load_busy()!=0U)return;memset(g_present,0,sizeof(g_present));if(!acquire())return;if(!ensure_directory()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return;}for(uint8_t s=0U;s<PROJECT_PRODUCT_SLOT_COUNT;++s){char x[48],tmp[48],bak[48];FILINFO i;if(path(x,sizeof(x),s)&&side_path(tmp,sizeof(tmp),s,"TMP")&&side_path(bak,sizeof(bak),s,"BAK")){(void)persistent_fatfs_recover_replace(x,tmp,bak);if(f_stat(x,&i)==FR_OK)g_present[s]=1U;}}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
void project_product_init(void){memset(&g_progress,0,sizeof(g_progress));memset(&g_project_save,0,sizeof(g_project_save));memset(&g_project_load,0,sizeof(g_project_load));g_project_load_workspace=NULL;g_storage_request=PROJECT_PRODUCT_COMMAND_NONE;g_storage_slot=0U;g_project_load_control_ready=0U;g_project_load_control_done=0U;g_project_load_control_result=0U;}
void project_product_storage_init(void){project_product_refresh_slots();}
uint8_t project_product_list_slots(uint8_t*out,uint8_t cap){uint8_t n=0U;if(out==NULL)return 0U;for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT&&n<cap;++s)if(g_present[s])out[n++]=s;return n;}
uint8_t project_product_slot_present(uint8_t s){return(s<PROJECT_PRODUCT_SLOT_COUNT)?g_present[s]:0U;}

static uint8_t project_memory_write(void *context,const uint8_t *data,uint32_t length)
{
    project_memory_io_t *const memory=context;
    if(memory==NULL||data==NULL||length>memory->capacity-memory->position)return 0U;
    memcpy(&memory->data[memory->position],data,length);memory->position+=length;return 1U;
}

static uint8_t project_memory_read(void *context,uint8_t *data,uint32_t length)
{
    project_memory_io_t *const memory=context;
    if(memory==NULL||data==NULL||length>memory->capacity-memory->position)return 0U;
    memcpy(data,&memory->data[memory->position],length);memory->position+=length;return 1U;
}

static uint8_t project_memory_reset(void *context){project_memory_io_t*m=context;if(m==NULL)return 0U;m->position=0U;return 1U;}
static uint8_t project_memory_size(void *context,uint32_t*out){project_memory_io_t*m=context;if(m==NULL||out==NULL)return 0U;*out=m->capacity;return 1U;}

static sd_scheduler_background_admission_t project_save_admit(
    sd_scheduler_background_kind_t kind,uint32_t bytes)
{
    const sd_scheduler_background_request_t request={bytes,g_project_save.media_epoch,kind};
    return sd_scheduler_runtime_background_try_begin(&request);
}

static void project_save_finish(uint8_t success)
{
    if(g_project_save.workspace!=NULL)
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);
    g_project_save.workspace=NULL;g_project_save.project_open=0U;g_project_save.pattern_open=0U;
    g_project_save.success=(success!=0U)?1U:0U;g_project_save.result_ready=1U;
    g_project_save.state=PROJECT_SAVE_DONE;g_progress.active=0U;g_progress.complete=1U;g_progress.result=(success!=0U)?PROJECT_PRODUCT_RESULT_SUCCESS:PROJECT_PRODUCT_RESULT_FAILED;
    if(success!=0U){g_present[g_project_save.slot]=1U;(void)boot_context_flash_commit(g_project_save.slot);}
}

static void project_save_fail(project_product_save_error_t error,int32_t detail)
{
    if(g_save_error==PROJECT_PRODUCT_SAVE_ERROR_NONE){g_save_error=error;g_save_detail=detail;}
    if(g_project_save.pattern_open!=0U)g_project_save.state=PROJECT_SAVE_CLEAN_PATTERN_CLOSE;
    else if(g_project_save.project_open!=0U)g_project_save.state=PROJECT_SAVE_CLEAN_PROJECT_CLOSE;
    else g_project_save.state=PROJECT_SAVE_CLEAN_TEMP;
}

static void project_save_queue_write(const uint8_t *data,uint32_t size,project_save_state_t next)
{
    g_project_save.write_data=data;g_project_save.write_size=size;g_project_save.write_offset=0U;
    g_project_save.after_write=next;g_project_save.state=PROJECT_SAVE_WRITE;
}

static uint8_t project_save_encode_core(void)
{
    project_memory_io_t memory={(uint8_t*)&g_project_save.workspace->pattern_record,
        sizeof(g_project_save.workspace->pattern_record),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_core_payload(
        &g_project_save.metadata,&g_project_save.workspace->working_pattern,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_CORE,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=(const uint8_t*)&g_project_save.workspace->pattern_record;
    g_project_save.encoded_size=bytes;return 1U;
}

static uint8_t project_save_encode_assets(void)
{
    project_memory_io_t memory={(uint8_t*)&g_project_save.workspace->pattern_record,
        sizeof(g_project_save.workspace->pattern_record),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_assets_payload(
        g_project_save.workspace->assets,g_project_save.metadata.asset_count,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_ASSETS,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=(const uint8_t*)&g_project_save.workspace->pattern_record;
    g_project_save.encoded_size=bytes;return 1U;
}

static uint8_t project_save_encode_macros(void)
{
    project_memory_io_t memory={(uint8_t*)&g_project_save.workspace->pattern_record,
        sizeof(g_project_save.workspace->pattern_record),0U};
    const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
    const persist_codec_result_t result=persist_codec_encode_project_macros_payload(
        &g_project_save.workspace->macros,&sink,&bytes);
    if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return 0U;}
    if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_MACROS,bytes,g_project_save.header)){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);return 0U;}
    g_project_save.encoded_data=(const uint8_t*)&g_project_save.workspace->pattern_record;
    g_project_save.encoded_size=bytes;return 1U;
}

uint8_t project_product_save(uint8_t slot)
{
    g_save_error=PROJECT_PRODUCT_SAVE_ERROR_NONE;g_save_detail=0;
    if(slot>=PROJECT_PRODUCT_SLOT_COUNT){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT;return 0U;}
    if(project_replacement_is_active()!=0U
        || project_product_save_busy()!=0U||project_product_load_busy()!=0U)
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY;return 0U;}
    if(g_project_save.state==PROJECT_SAVE_DONE)memset(&g_project_save,0,sizeof(g_project_save));
    persistence_project_save_workspace_t *const workspace = persistence_workspace_acquire_project_save();
    if(workspace==NULL){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY;return 0U;}
    persist_codec_result_t codec_result=persistent_pattern_control_capture(&workspace->working_pattern);
    if(codec_result!=PERSIST_CODEC_OK){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;g_save_detail=(int32_t)codec_result;persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);return 0U;}
    memset(&g_project_save,0,sizeof(g_project_save));g_project_save.workspace=workspace;g_project_save.slot=slot;
    project_capture_metadata(&g_project_save.metadata);
    g_project_save.metadata.pattern_count=pattern_control_bank_count();
    g_project_save.metadata.asset_count=project_control_asset_count();
    if(g_project_save.metadata.asset_count>PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY
            || !project_control_capture_macros(&workspace->macros))
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    for(uint16_t i=0U;i<g_project_save.metadata.asset_count;++i)
        if(!project_control_get_asset_ordinal(i,&workspace->assets[i]))
        {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    if(!path(g_project_save.final_path,sizeof(g_project_save.final_path),slot)
            || !side_path(g_project_save.temporary_path,sizeof(g_project_save.temporary_path),slot,"TMP")
            || !side_path(g_project_save.backup_path,sizeof(g_project_save.backup_path),slot,"BAK"))
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_ARGUMENT;persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);memset(&g_project_save,0,sizeof(g_project_save));return 0U;}
    g_project_save.media_epoch=sd_access_media_epoch();g_project_save.state=PROJECT_SAVE_MOUNT;
    g_progress=(project_product_progress_t){1U,0U,0U,1U,PROJECT_PRODUCT_RESULT_IN_PROGRESS};return 1U;
}

uint8_t project_product_save_busy(void){return(g_project_save.state!=PROJECT_SAVE_IDLE&&g_project_save.state!=PROJECT_SAVE_DONE)?1U:0U;}

uint8_t project_product_save_take_result(uint8_t *slot,uint8_t *success)
{
    if(g_project_save.state!=PROJECT_SAVE_DONE||g_project_save.result_ready==0U)return 0U;
    if(slot!=NULL)*slot=g_project_save.slot;
    if(success!=NULL)*success=g_project_save.success;
    memset(&g_project_save,0,sizeof(g_project_save));return 1U;
}

void project_product_save_service(void)
{
    if(g_project_save.state==PROJECT_SAVE_IDLE||g_project_save.state==PROJECT_SAVE_DONE)return;

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
            project_save_queue_write(g_project_save.encoded_data,g_project_save.encoded_size,PROJECT_SAVE_QUEUE_BANK_HEADER);
            return;
        case PROJECT_SAVE_QUEUE_BANK_HEADER:
            g_project_save.bank_header_offset=g_project_save.file_offset;
            memset(g_project_save.header,0,8U);
            project_save_queue_write(g_project_save.header,8U,PROJECT_SAVE_QUEUE_BANK_COUNT);
            return;
        case PROJECT_SAVE_QUEUE_BANK_COUNT:
            g_project_save.header[0]=(uint8_t)g_project_save.metadata.pattern_count;
            g_project_save.header[1]=(uint8_t)(g_project_save.metadata.pattern_count>>8U);
            project_save_queue_write(g_project_save.header,2U,PROJECT_SAVE_NEXT_PATTERN);
            return;
        case PROJECT_SAVE_NEXT_PATTERN:
            if(g_project_save.pattern_ordinal>=g_project_save.metadata.pattern_count)
            {g_project_save.state=PROJECT_SAVE_PATCH_BANK_HEADER;return;}
            if(!pattern_control_bank_get_ordinal_project_path(g_project_save.pattern_ordinal,
                    g_project_save.pattern_path,sizeof(g_project_save.pattern_path),
                    &g_project_save.expected_bank,&g_project_save.expected_pattern))
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,0);return;}
            g_project_save.state=PROJECT_SAVE_OPEN_PATTERN;return;
        case PROJECT_SAVE_DECODE_PATTERN:
        {
            project_memory_io_t memory={(uint8_t*)&g_project_save.workspace->working_pattern,
                g_project_save.pattern_encoded_size,0U};
            const persist_codec_source_t source={project_memory_read,project_memory_reset,project_memory_size,&memory};
            memset(&g_project_save.workspace->pattern_record,0,sizeof(g_project_save.workspace->pattern_record));
            const persist_codec_result_t result=persist_codec_decode_pattern(&source,
                (persist_codec_pattern_staging_t*)&g_project_save.workspace->pattern_record.content);
            if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)result);return;}
            g_project_save.workspace->pattern_record.bank=g_project_save.expected_bank;
            g_project_save.workspace->pattern_record.pattern=g_project_save.expected_pattern;
            g_project_save.workspace->pattern_record.present=1U;
            g_project_save.state=PROJECT_SAVE_ENCODE_PATTERN;return;
        }
        case PROJECT_SAVE_ENCODE_PATTERN:
        {
            project_memory_io_t memory={(uint8_t*)&g_project_save.workspace->working_pattern,
                sizeof(g_project_save.workspace->working_pattern),0U};
            const persist_codec_sink_t sink={project_memory_write,&memory};uint32_t bytes=0U;
            const persist_codec_result_t result=persist_codec_encode_project_pattern_record_payload(
                &g_project_save.workspace->pattern_record,&sink,&bytes);
            if(result!=PERSIST_CODEC_OK){project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)result);return;}
            g_project_save.encoded_data=(const uint8_t*)&g_project_save.workspace->working_pattern;
            g_project_save.encoded_size=bytes;g_project_save.state=PROJECT_SAVE_QUEUE_PATTERN;return;
        }
        case PROJECT_SAVE_QUEUE_PATTERN:
            g_project_save.pattern_ordinal++;
            project_save_queue_write(g_project_save.encoded_data,g_project_save.encoded_size,PROJECT_SAVE_NEXT_PATTERN);
            return;
        default:
            break;
    }

    uint32_t chunk=0U;sd_scheduler_background_kind_t kind=SD_SCHEDULER_BACKGROUND_METADATA;
    if(g_project_save.state==PROJECT_SAVE_WRITE)
    {chunk=g_project_save.write_size-g_project_save.write_offset;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    else if(g_project_save.state==PROJECT_SAVE_READ_PATTERN)
    {chunk=g_project_save.pattern_encoded_size-g_project_save.pattern_read_offset;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    else if(g_project_save.state==PROJECT_SAVE_CRC_READ)
    {chunk=g_project_save.crc_remaining;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    const sd_scheduler_background_admission_t admission=project_save_admit(kind,chunk);
    if(admission==SD_SCHEDULER_BACKGROUND_NOT_NOW)return;
    if(admission!=SD_SCHEDULER_BACKGROUND_GO)
    {g_save_error=PROJECT_PRODUCT_SAVE_ERROR_MEDIA_CHANGED;g_save_detail=0;project_save_finish(0U);return;}

    FRESULT fr=FR_OK;UINT transferred=0U;
    switch(g_project_save.state)
    {
        case PROJECT_SAVE_MOUNT:
            if(!sd_access_fs_mount_if_needed())project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY,0);
            else g_project_save.state=PROJECT_SAVE_MKDIR_BRICK;
            break;
        case PROJECT_SAVE_MKDIR_BRICK:
            fr=f_mkdir("0:/BRICK");if(fr!=FR_OK&&fr!=FR_EXIST)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_MKDIR_PROJECT;
            break;
        case PROJECT_SAVE_MKDIR_PROJECT:
            fr=f_mkdir("0:/BRICK/PROJECT");if(fr!=FR_OK&&fr!=FR_EXIST)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_DIRECTORY,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_RECOVER;
            break;
        case PROJECT_SAVE_RECOVER:
            fr=persistent_fatfs_recover_replace(g_project_save.final_path,g_project_save.temporary_path,g_project_save.backup_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_OPEN;
            break;
        case PROJECT_SAVE_OPEN:
            memset(&g_project_save.project_file,0,sizeof(g_project_save.project_file));
            fr=f_open(&g_project_save.project_file.file,g_project_save.temporary_path,
                      FA_CREATE_ALWAYS|FA_WRITE|FA_READ);
            g_project_save.project_file.last_result=fr;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_OPEN,(int32_t)fr);
            else{g_project_save.project_open=1U;g_project_save.file_offset=0U;g_project_save.state=PROJECT_SAVE_QUEUE_DOCUMENT_PLACEHOLDER;}
            break;
        case PROJECT_SAVE_WRITE:
            fr=f_write(&g_project_save.project_file.file,
                &g_project_save.write_data[g_project_save.write_offset],chunk,&transferred);
            if(fr!=FR_OK||transferred!=chunk)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(fr!=FR_OK)?(int32_t)fr:-1);
            else{g_project_save.write_offset+=chunk;g_project_save.file_offset+=chunk;g_progress.done=g_project_save.file_offset;if(g_project_save.write_offset==g_project_save.write_size)g_project_save.state=g_project_save.after_write;}
            break;
        case PROJECT_SAVE_OPEN_PATTERN:
            if(!persistent_fatfs_open_read(&g_project_save.pattern_file,g_project_save.pattern_path))
                project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)g_project_save.pattern_file.last_result);
            else if(g_project_save.pattern_file.size<PERSIST_CODEC_HEADER_BYTES||g_project_save.pattern_file.size>sizeof(g_project_save.workspace->working_pattern))
            {g_project_save.pattern_open=1U;project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)PERSIST_CODEC_BAD_LENGTH);}
            else{g_project_save.pattern_open=1U;g_project_save.pattern_encoded_size=g_project_save.pattern_file.size;g_project_save.pattern_read_offset=0U;g_project_save.state=PROJECT_SAVE_READ_PATTERN;}
            break;
        case PROJECT_SAVE_READ_PATTERN:
            fr=f_read(&g_project_save.pattern_file.file,
                &((uint8_t*)&g_project_save.workspace->working_pattern)[g_project_save.pattern_read_offset],chunk,&transferred);
            if(fr!=FR_OK||transferred!=chunk)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(fr!=FR_OK)?(int32_t)fr:-1);
            else{g_project_save.pattern_read_offset+=chunk;if(g_project_save.pattern_read_offset==g_project_save.pattern_encoded_size)g_project_save.state=PROJECT_SAVE_CLOSE_PATTERN;}
            break;
        case PROJECT_SAVE_CLOSE_PATTERN:
            fr=persistent_fatfs_close_result(&g_project_save.pattern_file);g_project_save.pattern_open=0U;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_PATTERN,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_DECODE_PATTERN;
            break;
        case PROJECT_SAVE_PATCH_BANK_HEADER:
        {
            const uint32_t bank_bytes=g_project_save.file_offset-(g_project_save.bank_header_offset+8U);
            if(!persist_codec_build_project_section_header(PERSIST_CODEC_PROJECT_SECTION_BANK,bank_bytes,g_project_save.header))
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);break;}
            fr=f_lseek(&g_project_save.project_file.file,g_project_save.bank_header_offset);
            if(fr==FR_OK)fr=f_write(&g_project_save.project_file.file,g_project_save.header,8U,&transferred);
            if(fr==FR_OK&&transferred==8U)fr=f_lseek(&g_project_save.project_file.file,g_project_save.file_offset);
            if(fr!=FR_OK||transferred!=8U)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)fr);
            else g_project_save.state=PROJECT_SAVE_CRC_SEEK;
            break;
        }
        case PROJECT_SAVE_CRC_SEEK:
            fr=f_lseek(&g_project_save.project_file.file,PERSIST_CODEC_HEADER_BYTES);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)fr);
            else{g_project_save.crc=0xFFFFFFFFUL;g_project_save.crc_remaining=g_project_save.file_offset-PERSIST_CODEC_HEADER_BYTES;g_project_save.state=PROJECT_SAVE_CRC_READ;}
            break;
        case PROJECT_SAVE_CRC_READ:
            fr=f_read(&g_project_save.project_file.file,(uint8_t*)&g_project_save.workspace->working_pattern,chunk,&transferred);
            if(fr!=FR_OK||transferred!=chunk)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(fr!=FR_OK)?(int32_t)fr:-1);
            else{g_project_save.crc=persist_codec_crc32_update(g_project_save.crc,(const uint8_t*)&g_project_save.workspace->working_pattern,chunk);g_project_save.crc_remaining-=chunk;if(g_project_save.crc_remaining==0U)g_project_save.state=PROJECT_SAVE_WRITE_DOCUMENT_HEADER;}
            break;
        case PROJECT_SAVE_WRITE_DOCUMENT_HEADER:
            if(!persist_codec_build_project_document_header(g_project_save.file_offset,~g_project_save.crc,g_project_save.header))
            {project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);break;}
            fr=f_lseek(&g_project_save.project_file.file,0U);
            if(fr==FR_OK)fr=f_write(&g_project_save.project_file.file,g_project_save.header,PERSIST_CODEC_HEADER_BYTES,&transferred);
            if(fr!=FR_OK||transferred!=PERSIST_CODEC_HEADER_BYTES)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_SYNC;
            break;
        case PROJECT_SAVE_SYNC:
            fr=f_sync(&g_project_save.project_file.file);if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_SYNC,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_CLOSE;
            break;
        case PROJECT_SAVE_CLOSE:
            fr=persistent_fatfs_close_result(&g_project_save.project_file);g_project_save.project_open=0U;
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CLOSE,(int32_t)fr);else g_project_save.state=PROJECT_SAVE_COMMIT;
            break;
        case PROJECT_SAVE_COMMIT:
            fr=persistent_fatfs_commit_replace(g_project_save.final_path,g_project_save.temporary_path,g_project_save.backup_path);
            if(fr!=FR_OK)project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_REPLACE,(int32_t)fr);
            else{g_progress.total=g_project_save.file_offset;g_progress.done=g_project_save.file_offset;project_save_finish(1U);}
            break;
        case PROJECT_SAVE_CLEAN_PATTERN_CLOSE:
            (void)persistent_fatfs_close_result(&g_project_save.pattern_file);g_project_save.pattern_open=0U;
            g_project_save.state=(g_project_save.project_open!=0U)?PROJECT_SAVE_CLEAN_PROJECT_CLOSE:PROJECT_SAVE_CLEAN_TEMP;
            break;
        case PROJECT_SAVE_CLEAN_PROJECT_CLOSE:
            (void)persistent_fatfs_close_result(&g_project_save.project_file);g_project_save.project_open=0U;g_project_save.state=PROJECT_SAVE_CLEAN_TEMP;
            break;
        case PROJECT_SAVE_CLEAN_TEMP:
            (void)f_unlink(g_project_save.temporary_path);project_save_finish(0U);
            break;
        default:
            project_save_fail(PROJECT_PRODUCT_SAVE_ERROR_CODEC,0);
            break;
    }
    sd_scheduler_runtime_background_end();
}

project_product_save_error_t project_product_save_last_error(void){return g_save_error;}
int32_t project_product_save_last_detail(void){return g_save_detail;}

static uint8_t begin_assets(void*ctx){persistence_project_restore_workspace_t*w=ctx;if(w==NULL)return 0U;w->asset_count=0U;return 1U;}
static uint8_t validate_asset(void*ctx,const persist_control_asset_ref_t*a){(void)ctx;return project_product_asset_valid(a);}
static uint8_t put_asset(void*ctx,const persist_control_asset_ref_t*a){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||a==NULL||w->asset_count>=PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY)return 0U;w->assets[w->asset_count++]=*a;return 1U;}
static uint8_t apply_working(void*ctx,const persist_codec_project_metadata_t*m,const persist_control_pattern_t*p){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL||p==NULL)return 0U;w->metadata=*m;w->working_pattern=*p;w->working_valid=1U;return 1U;}
static uint8_t apply_macros(void*ctx,const persist_control_macros_t*m){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL)return 0U;w->macros=*m;w->macros_valid=1U;return 1U;}
static uint8_t begin_patterns(void*ctx){persistence_project_restore_workspace_t*w=ctx;if(w==NULL)return 0U;w->pattern_bank_started=0U;w->pattern_bank_staged=0U;if(pattern_control_bank_begin_project()==0U)return 0U;w->pattern_bank_started=1U;return 1U;}
static uint8_t put_pattern(void*ctx,const persist_control_pattern_record_t*r){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||r==NULL)return 0U;if((r->bank==w->metadata.active_pattern_bank)&&(r->pattern==w->metadata.active_pattern))w->active_pattern_seen=1U;return (pattern_control_bank_put_record_project(r)!=0U)?1U:0U;}
static uint8_t stage_patterns(void*ctx){persistence_project_restore_workspace_t*w=ctx;if(w==NULL)return 0U;w->pattern_bank_staged=1U;return 1U;}

static uint8_t project_product_build_default_candidate(
    persistence_project_restore_workspace_t *restore)
{
    persist_control_pattern_record_t *record;

    if (restore == NULL) return 0U;
    memset(restore,0,sizeof(*restore));
    if (pattern_live_get_control_boot(&restore->working_pattern) == 0U
        || project_control_get_default_macros(&restore->macros) == 0U)
        return 0U;

    restore->metadata.active_pattern_bank=0U;
    restore->metadata.active_pattern=0U;
    restore->metadata.pattern_count=1U;
    restore->metadata.asset_count=0U;
    restore->working_valid=1U;
    restore->macros_valid=1U;
    record=&restore->codec.unit.pattern_record;
    record->bank=0U;
    record->pattern=0U;
    record->present=1U;
    record->content=restore->working_pattern;
    if (pattern_control_bank_begin_project() == 0U
        || pattern_control_bank_put_record_project(record) == 0U)
    {
        pattern_control_bank_abort(NULL);
        memset(restore,0,sizeof(*restore));
        return 0U;
    }
    restore->pattern_bank_started=1U;
    restore->pattern_bank_staged=1U;
    restore->active_pattern_seen=1U;
    return 1U;
}

static void project_product_start_candidate(
    persistence_project_restore_workspace_t *restore,
    uint8_t slot,
    uint8_t quiesce_requested)
{
    g_project_load_control_ready=0U;
    g_project_load_control_done=0U;
    g_project_load_control_result=0U;
    g_project_load_workspace=restore;
    g_project_load.asset_index=0U;
    g_project_load.slot=slot;
    g_project_load.quiesce_requested=quiesce_requested;
    g_project_load.state=PROJECT_LOAD_WAIT_SAFE;
    if (quiesce_requested != 0U)
        project_load_quiesce_request();
}

uint8_t project_product_load_busy(void)
{
    return (g_project_load.state != PROJECT_LOAD_IDLE) ? 1U : 0U;
}

static void project_discard_restore_workspace(
    persistence_project_restore_workspace_t *restore)
{
    if (restore == NULL) return;
    if (restore->pattern_bank_started != 0U)
    {
        pattern_control_bank_abort(NULL);
        restore->pattern_bank_started = 0U;
        restore->pattern_bank_staged = 0U;
    }
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
}

static void project_product_load_fail_post_p2(void)
{
    persistence_project_restore_workspace_t *const restore =
        g_project_load_workspace;
    const uint8_t quiesce_requested = g_project_load.quiesce_requested;
    if (restore != NULL && restore->pattern_bank_started != 0U)
    {
        pattern_control_bank_abort(NULL);
        restore->pattern_bank_started = 0U;
        restore->pattern_bank_staged = 0U;
    }
    if (restore != NULL)
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
    memset(&g_project_load,0,sizeof(g_project_load));
    g_project_load_control_ready=0U;
    g_project_load_control_done=0U;
    g_progress.done=g_progress.total;
    g_progress.complete=1U;
    g_progress.active=0U;
    g_progress.result=PROJECT_PRODUCT_RESULT_FAILED;
    g_project_load_workspace=NULL;
    g_project_load.state=PROJECT_LOAD_IDLE;
    if (quiesce_requested != 0U)
        project_load_quiesce_end();
}

static uint8_t project_product_asset_loads_pending(void)
{
    return (uint8_t)(sampler_ram_pool_load_async_busy() != 0U
        || wavetable_pool_load_async_busy() != 0U
        || multi_sample_load_has_pending() != 0U
        || sample_cache_has_pending_sd_work() != 0U);
}

static uint8_t project_product_asset_valid(const persist_control_asset_ref_t *asset)
{
    return (asset != NULL && asset_ref_is_canonical(asset) != 0U) ? 1U : 0U;
}

static void project_product_reset_physical_assets(void)
{
    sample_cache_init();
    sample_global_pool_reset();
}

static uint8_t project_product_start_asset(
    persistence_project_restore_workspace_t *restore, uint16_t index)
{
    const persist_control_asset_ref_t *const asset = &restore->assets[index];
    char asset_path[PERSIST_CONTROL_ASSET_PATH_BYTES];
    if (project_product_asset_valid(asset) == 0U) return 0U;
    memcpy(asset_path, asset->canonical_path, asset->path_length);
    asset_path[asset->path_length] = '\0';
    restore->asset_result[index] = PERSISTENCE_ASSET_RESULT_FAILED;
    restore->asset_runtime[index] = UINT16_MAX;
    if (asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
    {
        const uint16_t runtime = sample_global_pool_find_free_slot();
        if (runtime == SAMPLE_GLOBAL_POOL_INVALID_INDEX
                || sample_global_pool_load_classic(runtime, asset_path) == 0U)
            return 0U;
        restore->asset_runtime[index] = runtime;
        return 1U;
    }
    if (asset->kind == PERSIST_ASSET_SAMPLE_RAM)
    {
        const uint16_t backend = sampler_ram_pool_find_free_slot();
        if (backend >= SAMPLER_RAM_POOL_MAX_SLOTS
                || sampler_ram_pool_load_async_begin(backend, asset_path) == 0U)
            return 0U;
        restore->asset_runtime[index] = backend;
        return 1U;
    }
    if (asset->kind == PERSIST_ASSET_WAVETABLE)
    {
        const uint16_t backend = wavetable_pool_find_free_slot();
        if (backend >= WAVETABLE_POOL_MAX_SLOTS
                || wavetable_pool_load_async_begin_with_geometry(
                    backend, asset_path, WAVETABLE_SOURCE_GEOMETRY_2048) == 0U)
            return 0U;
        restore->asset_runtime[index] = backend;
        return 1U;
    }
    if (asset->kind == PERSIST_ASSET_MULTI)
    {
        for (uint16_t runtime = 0U; runtime < MULTI_SAMPLE_POOL_MAX_INSTRUMENTS;
             ++runtime)
            if (multi_sample_pool_get_state(runtime) == MULTI_SAMPLE_INSTRUMENT_EMPTY)
            {
                const multi_sample_load_result_t result =
                    multi_sample_load_instrument(MULTI_SAMPLE_POOL_INVALID_ID,
                                                 asset_path, runtime);
                if (result == MULTI_SAMPLE_LOAD_OK
                        || result == MULTI_SAMPLE_LOAD_ALREADY_READY)
                {
                    restore->asset_runtime[index] = runtime;
                    return 1U;
                }
                return 0U;
            }
    }
    return 0U;
}

static uint8_t project_product_finish_asset(
    persistence_project_restore_workspace_t *restore, uint16_t index)
{
    const persist_control_asset_ref_t *const asset = &restore->assets[index];
    if (asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
    {
        if (sample_cache_is_ready(restore->asset_runtime[index]) == 0U)
            return (sample_cache_get_state(restore->asset_runtime[index])
                    == SAMPLE_CACHE_ERROR) ? 0U : 2U;
    }
    else if (asset->kind == PERSIST_ASSET_SAMPLE_RAM)
    {
        sampler_ram_result_t result; uint16_t backend, runtime; const char *path_value;
        if (sampler_ram_pool_load_async_take_result(
                &result, &backend, &runtime, &path_value) == 0U) return 2U;
        if (result != SAMPLER_RAM_RESULT_OK) return 0U;
        restore->asset_runtime[index] = runtime;
    }
    else if (asset->kind == PERSIST_ASSET_WAVETABLE)
    {
        wavetable_result_t result; uint16_t backend, runtime; const char *path_value;
        if (wavetable_pool_load_async_take_result(
                &result, &backend, &runtime, &path_value) == 0U) return 2U;
        if (result != WAVETABLE_RESULT_OK) return 0U;
        restore->asset_runtime[index] = runtime;
    }
    else if (asset->kind == PERSIST_ASSET_MULTI)
    {
        if (multi_sample_load_has_pending() != 0U) return 2U;
        if (multi_sample_pool_get_state(restore->asset_runtime[index])
                != MULTI_SAMPLE_INSTRUMENT_READY) return 0U;
    }
    restore->asset_result[index] = PERSISTENCE_ASSET_RESULT_READY;
    return 1U;
}

static void project_product_load_finish(uint8_t success)
{
    persistence_project_restore_workspace_t *const restore=g_project_load_workspace;
    const uint8_t slot=g_project_load.slot;
    const uint8_t quiesce_requested=g_project_load.quiesce_requested;
    if (success != 0U
        && (g_project_load.asset_index < g_project_load.asset_count
            || project_product_asset_loads_pending() != 0U))
        success = 0U;
    if (success == 0U)
    {
        project_product_load_fail_post_p2();
        return;
    }
    if (slot != PROJECT_PRODUCT_NO_SLOT
        && boot_context_flash_commit(slot) == 0U)
    {
        project_product_load_fail_post_p2();
        return;
    }
    if(restore!=NULL)persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
    g_project_load_workspace=NULL;
    memset(&g_project_load,0,sizeof(g_project_load));
    g_project_load_control_ready=0U;
    g_project_load_control_done=0U;
    g_progress.done=g_progress.total;
    g_progress.complete=1U;
    g_progress.active=0U;
    if (slot == PROJECT_PRODUCT_NO_SLOT)
        boot_context_flash_clear();
    g_progress.result=PROJECT_PRODUCT_RESULT_SUCCESS;
    if (quiesce_requested != 0U)
        project_load_quiesce_end();
}

static uint8_t project_multi_result_internal(multi_sample_load_result_t result)
{
    return (uint8_t)(result == MULTI_SAMPLE_LOAD_INVALID_ARG
        || result == MULTI_SAMPLE_LOAD_POOL_FAIL
        || result == MULTI_SAMPLE_LOAD_REGISTER_FAIL
        || result == MULTI_SAMPLE_LOAD_TRANSPORT_ACTIVE
        || result == MULTI_SAMPLE_LOAD_CANCELLED);
}

void project_product_load_service(void)
{
    persistence_project_restore_workspace_t *const restore=g_project_load_workspace;
    if(g_project_load.state==PROJECT_LOAD_IDLE
        || g_project_load.state==PROJECT_LOAD_FAILED)return;
    if (g_project_load.state == PROJECT_LOAD_WAIT_SAFE
        && g_project_load.quiesce_requested != 0U
        && project_load_quiesce_failed() != 0U)
    {
        project_product_load_finish(0U);
        return;
    }
    if(g_project_load.state==PROJECT_LOAD_WAIT_MULTI)
    {
        if(multi_sample_load_has_pending()!=0U)return;
        multi_sample_load_diag_t diag;
        multi_sample_get_load_diag(&diag);
        if (project_multi_result_internal(diag.last_error))
        {
            project_product_load_finish(0U); return;
        }
        restore->asset_result[g_project_load.asset_index] =
            (diag.last_error == MULTI_SAMPLE_LOAD_OK)
                ? PERSISTENCE_ASSET_RESULT_READY : PERSISTENCE_ASSET_RESULT_FAILED;
        if (diag.last_error != MULTI_SAMPLE_LOAD_OK)
            ++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_ASSETS;
        return;
    }
    if(g_project_load.state==PROJECT_LOAD_WAIT_STREAM)
    {
        const uint8_t result = project_product_finish_asset(
            restore, g_project_load.asset_index);
        if(result==2U)return;
        if(result==0U)++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_ASSETS;
    }
    if(restore==NULL)return;
    if (g_project_load.state == PROJECT_LOAD_WAIT_SAFE)
    {
        if (g_project_load.quiesce_requested != 0U
            && project_load_quiesce_safe() == 0U) return;
        sd_scheduler_runtime_exclusive_request();
        if (sd_scheduler_runtime_exclusive_try_begin() == 0U) return;
        if (pattern_control_bank_commit(restore) == 0U)
        {
            sd_scheduler_runtime_exclusive_end();
            project_product_load_finish(0U);
            return;
        }
        restore->pattern_bank_started = 0U;
        restore->pattern_bank_staged = 0U;
        project_product_reset_physical_assets();
        g_project_load.state = PROJECT_LOAD_ASSETS;
        g_progress=(project_product_progress_t){1U,0U,0U,
            (uint32_t)restore->asset_count+1U};
        sd_scheduler_runtime_exclusive_end();
        return;
    }
    if(g_project_load.state==PROJECT_LOAD_WAIT_RAM)
    {
        const uint8_t result = project_product_finish_asset(
            restore, g_project_load.asset_index);
        if(result==2U)return;
        if(result==0U)++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state=PROJECT_LOAD_ASSETS;
    }
    if (g_project_load.state == PROJECT_LOAD_WAIT_WAVETABLE)
    {
        const uint8_t result = project_product_finish_asset(
            restore, g_project_load.asset_index);
        if(result==2U)return;
        if(result==0U)++g_progress.asset_warning_count;
        ++g_project_load.asset_index;
        ++g_progress.done;
        g_project_load.state = PROJECT_LOAD_ASSETS;
    }
    if(g_project_load.state==PROJECT_LOAD_ASSETS)
    {
        if(g_project_load.asset_index<restore->asset_count)
        {
            const uint8_t result = project_product_start_asset(
                restore, g_project_load.asset_index);
            if(result != 0U)
            {
                const uint32_t kind=restore->assets[g_project_load.asset_index].kind;
                if (kind == PERSIST_ASSET_SAMPLE_STREAM)
                {
                    const uint8_t terminal = project_product_finish_asset(
                        restore, g_project_load.asset_index);
                    if (terminal == 1U) { ++g_project_load.asset_index; ++g_progress.done; return; }
                    if (terminal == 0U) ++g_progress.asset_warning_count;
                }
                else if (kind == PERSIST_ASSET_MULTI
                         && multi_sample_load_has_pending() == 0U)
                {
                    restore->asset_result[g_project_load.asset_index] =
                        PERSISTENCE_ASSET_RESULT_READY;
                    ++g_project_load.asset_index; ++g_progress.done; return;
                }
                g_project_load.state=(kind==PERSIST_ASSET_SAMPLE_STREAM)
                    ? PROJECT_LOAD_WAIT_STREAM
                    : (kind==PERSIST_ASSET_WAVETABLE
                        ? PROJECT_LOAD_WAIT_WAVETABLE
                        : (kind==PERSIST_ASSET_MULTI
                            ? PROJECT_LOAD_WAIT_MULTI : PROJECT_LOAD_WAIT_RAM));
                return;
            }
            ++g_progress.asset_warning_count;
            ++g_project_load.asset_index;
            ++g_progress.done;
            return;
        }
        g_project_load.state=PROJECT_LOAD_COMMIT;
    }
    if(g_project_load.state==PROJECT_LOAD_COMMIT)
    {
        if (project_product_asset_loads_pending() != 0U)
        {
            project_product_load_finish(0U);
            return;
        }
        if (g_project_load_control_ready == 0U)
        {
            __DMB();
            g_project_load_control_ready = 1U;
            return;
        }
        if (g_project_load_control_done != 0U)
        {
            project_product_load_finish(g_project_load_control_result);
        }
    }
}

void project_product_control_process(void)
{
    if (g_project_load_control_ready == 0U)
        return;

    persistence_project_restore_workspace_t *const restore =
        persistence_workspace_project_restore_view();
    uint8_t ok = (restore != NULL) ? 1U : 0U;
    if (ok != 0U)
        ok = project_control_begin_asset_restore();
    if (ok != 0U)
        for (uint16_t i = 0U; i < restore->asset_count; ++i)
        {
            const persist_control_asset_ref_t *const asset = &restore->assets[i];
            if (restore->asset_result[i] != PERSISTENCE_ASSET_RESULT_READY)
                continue;
            if (asset->kind == PERSIST_ASSET_SAMPLE_STREAM)
                ok = project_control_register_sample_runtime(
                    asset->kind, asset->canonical_path,
                    restore->asset_runtime[i], NULL);
            else if (asset->kind == PERSIST_ASSET_SAMPLE_RAM)
                ok = project_control_register_sample_runtime(
                    asset->kind, asset->canonical_path,
                    restore->asset_runtime[i], NULL);
            else if (asset->kind == PERSIST_ASSET_WAVETABLE)
                ok = project_control_register_wavetable_runtime(
                    asset->canonical_path, restore->asset_runtime[i], NULL);
            else if (asset->kind == PERSIST_ASSET_MULTI)
                ok = project_control_register_multi_runtime(
                    asset->canonical_path, restore->asset_runtime[i], NULL);
            if (ok == 0U) break;
        }
    if (ok != 0U)
        ok = (persistent_pattern_control_apply(
            &restore->working_pattern, 0U) == PERSIST_CODEC_OK) ? 1U : 0U;
    if (ok != 0U)
        ok = project_control_apply_macros(&restore->macros);
    if (ok != 0U)
        pattern_live_set_active_state(restore->metadata.active_pattern_bank,
                                       restore->metadata.active_pattern,
                                       0U, 0U, 0U, 0U);
    __DMB();
    g_project_load_control_result = ok;
    g_project_load_control_done = 1U;
    g_project_load_control_ready = 0U;
    if (restore != NULL)
        persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
}

void project_product_control_process_intent(uint8_t operation, uint8_t slot)
{
    if (operation == PROJECT_PRODUCT_COMMAND_SAVE)
    {
        (void)project_product_save(slot);
        return;
    }
    if (g_storage_request != PROJECT_PRODUCT_COMMAND_NONE
        || project_product_save_busy() != 0U
        || project_product_load_busy() != 0U)
        return;
    g_storage_slot = slot;
    __DMB();
    g_storage_request = (project_product_command_t)operation;
}

uint8_t project_product_load(uint8_t slot)
{
    if (project_product_save_busy()!=0U || project_product_load_busy()!=0U
        || project_replacement_is_active()!=0U || project_load_allowed()==0U
        || slot>=PROJECT_PRODUCT_SLOT_COUNT || !g_present[slot]) return 0U;

    persistence_project_restore_workspace_t *const restore =
        persistence_workspace_acquire_project_restore();
    persist_codec_project_workspace_t *const workspace =
        (restore != NULL) ? &restore->codec : NULL;
    if (workspace == NULL) return 0U;
    memset(restore, 0, sizeof(*restore));
    g_progress = (project_product_progress_t){1U, 0U, 0U, 1U,
        PROJECT_PRODUCT_RESULT_IN_PROGRESS};

    char project_path[48];
    persistent_fatfs_file_t file;
    uint8_t ok = path(project_path, sizeof(project_path), slot) && acquire();
    if ((ok != 0U) && (persistent_fatfs_open_read(&file, project_path) == 0U))
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
        ok = 0U;
    }
    persist_codec_source_t source = {0};
    if (ok != 0U) source = persistent_fatfs_source(&file);
    persist_codec_result_t result = PERSIST_CODEC_IO_ERROR;
    if (ok != 0U)
    {
        persist_codec_project_consumer_t project = {
            begin_assets, validate_asset, put_asset, apply_working, apply_macros, restore};
        persist_codec_pattern_consumer_t patterns = {
            begin_patterns, put_pattern, stage_patterns, pattern_control_bank_abort, restore};
        result = persist_codec_decode_project_progressive(&source, workspace,
                                                          &project, &patterns);
    }
    if (source.context != NULL) persistent_fatfs_close(&file);
    if (ok != 0U) sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);

    ok = (ok != 0U) && (result == PERSIST_CODEC_OK)
        && (restore->working_valid != 0U) && (restore->macros_valid != 0U)
        && (restore->pattern_bank_staged != 0U)
        && (restore->active_pattern_seen != 0U)
        && (restore->asset_count <= PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY);
    if (ok == 0U)
    {
        project_discard_restore_workspace(restore);
        g_progress = (project_product_progress_t){0U, 1U, 0U, 0U,PROJECT_PRODUCT_RESULT_FAILED};
        return 0U;
    }

    g_project_load.asset_count=restore->asset_count;

    /* P1 ends here: the bounded Project DTO and inactive Pattern bank staging
     * are complete while the live Project remains untouched. */
    project_product_start_candidate(restore,slot,1U);
    return 1U;
}

uint8_t project_product_delete(uint8_t slot){if(project_replacement_is_active()!=0U||project_product_save_busy()!=0U||project_product_load_busy()!=0U||slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;char x[48];FRESULT r=FR_INVALID_NAME;if(path(x,sizeof(x),slot))r=f_unlink(x);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);uint8_t ok=(r==FR_OK||r==FR_NO_FILE);if(ok)g_present[slot]=0U;return ok;}
uint8_t project_product_blank(void)
{
    if(project_replacement_is_active()!=0U||project_product_save_busy()!=0U
       ||project_product_load_busy()!=0U||project_load_allowed()==0U)return 0U;
    persistence_project_restore_workspace_t *const restore=
        persistence_workspace_acquire_project_restore();
    if(restore==NULL||project_product_build_default_candidate(restore)==0U)
    {
        if(restore!=NULL)persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
        return 0U;
    }
    g_progress=(project_product_progress_t){1U,0U,0U,1U,
        PROJECT_PRODUCT_RESULT_IN_PROGRESS};
    project_product_start_candidate(restore,PROJECT_PRODUCT_NO_SLOT,1U);
    return 1U;
}
project_product_boot_restore_result_t project_product_restore_boot(void)
{
    if (sd_access_storage_status() != SD_STORAGE_STATUS_READY)
        return PROJECT_PRODUCT_BOOT_RESTORE_FAILED;

    boot_context_flash_data_t context;
    if (!boot_context_flash_load(&context))
    {
        persistence_project_restore_workspace_t *const restore =
            persistence_workspace_acquire_project_restore();
        if (restore == NULL || project_product_build_default_candidate(restore) == 0U)
        {
            if (restore != NULL)
                persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);
            return PROJECT_PRODUCT_BOOT_RESTORE_FAILED;
        }
        g_progress=(project_product_progress_t){1U,0U,0U,1U,
            PROJECT_PRODUCT_RESULT_IN_PROGRESS};
        project_product_start_candidate(restore,PROJECT_PRODUCT_NO_SLOT,0U);
        return PROJECT_PRODUCT_BOOT_RESTORE_DEFAULTS_READY;
    }
    if (context.active_project_slot >= PROJECT_PRODUCT_SLOT_COUNT)
        return PROJECT_PRODUCT_BOOT_RESTORE_FAILED;
    return (project_product_load(context.active_project_slot) != 0U)
        ? PROJECT_PRODUCT_BOOT_RESTORE_PROJECT_READY
        : PROJECT_PRODUCT_BOOT_RESTORE_FAILED;
}
uint8_t project_product_get_progress(project_product_progress_t*out){if(out==NULL)return 0U;*out=g_progress;return 1U;}

void project_product_storage_request_service(void)
{
    const project_product_command_t command = g_storage_request;
    if (command == PROJECT_PRODUCT_COMMAND_NONE)
        return;
    const uint8_t slot = g_storage_slot;
    g_storage_request = PROJECT_PRODUCT_COMMAND_NONE;
    switch (command)
    {
        case PROJECT_PRODUCT_COMMAND_LOAD:
            (void)project_product_load(slot);
            break;
        case PROJECT_PRODUCT_COMMAND_DELETE:
            (void)project_product_delete(slot);
            break;
        case PROJECT_PRODUCT_COMMAND_BLANK:
            (void)project_product_blank();
            break;
        case PROJECT_PRODUCT_COMMAND_RESTORE_BOOT:
            (void)project_product_restore_boot();
            break;
        default:
            break;
    }
}
