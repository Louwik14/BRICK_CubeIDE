#include "Storage/project_product.h"
#include "Core/project_load_quiesce.h"
#include "Storage/audio_recorder.h"
#include "Sampler/sample_stream_transport.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/persistent_project_control.h"
#include "Storage/persistent_pattern_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/pattern_control_bank.h"
#include "Storage/persistence_workspace.h"
#include "Storage/sd_access_gate.h"
#include "Storage/memory_layout.h"
#include "Storage/boot_context_flash.h"
#include "Storage/pattern_live_ram.h"
#include "Storage/persistent_pattern_restore_prepare.h"
#include "Core/project_control.h"
#include "Sampler/multi_sample_loader.h"
#include "Sampler/multi_sample_pool.h"
#include "Sampler/sample_global_pool.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

_Static_assert((2U * SAMPLE_GLOBAL_POOL_ACTIVE_SLOTS
                    + MULTI_SAMPLE_POOL_MAX_INSTRUMENTS)
                   <= PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY,
               "Project Save asset snapshot capacity is too small");

static uint8_t g_present[PROJECT_PRODUCT_SLOT_COUNT],g_active_valid,g_active;
static project_product_progress_t g_progress;
static project_product_save_error_t g_save_error;
static int32_t g_save_detail;

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

static uint8_t path(char*out,uint32_t size,uint8_t slot){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.B6C",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t side_path(char*out,uint32_t size,uint8_t slot,const char*extension){int n=snprintf(out,size,"0:/BRICK/PROJECT/P%02u.%s",slot,extension);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return 0U;}return 1U;}
static uint8_t ensure_directory(void){FRESULT r=f_mkdir("0:/BRICK");if(r!=FR_OK&&r!=FR_EXIST)return 0U;r=f_mkdir("0:/BRICK/PROJECT");return(r==FR_OK||r==FR_EXIST)?1U:0U;}

void project_product_refresh_slots(void){if(project_product_save_busy()!=0U)return;memset(g_present,0,sizeof(g_present));if(!acquire())return;if(!ensure_directory()){sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return;}for(uint8_t s=0;s<PROJECT_PRODUCT_SLOT_COUNT;++s){char x[48],tmp[48],bak[48];FILINFO i;if(path(x,sizeof(x),s)&&side_path(tmp,sizeof(tmp),s,"TMP")&&side_path(bak,sizeof(bak),s,"BAK")){(void)persistent_fatfs_recover_replace(x,tmp,bak);if(f_stat(x,&i)==FR_OK)g_present[s]=1U;}}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
void project_product_init(void){memset(&g_progress,0,sizeof(g_progress));memset(&g_project_save,0,sizeof(g_project_save));g_active_valid=0U;g_active=0U;project_product_refresh_slots();boot_context_flash_init();}
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
    g_project_save.state=PROJECT_SAVE_DONE;g_progress.active=0U;g_progress.complete=1U;
    if(success!=0U){g_present[g_project_save.slot]=1U;g_active=g_project_save.slot;g_active_valid=1U;(void)boot_context_flash_commit(g_project_save.slot);}
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
    if(project_product_save_busy()!=0U){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SD_BUSY;return 0U;}
    if(g_project_save.state==PROJECT_SAVE_DONE)memset(&g_project_save,0,sizeof(g_project_save));
    persistence_project_save_workspace_t *const workspace = persistence_workspace_acquire_project_save();
    if(workspace==NULL){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_WORKSPACE_BUSY;return 0U;}
    persist_codec_result_t codec_result=persistent_pattern_control_capture(&workspace->working_pattern);
    if(codec_result!=PERSIST_CODEC_OK){g_save_error=PROJECT_PRODUCT_SAVE_ERROR_SNAPSHOT;g_save_detail=(int32_t)codec_result;persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_SAVE);return 0U;}
    memset(&g_project_save,0,sizeof(g_project_save));g_project_save.workspace=workspace;g_project_save.slot=slot;
    persistent_project_control_capture_metadata(&g_project_save.metadata);
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
    g_progress=(project_product_progress_t){1U,0U,0U,1U};return 1U;
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
static uint8_t validate_asset(void*ctx,const persist_control_asset_ref_t*a){(void)ctx;return project_control_validate_asset(a);}
static uint8_t put_asset(void*ctx,const persist_control_asset_ref_t*a){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||a==NULL||w->asset_count>=PERSISTENCE_PROJECT_SAVE_ASSET_CAPACITY)return 0U;w->assets[w->asset_count++]=*a;return 1U;}
static uint8_t apply_working(void*ctx,const persist_codec_project_metadata_t*m,const persist_control_pattern_t*p){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL||p==NULL||persistent_pattern_restore_prepare(p,&g_restore_audio_plan)!=PERSIST_CODEC_OK)return 0U;w->metadata=*m;w->working_pattern=*p;w->working_valid=1U;return 1U;}
static uint8_t apply_macros(void*ctx,const persist_control_macros_t*m){persistence_project_restore_workspace_t*w=ctx;if(w==NULL||m==NULL)return 0U;w->macros=*m;w->macros_valid=1U;return 1U;}
static uint8_t begin_patterns(void*ctx){(void)ctx;return pattern_control_bank_begin_project();}
static uint8_t put_pattern(void*ctx,const persist_control_pattern_record_t*r){(void)ctx;return pattern_control_bank_put_record_project(r);}

