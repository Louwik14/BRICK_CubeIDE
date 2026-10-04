#include "Storage/pattern_control_bank.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/project_load_quiesce.h"
#include "Storage/project_storage_paths.h"
#include "Storage/pattern_working_bank.h"
#include "Storage/sd_access_gate.h"
#include "SD/sd_scheduler_runtime.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define BANKS 16U
#define SLOTS 16U
#define INVALID_PROJECT 0xFFU

static uint8_t g_present[BANKS][SLOTS];
/* Canonical Project root selector: 0..15 is PROJECTS/P##, 0xFF means the
 * current musical state is unsaved and has no Pattern Store destination. */
static uint8_t g_active_project=INVALID_PROJECT;

typedef enum { PATTERN_ASYNC_IDLE=0,PATTERN_ASYNC_MOUNT,PATTERN_ASYNC_RECOVER,
    PATTERN_ASYNC_OPEN,PATTERN_ASYNC_ALLOCATE,PATTERN_ASYNC_TRANSFER,
    PATTERN_ASYNC_SYNC,PATTERN_ASYNC_CLOSE,PATTERN_ASYNC_COMMIT,
    PATTERN_ASYNC_DECODE,PATTERN_ASYNC_CLEANUP_CLOSE,
    PATTERN_ASYNC_CLEANUP_TEMP,PATTERN_ASYNC_DONE } pattern_async_state_t;

typedef struct {
    persistent_fatfs_file_t file;persist_control_pattern_t *load_out;
    uint8_t *encoded;uint32_t encoded_capacity,encoded_size,offset,media_epoch;
    pattern_async_state_t state;pattern_control_bank_async_operation_t operation;
    pattern_control_bank_async_error_t error;int32_t filesystem_result;
    uint8_t bank,pattern,file_open,result_ready,success;
    char final_path[80],temporary_path[84],backup_path[84];
} pattern_async_context_t;

typedef struct {uint8_t *data;uint32_t capacity,position;} pattern_memory_io_t;
static pattern_async_context_t g_pattern_async;

static uint8_t valid(uint8_t b,uint8_t p){return(b<BANKS&&p<SLOTS)?1U:0U;}
static uint8_t side_path(char*out,uint32_t size,const char*base,const char*suffix){int n=snprintf(out,size,"%s.%s",base,suffix);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return 0U;}return 1U;}

static uint8_t pattern_name_slot(const char*n,uint8_t*b,uint8_t*p)
{
    if(n==NULL||b==NULL||p==NULL||strlen(n)<11U||n[0]!='B'||n[3]!='_'
       ||n[4]!='P'||n[7]!='.'||n[8]!='B'||n[9]!='6'||n[10]!='C'
       ||n[1]<'0'||n[1]>'9'||n[2]<'0'||n[2]>'9'
       ||n[5]<'0'||n[5]>'9'||n[6]<'0'||n[6]>'9')return 0U;
    uint8_t bank=(uint8_t)((n[1]-'0')*10+(n[2]-'0'));
    uint8_t pattern=(uint8_t)((n[5]-'0')*10+(n[6]-'0'));
    if(!valid(bank,pattern))return 0U;
    *b=bank;*p=pattern;return 1U;
}

static uint8_t scan_project(uint8_t slot)
{
    char directory[64];DIR dir;FILINFO info;memset(g_present,0,sizeof(g_present));
    if(!project_storage_patterns_dir(directory,sizeof(directory),slot)
       ||f_opendir(&dir,directory)!=FR_OK)return 0U;
    for(;;){if(f_readdir(&dir,&info)!=FR_OK){(void)f_closedir(&dir);return 0U;}
        if(info.fname[0]=='\0')break;
        uint8_t b=0U,p=0U;
        if(!pattern_name_slot(info.fname,&b,&p))continue;
        char x[80],tmp[84],bak[84];FILINFO final_info;
        if(!project_storage_pattern_file(x,sizeof(x),slot,b,p)
           ||!side_path(tmp,sizeof(tmp),x,"TMP")||!side_path(bak,sizeof(bak),x,"BAK"))continue;
        (void)persistent_fatfs_recover_replace(x,tmp,bak);
        if(f_stat(x,&final_info)==FR_OK)g_present[b][p]=1U;}
    (void)f_closedir(&dir);return 1U;
}

void pattern_control_bank_init(void){memset(g_present,0,sizeof(g_present));memset(&g_pattern_async,0,sizeof(g_pattern_async));g_active_project=INVALID_PROJECT;pattern_working_bank_init();}
uint8_t pattern_control_bank_activate_project(uint8_t slot){if(slot>=PROJECT_STORAGE_SLOT_COUNT||g_pattern_async.state!=PATTERN_ASYNC_IDLE||!acquire())return 0U;uint8_t ok=scan_project(slot);if(ok)ok=pattern_working_bank_start_project_mounted(slot);if(ok)g_active_project=slot;sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;}
uint8_t pattern_control_bank_activate_resume_project(uint8_t slot,const uint32_t dirty_words[8]){if(slot>=PROJECT_STORAGE_SLOT_COUNT||dirty_words==NULL||g_pattern_async.state!=PATTERN_ASYNC_IDLE||!acquire())return 0U;uint8_t ok=scan_project(slot);if(ok)ok=pattern_working_bank_restore_mounted(PATTERN_WORKING_BASE_PROJECT,slot,dirty_words);if(ok)g_active_project=slot;sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;}
uint8_t pattern_control_bank_activate_resume_blank(const uint32_t dirty_words[8]){if(dirty_words==NULL||g_pattern_async.state!=PATTERN_ASYNC_IDLE)return 0U;g_active_project=INVALID_PROJECT;memset(g_present,0,sizeof(g_present));return pattern_working_bank_restore_mounted(PATTERN_WORKING_BASE_BLANK,INVALID_PROJECT,dirty_words);}
uint8_t pattern_control_bank_validate_project(uint8_t slot)
{
    if(slot>=PROJECT_STORAGE_SLOT_COUNT||!acquire())return 0U;
    char directory[64];DIR dir;uint8_t ok=(uint8_t)(
        project_storage_patterns_dir(directory,sizeof(directory),slot)
        &&f_opendir(&dir,directory)==FR_OK);
    if(ok)(void)f_closedir(&dir);
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;
}
void pattern_control_bank_deactivate_project(void){if(g_pattern_async.state==PATTERN_ASYNC_IDLE){g_active_project=INVALID_PROJECT;memset(g_present,0,sizeof(g_present));pattern_working_bank_start_blank();}}
uint8_t pattern_control_bank_active_project(uint8_t*out){if(out==NULL||g_active_project>=PROJECT_STORAGE_SLOT_COUNT)return 0U;*out=g_active_project;return 1U;}
void pattern_control_bank_publish_project(uint8_t slot,const uint32_t present_words[8])
{
    if(slot>=PROJECT_STORAGE_SLOT_COUNT||present_words==NULL
        ||g_pattern_async.state!=PATTERN_ASYNC_IDLE)return;
    g_active_project=slot;memset(g_present,0,sizeof(g_present));
    for(uint16_t index=0U;index<256U;++index)
        if((present_words[index>>5U]&(UINT32_C(1)<<(index&31U)))!=0U)
            g_present[index>>4U][index&15U]=1U;
    pattern_working_bank_rebase_project(slot);
}
uint8_t pattern_control_bank_present(uint8_t b,uint8_t p){return valid(b,p)?g_present[b][p]:0U;}
void pattern_control_bank_mark_present(uint8_t b,uint8_t p){if(valid(b,p))g_present[b][p]=1U;}
uint16_t pattern_control_bank_count(void){uint16_t n=0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)n+=g_present[b][p]?1U:0U;return n;}
uint8_t pattern_control_bank_delete(uint8_t b,uint8_t p){if(g_active_project>=PROJECT_STORAGE_SLOT_COUNT||!valid(b,p)||!acquire())return 0U;char x[80],tmp[84],bak[84];uint8_t ok=project_storage_pattern_file(x,sizeof(x),g_active_project,b,p)&&side_path(tmp,sizeof(tmp),x,"TMP")&&side_path(bak,sizeof(bak),x,"BAK");if(ok){FRESULT r=f_unlink(x);ok=(r==FR_OK||r==FR_NO_FILE);(void)f_unlink(tmp);(void)f_unlink(bak);}if(ok)g_present[b][p]=0U;sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;}