uint8_t project_product_load(uint8_t slot)
{
    if(project_product_save_busy()!=0U||slot>=PROJECT_PRODUCT_SLOT_COUNT||!g_present[slot])return 0U;
    project_load_quiesce_request();
    while(project_load_quiesce_safe()==0U){audio_recorder_service();sample_stream_transport_worker_poll();sd_scheduler_runtime_service();}
    sd_scheduler_runtime_exclusive_request();
    while(sd_scheduler_runtime_exclusive_try_begin()==0U){sample_stream_transport_worker_poll();sd_scheduler_runtime_service();}
    persistence_project_restore_workspace_t *const restore = persistence_workspace_acquire_project_restore();
    persist_codec_project_workspace_t *const workspace = (restore!=NULL)?&restore->codec:NULL;
    if(workspace==NULL){sd_scheduler_runtime_exclusive_end();project_load_quiesce_end();return 0U;}
    memset(restore,0,sizeof(*restore));
    if(!acquire()){persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);sd_scheduler_runtime_exclusive_end();project_load_quiesce_end();return 0U;}
    char x[48];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot)&&persistent_fatfs_open_read(&f,x);g_progress=(project_product_progress_t){1U,0U,0U,1U};persist_codec_source_t source={0};if(ok)source=persistent_fatfs_source(&f);
    persist_codec_result_t r=PERSIST_CODEC_IO_ERROR;
    if(ok){persist_codec_project_consumer_t project={begin_assets,validate_asset,put_asset,apply_working,apply_macros,restore};persist_codec_pattern_consumer_t patterns={begin_patterns,put_pattern,pattern_control_bank_commit,pattern_control_bank_abort,NULL};r=persist_codec_decode_project_progressive(&source,workspace,&project,&patterns);}
    ok=(ok&&r==PERSIST_CODEC_OK&&restore->working_valid!=0U&&restore->macros_valid!=0U);if(source.context!=NULL)persistent_fatfs_close(&f);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);
    if(ok)ok=project_control_begin_asset_restore();
    for(uint16_t i=0U;ok&&i<restore->asset_count;++i)ok=project_control_put_asset(&restore->assets[i]);
    if(ok)ok=(persistent_pattern_restore_execute(&restore->working_pattern,0U)==PERSIST_CODEC_OK)?1U:0U;
    if(ok)ok=project_control_apply_macros(&restore->macros);
    if(ok)pattern_live_set_active_state(restore->metadata.active_pattern_bank,restore->metadata.active_pattern,0U,0U,0U);
    persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT_RESTORE);g_progress.done=1U;g_progress.complete=(ok&&multi_sample_load_has_pending()!=0U)?0U:1U;g_progress.active=(ok&&g_progress.complete==0U)?1U:0U;if(ok){g_active=slot;g_active_valid=1U;(void)boot_context_flash_commit(slot);}sd_scheduler_runtime_exclusive_end();project_load_quiesce_end();return ok;
}

uint8_t project_product_delete(uint8_t slot){if(project_product_save_busy()!=0U||slot>=PROJECT_PRODUCT_SLOT_COUNT||!acquire())return 0U;char x[48];FRESULT r=FR_INVALID_NAME;if(path(x,sizeof(x),slot))r=f_unlink(x);sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);uint8_t ok=(r==FR_OK||r==FR_NO_FILE);if(ok){g_present[slot]=0U;if(g_active_valid&&g_active==slot)g_active_valid=0U;}return ok;}
uint8_t project_product_blank(void){if(project_product_save_busy()!=0U)return 0U;persist_codec_project_workspace_t*w=persistence_workspace_acquire_project();if(w==NULL)return 0U;persist_control_pattern_t*p=&w->unit.pattern_record.content;uint8_t ok=(pattern_live_get_control_boot(p)&&project_control_begin_asset_restore())?1U:0U;if(ok){project_control_reset_macros();ok=pattern_control_bank_clear();}if(ok){g_active_valid=0U;ok=(persistent_pattern_restore_execute(p,0U)==PERSIST_CODEC_OK)?1U:0U;}if(ok){pattern_live_set_active_state(0U,0U,0U,0U,0U);boot_context_flash_clear();}persistence_workspace_release(PERSISTENCE_WORKSPACE_PROJECT);return ok;}
uint8_t project_product_restore_boot(void){boot_context_flash_data_t c;if(!boot_context_flash_load(&c)||c.active_project_slot>=PROJECT_PRODUCT_SLOT_COUNT)return 0U;return project_product_load(c.active_project_slot);}
uint8_t project_product_get_progress(project_product_progress_t*out){if(out==NULL)return 0U;if(project_product_save_busy()==0U&&g_progress.active&&multi_sample_load_has_pending()==0U){g_progress.active=0U;g_progress.complete=1U;}*out=g_progress;return 1U;}