static uint8_t mem_write(void*c,const uint8_t*d,uint32_t n){pattern_memory_io_t*m=c;if(m==NULL||d==NULL||m->position>m->capacity||n>m->capacity-m->position)return 0U;memcpy(&m->data[m->position],d,n);m->position+=n;return 1U;}
static uint8_t mem_read(void*c,uint8_t*d,uint32_t n){pattern_memory_io_t*m=c;if(m==NULL||d==NULL||m->position>m->capacity||n>m->capacity-m->position)return 0U;memcpy(d,&m->data[m->position],n);m->position+=n;return 1U;}
static uint8_t mem_reset(void*c){pattern_memory_io_t*m=c;if(m==NULL)return 0U;m->position=0U;return 1U;}
static uint8_t mem_size(void*c,uint32_t*n){pattern_memory_io_t*m=c;if(m==NULL||n==NULL)return 0U;*n=m->capacity;return 1U;}
static void async_finish(uint8_t ok){g_pattern_async.file_open=0U;g_pattern_async.success=ok?1U:0U;g_pattern_async.result_ready=1U;g_pattern_async.state=PATTERN_ASYNC_DONE;}
static void async_fail(pattern_control_bank_async_error_t error,FRESULT result){if(g_pattern_async.error==PATTERN_CONTROL_BANK_ASYNC_ERROR_NONE){g_pattern_async.error=error;g_pattern_async.filesystem_result=(int32_t)result;}if(g_pattern_async.file_open)g_pattern_async.state=PATTERN_ASYNC_CLEANUP_CLOSE;else if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)g_pattern_async.state=PATTERN_ASYNC_CLEANUP_TEMP;else async_finish(0U);}
static uint8_t async_abort(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_BACKGROUND))return 0U;g_pattern_async.error=PATTERN_CONTROL_BANK_ASYNC_ERROR_MEDIA;g_pattern_async.filesystem_result=(int32_t)FR_NOT_READY;if(g_pattern_async.file_open)(void)persistent_fatfs_close_result(&g_pattern_async.file);g_pattern_async.file_open=0U;if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)(void)f_unlink(g_pattern_async.temporary_path);sd_access_gate_release(SD_ACCESS_CLIENT_BACKGROUND);async_finish(0U);return 1U;}
static sd_scheduler_background_admission_t admit(sd_scheduler_background_kind_t kind,uint32_t bytes){const sd_scheduler_background_request_t r={bytes,g_pattern_async.media_epoch,kind};return sd_scheduler_runtime_background_try_begin(&r);}

pattern_control_bank_store_begin_result_t pattern_control_bank_store_async_begin(
    uint8_t b,uint8_t p,const persist_control_pattern_t*in,uint8_t*encoded,
    uint32_t capacity)
{
    if(project_replacement_is_active())return PATTERN_CONTROL_BANK_STORE_BEGIN_POLICY;
    if(g_active_project>=PROJECT_STORAGE_SLOT_COUNT)
        return PATTERN_CONTROL_BANK_STORE_BEGIN_NO_PROJECT;
    if(!valid(b,p)||in==NULL||encoded==NULL||capacity==0U)
        return PATTERN_CONTROL_BANK_STORE_BEGIN_ARGUMENT;
    if(g_pattern_async.state!=PATTERN_ASYNC_IDLE)
        return PATTERN_CONTROL_BANK_STORE_BEGIN_BUSY;
    pattern_memory_io_t memory={encoded,capacity,0U};const persist_codec_sink_t sink={mem_write,&memory};uint32_t size=0U;
    if(persist_codec_encode_pattern(in,&sink,&size)!=PERSIST_CODEC_OK)
        return PATTERN_CONTROL_BANK_STORE_BEGIN_CODEC;
    memset(&g_pattern_async,0,sizeof(g_pattern_async));g_pattern_async.operation=PATTERN_CONTROL_BANK_ASYNC_SAVE;g_pattern_async.state=PATTERN_ASYNC_MOUNT;g_pattern_async.bank=b;g_pattern_async.pattern=p;g_pattern_async.encoded=encoded;g_pattern_async.encoded_capacity=capacity;g_pattern_async.encoded_size=size;g_pattern_async.media_epoch=sd_access_media_epoch();
    if(!project_storage_pattern_file(g_pattern_async.final_path,sizeof(g_pattern_async.final_path),g_active_project,b,p)||!side_path(g_pattern_async.temporary_path,sizeof(g_pattern_async.temporary_path),g_pattern_async.final_path,"TMP")||!side_path(g_pattern_async.backup_path,sizeof(g_pattern_async.backup_path),g_pattern_async.final_path,"BAK")){memset(&g_pattern_async,0,sizeof(g_pattern_async));return PATTERN_CONTROL_BANK_STORE_BEGIN_PATH;}
    return PATTERN_CONTROL_BANK_STORE_BEGIN_OK;
}

uint8_t pattern_control_bank_load_async_begin(uint8_t b,uint8_t p,uint8_t*encoded,uint32_t capacity,persist_control_pattern_t*out)
{
    if(project_replacement_is_active()||g_active_project>=PROJECT_STORAGE_SLOT_COUNT||!valid(b,p)||encoded==NULL||capacity==0U||out==NULL||!g_present[b][p]||g_pattern_async.state!=PATTERN_ASYNC_IDLE)return 0U;
    memset(&g_pattern_async,0,sizeof(g_pattern_async));g_pattern_async.operation=PATTERN_CONTROL_BANK_ASYNC_LOAD;g_pattern_async.state=PATTERN_ASYNC_MOUNT;g_pattern_async.bank=b;g_pattern_async.pattern=p;g_pattern_async.encoded=encoded;g_pattern_async.encoded_capacity=capacity;g_pattern_async.load_out=out;g_pattern_async.media_epoch=sd_access_media_epoch();
    if(!project_storage_pattern_file(g_pattern_async.final_path,sizeof(g_pattern_async.final_path),g_active_project,b,p)){memset(&g_pattern_async,0,sizeof(g_pattern_async));return 0U;}return 1U;
}

pattern_control_bank_project_load_result_t pattern_control_bank_load_project(
    uint8_t slot,uint8_t b,uint8_t p,persist_control_pattern_t*out)
{
    if(slot>=PROJECT_STORAGE_SLOT_COUNT||!valid(b,p)||out==NULL||!acquire())
        return PATTERN_CONTROL_BANK_PROJECT_LOAD_ERROR;
    char path[80];FILINFO info;FRESULT stat_result=FR_INVALID_NAME;
    if(project_storage_pattern_file(path,sizeof(path),slot,b,p))
        stat_result=f_stat(path,&info);
    if(stat_result==FR_NO_FILE||stat_result==FR_NO_PATH)
    {
        sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
        return PATTERN_CONTROL_BANK_PROJECT_LOAD_EMPTY;
    }
    persistent_fatfs_file_t file;
    uint8_t ok=(stat_result==FR_OK)&&persistent_fatfs_open_read(&file,path);
    if(ok)
    {
        persist_codec_source_t source=persistent_fatfs_source(&file);
        ok=(persist_codec_decode_pattern(&source,
            (persist_codec_pattern_staging_t*)out)==PERSIST_CODEC_OK);
        if(persistent_fatfs_close_result(&file)!=FR_OK)ok=0U;
    }
    sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);
    return ok?PATTERN_CONTROL_BANK_PROJECT_LOAD_OK
        :PATTERN_CONTROL_BANK_PROJECT_LOAD_ERROR;
}

void pattern_control_bank_async_service(void)
{
    if(g_pattern_async.state==PATTERN_ASYNC_IDLE||g_pattern_async.state==PATTERN_ASYNC_DONE)return;
    if(g_pattern_async.state==PATTERN_ASYNC_DECODE){pattern_memory_io_t m={g_pattern_async.encoded,g_pattern_async.encoded_size,0U};const persist_codec_source_t s={mem_read,mem_reset,mem_size,&m};const uint8_t ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)g_pattern_async.load_out)==PERSIST_CODEC_OK);if(!ok)g_pattern_async.error=PATTERN_CONTROL_BANK_ASYNC_ERROR_DECODE;async_finish(ok);return;}
    uint32_t chunk=0U;sd_scheduler_background_kind_t kind=SD_SCHEDULER_BACKGROUND_METADATA;if(g_pattern_async.state==PATTERN_ASYNC_TRANSFER){chunk=g_pattern_async.encoded_size-g_pattern_async.offset;if(chunk>SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)chunk=SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;kind=SD_SCHEDULER_BACKGROUND_DATA;}
    sd_scheduler_background_admission_t a=admit(kind,chunk);if(a==SD_SCHEDULER_BACKGROUND_NOT_NOW)return;if(a!=SD_SCHEDULER_BACKGROUND_GO){(void)async_abort();return;}
    FRESULT fr=FR_OK;UINT done=0U;
    switch(g_pattern_async.state){
    case PATTERN_ASYNC_MOUNT:if(!sd_access_fs_mount_if_needed())async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_MOUNT,FR_NOT_READY);else g_pattern_async.state=(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)?PATTERN_ASYNC_RECOVER:PATTERN_ASYNC_OPEN;break;
    case PATTERN_ASYNC_RECOVER:fr=persistent_fatfs_recover_replace(g_pattern_async.final_path,g_pattern_async.temporary_path,g_pattern_async.backup_path);if(fr==FR_OK)g_pattern_async.state=PATTERN_ASYNC_OPEN;else async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_RECOVER,fr);break;
    case PATTERN_ASYNC_OPEN:if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)fr=persistent_fatfs_open_write_result(&g_pattern_async.file,g_pattern_async.temporary_path);else if(!persistent_fatfs_open_read(&g_pattern_async.file,g_pattern_async.final_path))fr=FR_DISK_ERR;if(fr!=FR_OK){async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_OPEN,fr);break;}g_pattern_async.file_open=1U;if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_LOAD){g_pattern_async.encoded_size=g_pattern_async.file.size;if(g_pattern_async.encoded_size<PERSIST_CODEC_HEADER_BYTES||g_pattern_async.encoded_size>g_pattern_async.encoded_capacity){async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_DECODE,FR_INVALID_OBJECT);break;}}g_pattern_async.state=(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)?PATTERN_ASYNC_ALLOCATE:PATTERN_ASYNC_TRANSFER;break;
    case PATTERN_ASYNC_ALLOCATE:fr=f_lseek(&g_pattern_async.file.file,(FSIZE_t)(g_pattern_async.encoded_size-1U));if(fr==FR_OK)fr=f_write(&g_pattern_async.file.file,&g_pattern_async.encoded[g_pattern_async.encoded_size-1U],1U,&done);if(fr==FR_OK&&done!=1U)fr=FR_DISK_ERR;if(fr==FR_OK)fr=f_lseek(&g_pattern_async.file.file,0U);if(fr==FR_OK)g_pattern_async.state=PATTERN_ASYNC_TRANSFER;else async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_TRANSFER,fr);break;
    case PATTERN_ASYNC_TRANSFER:if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)fr=f_write(&g_pattern_async.file.file,&g_pattern_async.encoded[g_pattern_async.offset],chunk,&done);else fr=f_read(&g_pattern_async.file.file,&g_pattern_async.encoded[g_pattern_async.offset],chunk,&done);if(fr!=FR_OK||done!=chunk){async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_TRANSFER,(fr!=FR_OK)?fr:FR_DISK_ERR);break;}g_pattern_async.offset+=chunk;if(g_pattern_async.offset==g_pattern_async.encoded_size)g_pattern_async.state=(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)?PATTERN_ASYNC_SYNC:PATTERN_ASYNC_CLOSE;break;
    case PATTERN_ASYNC_SYNC:fr=f_sync(&g_pattern_async.file.file);if(fr==FR_OK)g_pattern_async.state=PATTERN_ASYNC_CLOSE;else async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_SYNC,fr);break;
    case PATTERN_ASYNC_CLOSE:fr=persistent_fatfs_close_result(&g_pattern_async.file);g_pattern_async.file_open=0U;if(fr!=FR_OK)async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_CLOSE,fr);else g_pattern_async.state=(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)?PATTERN_ASYNC_COMMIT:PATTERN_ASYNC_DECODE;break;
    case PATTERN_ASYNC_COMMIT:fr=persistent_fatfs_commit_replace(g_pattern_async.final_path,g_pattern_async.temporary_path,g_pattern_async.backup_path);if(fr==FR_OK){g_present[g_pattern_async.bank][g_pattern_async.pattern]=1U;async_finish(1U);}else async_fail(PATTERN_CONTROL_BANK_ASYNC_ERROR_REPLACE,fr);break;
    case PATTERN_ASYNC_CLEANUP_CLOSE:(void)persistent_fatfs_close_result(&g_pattern_async.file);g_pattern_async.file_open=0U;if(g_pattern_async.operation==PATTERN_CONTROL_BANK_ASYNC_SAVE)g_pattern_async.state=PATTERN_ASYNC_CLEANUP_TEMP;else async_finish(0U);break;
    case PATTERN_ASYNC_CLEANUP_TEMP:(void)f_unlink(g_pattern_async.temporary_path);async_finish(0U);break;
    default:async_finish(0U);break;}
    sd_scheduler_runtime_background_end();
}

uint8_t pattern_control_bank_async_busy(void){return(g_pattern_async.state!=PATTERN_ASYNC_IDLE)?1U:0U;}
uint8_t pattern_control_bank_async_take_result(pattern_control_bank_async_operation_t*op,uint8_t*b,uint8_t*p,uint8_t*ok,pattern_control_bank_async_error_t*error,int32_t*filesystem_result,uint32_t*offset){if(g_pattern_async.state!=PATTERN_ASYNC_DONE||!g_pattern_async.result_ready)return 0U;if(op)*op=g_pattern_async.operation;if(b)*b=g_pattern_async.bank;if(p)*p=g_pattern_async.pattern;if(ok)*ok=g_pattern_async.success;if(error)*error=g_pattern_async.error;if(filesystem_result)*filesystem_result=g_pattern_async.filesystem_result;if(offset)*offset=g_pattern_async.offset;memset(&g_pattern_async,0,sizeof(g_pattern_async));return 1U;}
