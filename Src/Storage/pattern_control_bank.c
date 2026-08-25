#include "Storage/pattern_control_bank.h"
#include "Storage/persistent_fatfs_io.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define BANKS 16U
#define SLOTS 16U
#define SET_COUNT 2U
#define INVALID_SET 0xFFU
#define COMMIT_BYTES 44U

static uint8_t g_present[BANKS][SLOTS];
static uint8_t g_active_set;
static uint8_t g_staging_set = INVALID_SET;
static uint32_t g_generation;
static uint8_t g_staging_bitmap[32];

typedef enum
{
    PATTERN_ASYNC_IDLE = 0,
    PATTERN_ASYNC_MOUNT,
    PATTERN_ASYNC_RECOVER,
    PATTERN_ASYNC_OPEN,
    PATTERN_ASYNC_ALLOCATE,
    PATTERN_ASYNC_TRANSFER,
    PATTERN_ASYNC_SYNC,
    PATTERN_ASYNC_CLOSE,
    PATTERN_ASYNC_COMMIT,
    PATTERN_ASYNC_DECODE,
    PATTERN_ASYNC_CLEANUP_CLOSE,
    PATTERN_ASYNC_CLEANUP_TEMP,
    PATTERN_ASYNC_DONE
} pattern_async_state_t;

typedef struct
{
    persistent_fatfs_file_t file;
    persist_control_pattern_t *load_out;
    uint8_t *encoded;
    uint32_t encoded_capacity;
    uint32_t encoded_size;
    uint32_t offset;
    uint32_t media_epoch;
    pattern_async_state_t state;
    pattern_control_bank_async_operation_t operation;
    uint8_t bank;
    uint8_t pattern;
    uint8_t file_open;
    uint8_t result_ready;
    uint8_t success;
    char final_path[48];
    char temporary_path[52];
    char backup_path[52];
} pattern_async_context_t;

typedef struct
{
    uint8_t *data;
    uint32_t capacity;
    uint32_t position;
} pattern_memory_io_t;

static pattern_async_context_t g_pattern_async;

static uint8_t valid(uint8_t b,uint8_t p){return(b<BANKS&&p<SLOTS)?1U:0U;}
static uint8_t path_for_set(char*out,uint32_t size,uint8_t set,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/S%u/B%02u_P%02u.B6C",set,b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t commit_path(char*out,uint32_t size,uint8_t set,uint8_t temporary){int n=snprintf(out,size,"0:/PATTERN/S%u/COMMIT.%s",set,temporary?"TMP":"BIN");return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN)==0U)return 0U;if(sd_access_fs_mount_if_needed()==0U){sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return 0U;}return 1U;}
static uint32_t crc32(uint32_t crc,const uint8_t*d,uint32_t n){for(uint32_t i=0U;i<n;++i){crc^=d[i];for(uint8_t b=0U;b<8U;++b)crc=(crc>>1U)^(0xEDB88320UL&((uint32_t)-(int32_t)(crc&1U)));}return crc;}
static uint32_t le32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);}
static void put32(uint8_t*p,uint32_t v){for(uint8_t i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(8U*i));}
static uint8_t side_path(char*out,uint32_t size,const char*base,const char*suffix){int n=snprintf(out,size,"%s.%s",base,suffix);return(n>0&&(uint32_t)n<size)?1U:0U;}
static void recover_slot(uint8_t set,uint8_t b,uint8_t p){char x[48],tmp[52],bak[52];if(path_for_set(x,sizeof(x),set,b,p)&&side_path(tmp,sizeof(tmp),x,"TMP")&&side_path(bak,sizeof(bak),x,"BAK"))(void)persistent_fatfs_recover_replace(x,tmp,bak);}
static void scan_active(void){memset(g_present,0,sizeof(g_present));for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48];FILINFO i;recover_slot(g_active_set,b,p);if(path_for_set(x,sizeof(x),g_active_set,b,p)&&f_stat(x,&i)==FR_OK)g_present[b][p]=1U;}}
static void clear_set(uint8_t set){char commit[48],tmp[48];if(commit_path(commit,sizeof(commit),set,0U))(void)f_unlink(commit);if(commit_path(tmp,sizeof(tmp),set,1U))(void)f_unlink(tmp);for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48],slot_tmp[52],bak[52];if(path_for_set(x,sizeof(x),set,b,p)){(void)f_unlink(x);if(side_path(slot_tmp,sizeof(slot_tmp),x,"TMP"))(void)f_unlink(slot_tmp);if(side_path(bak,sizeof(bak),x,"BAK"))(void)f_unlink(bak);}}}
static uint8_t write_commit(uint8_t set,uint32_t generation,const uint8_t*bitmap){uint8_t r[COMMIT_BYTES]={0};r[0]='B';r[1]='6';r[2]='P';r[3]='B';r[4]=1U;r[5]=set;put32(&r[8],generation);memcpy(&r[12],bitmap,32U);put32(&r[40],~crc32(0xFFFFFFFFUL,r,40U));char x[48],tmp[48];FIL f;UINT n=0U;if(!commit_path(x,sizeof(x),set,0U)||!commit_path(tmp,sizeof(tmp),set,1U)||f_open(&f,tmp,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return 0U;uint8_t ok=(f_write(&f,r,sizeof(r),&n)==FR_OK&&n==sizeof(r)&&f_sync(&f)==FR_OK);(void)f_close(&f);if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}if(!ok)(void)f_unlink(tmp);return ok;}
static uint8_t read_commit(uint8_t set,uint32_t*out_generation){uint8_t r[COMMIT_BYTES];char x[48];FIL f;UINT n=0U;if(!commit_path(x,sizeof(x),set,0U)||f_open(&f,x,FA_READ)!=FR_OK)return 0U;uint8_t ok=((uint32_t)f_size(&f)==sizeof(r)&&f_read(&f,r,sizeof(r),&n)==FR_OK&&n==sizeof(r));(void)f_close(&f);ok=(ok&&r[0]=='B'&&r[1]=='6'&&r[2]=='P'&&r[3]=='B'&&r[4]==1U&&r[5]==set&&r[6]==0U&&r[7]==0U&&le32(&r[40])==~crc32(0xFFFFFFFFUL,r,40U));if(ok)*out_generation=le32(&r[8]);return ok;}
static uint8_t store_to_set(uint8_t set,uint8_t b,uint8_t p,const persist_control_pattern_t*in){char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=path_for_set(x,sizeof(x),set,b,p);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_pattern(in,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}return ok;}

static uint8_t begin_staging(void){if(g_active_set>=SET_COUNT||g_staging_set!=INVALID_SET)return 0U;g_staging_set=(uint8_t)(g_active_set^1U);clear_set(g_staging_set);memset(g_staging_bitmap,0,sizeof(g_staging_bitmap));return 1U;}


static uint8_t pattern_memory_write(void *context,
                                    const uint8_t *data,
                                    uint32_t length)
{
    pattern_memory_io_t *const io = context;
    if ((io == NULL) || (data == NULL) || (io->position > io->capacity)
        || (length > (io->capacity - io->position)))
    {
        return 0U;
    }
    memcpy(&io->data[io->position], data, length);
    io->position += length;
    return 1U;
}

static uint8_t pattern_memory_read(void *context,
                                   uint8_t *data,
                                   uint32_t length)
{
    pattern_memory_io_t *const io = context;
    if ((io == NULL) || (data == NULL) || (io->position > io->capacity)
        || (length > (io->capacity - io->position)))
    {
        return 0U;
    }
    memcpy(data, &io->data[io->position], length);
    io->position += length;
    return 1U;
}

static uint8_t pattern_memory_reset(void *context)
{
    pattern_memory_io_t *const io = context;
    if (io == NULL)
    {
        return 0U;
    }
    io->position = 0U;
    return 1U;
}

static uint8_t pattern_memory_size(void *context, uint32_t *out_size)
{
    pattern_memory_io_t *const io = context;
    if ((io == NULL) || (out_size == NULL))
    {
        return 0U;
    }
    *out_size = io->capacity;
    return 1U;
}

static void pattern_async_finish(uint8_t success)
{
    g_pattern_async.file_open = 0U;
    g_pattern_async.success = (success != 0U) ? 1U : 0U;
    g_pattern_async.result_ready = 1U;
    g_pattern_async.state = PATTERN_ASYNC_DONE;
}

static void pattern_async_fail(void)
{
    if (g_pattern_async.file_open != 0U)
    {
        g_pattern_async.state = PATTERN_ASYNC_CLEANUP_CLOSE;
    }
    else if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
    {
        g_pattern_async.state = PATTERN_ASYNC_CLEANUP_TEMP;
    }
    else
    {
        pattern_async_finish(0U);
    }
}

static sd_scheduler_background_admission_t pattern_async_admit(
    sd_scheduler_background_kind_t kind,
    uint32_t byte_count)
{
    const sd_scheduler_background_request_t request = {
        .byte_count = byte_count,
        .media_epoch = g_pattern_async.media_epoch,
        .kind = kind,
    };
    return sd_scheduler_runtime_background_try_begin(&request);
}

uint8_t pattern_control_bank_store_async_begin(
    uint8_t bank,
    uint8_t pattern,
    const persist_control_pattern_t *in,
    uint8_t *encoded,
    uint32_t encoded_capacity)
{
    if (!valid(bank, pattern) || (in == NULL) || (encoded == NULL)
        || (encoded_capacity == 0U)
        || (g_active_set >= SET_COUNT)
        || (g_pattern_async.state != PATTERN_ASYNC_IDLE))
    {
        return 0U;
    }

    pattern_memory_io_t memory = {
        .data = encoded,
        .capacity = encoded_capacity,
        .position = 0U,
    };
    const persist_codec_sink_t sink = {
        .write = pattern_memory_write,
        .context = &memory,
    };
    uint32_t encoded_size = 0U;
    if (persist_codec_encode_pattern(in, &sink, &encoded_size) != PERSIST_CODEC_OK)
    {
        return 0U;
    }

    memset(&g_pattern_async, 0, sizeof(g_pattern_async));
    g_pattern_async.operation = PATTERN_CONTROL_BANK_ASYNC_SAVE;
    g_pattern_async.state = PATTERN_ASYNC_MOUNT;
    g_pattern_async.bank = bank;
    g_pattern_async.pattern = pattern;
    g_pattern_async.encoded = encoded;
    g_pattern_async.encoded_capacity = encoded_capacity;
    g_pattern_async.encoded_size = encoded_size;
    g_pattern_async.media_epoch = sd_access_media_epoch();
    if (!path_for_set(g_pattern_async.final_path,
                      sizeof(g_pattern_async.final_path),
                      g_active_set, bank, pattern)
        || !side_path(g_pattern_async.temporary_path,
                      sizeof(g_pattern_async.temporary_path),
                      g_pattern_async.final_path, "TMP")
        || !side_path(g_pattern_async.backup_path,
                      sizeof(g_pattern_async.backup_path),
                      g_pattern_async.final_path, "BAK"))
    {
        memset(&g_pattern_async, 0, sizeof(g_pattern_async));
        return 0U;
    }
    return 1U;
}

uint8_t pattern_control_bank_load_async_begin(
    uint8_t bank,
    uint8_t pattern,
    uint8_t *encoded,
    uint32_t encoded_capacity,
    persist_control_pattern_t *out)
{
    if (!valid(bank, pattern) || (encoded == NULL) || (encoded_capacity == 0U)
        || (out == NULL) || (g_active_set >= SET_COUNT) || !g_present[bank][pattern]
        || (g_pattern_async.state != PATTERN_ASYNC_IDLE))
    {
        return 0U;
    }
    memset(&g_pattern_async, 0, sizeof(g_pattern_async));
    g_pattern_async.operation = PATTERN_CONTROL_BANK_ASYNC_LOAD;
    g_pattern_async.state = PATTERN_ASYNC_MOUNT;
    g_pattern_async.bank = bank;
    g_pattern_async.pattern = pattern;
    g_pattern_async.encoded = encoded;
    g_pattern_async.encoded_capacity = encoded_capacity;
    g_pattern_async.load_out = out;
    g_pattern_async.media_epoch = sd_access_media_epoch();
    if (!path_for_set(g_pattern_async.final_path,
                      sizeof(g_pattern_async.final_path),
                      g_active_set, bank, pattern))
    {
        memset(&g_pattern_async, 0, sizeof(g_pattern_async));
        return 0U;
    }
    return 1U;
}

void pattern_control_bank_async_service(void)
{
    if ((g_pattern_async.state == PATTERN_ASYNC_IDLE)
        || (g_pattern_async.state == PATTERN_ASYNC_DONE))
    {
        return;
    }

    if (g_pattern_async.state == PATTERN_ASYNC_DECODE)
    {
        pattern_memory_io_t memory = {
            .data = g_pattern_async.encoded,
            .capacity = g_pattern_async.encoded_size,
            .position = 0U,
        };
        const persist_codec_source_t source = {
            .read = pattern_memory_read,
            .reset = pattern_memory_reset,
            .size = pattern_memory_size,
            .context = &memory,
        };
        const persist_codec_result_t result = persist_codec_decode_pattern(
            &source, (persist_codec_pattern_staging_t *)g_pattern_async.load_out);
        pattern_async_finish((result == PERSIST_CODEC_OK) ? 1U : 0U);
        return;
    }

    uint32_t chunk = 0U;
    sd_scheduler_background_kind_t kind = SD_SCHEDULER_BACKGROUND_METADATA;
    if (g_pattern_async.state == PATTERN_ASYNC_TRANSFER)
    {
        chunk = g_pattern_async.encoded_size - g_pattern_async.offset;
        if (chunk > SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES)
        {
            chunk = SD_SCHEDULER_BACKGROUND_MAX_DATA_BYTES;
        }
        kind = SD_SCHEDULER_BACKGROUND_DATA;
    }

    const sd_scheduler_background_admission_t admission =
        pattern_async_admit(kind, chunk);
    if (admission == SD_SCHEDULER_BACKGROUND_NOT_NOW)
    {
        return;
    }
    if (admission != SD_SCHEDULER_BACKGROUND_GO)
    {
        pattern_async_finish(0U);
        return;
    }

    FRESULT fr = FR_OK;
    UINT transferred = 0U;
    switch (g_pattern_async.state)
    {
        case PATTERN_ASYNC_MOUNT:
            if (sd_access_fs_mount_if_needed() == 0U)
            {
                pattern_async_fail();
            }
            else
            {
                g_pattern_async.state =
                    (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
                        ? PATTERN_ASYNC_RECOVER : PATTERN_ASYNC_OPEN;
            }
            break;

        case PATTERN_ASYNC_RECOVER:
            fr = persistent_fatfs_recover_replace(g_pattern_async.final_path,
                                                   g_pattern_async.temporary_path,
                                                   g_pattern_async.backup_path);
            if (fr == FR_OK) g_pattern_async.state = PATTERN_ASYNC_OPEN;
            else pattern_async_fail();
            break;

        case PATTERN_ASYNC_OPEN:
            if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
            {
                fr = persistent_fatfs_open_write_result(
                    &g_pattern_async.file, g_pattern_async.temporary_path);
            }
            else if (persistent_fatfs_open_read(
                         &g_pattern_async.file, g_pattern_async.final_path) == 0U)
            {
                fr = FR_DISK_ERR;
            }
            if (fr != FR_OK)
            {
                pattern_async_fail();
                break;
            }
            g_pattern_async.file_open = 1U;
            if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_LOAD)
            {
                g_pattern_async.encoded_size = g_pattern_async.file.size;
                if ((g_pattern_async.encoded_size < PERSIST_CODEC_HEADER_BYTES)
                    || (g_pattern_async.encoded_size
                        > g_pattern_async.encoded_capacity))
                {
                    pattern_async_fail();
                    break;
                }
            }
            g_pattern_async.state =
                (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
                    ? PATTERN_ASYNC_ALLOCATE : PATTERN_ASYNC_TRANSFER;
            break;

        case PATTERN_ASYNC_ALLOCATE:
            fr = f_lseek(&g_pattern_async.file.file,
                         (FSIZE_t)(g_pattern_async.encoded_size - 1U));
            if (fr == FR_OK)
            {
                fr = f_write(&g_pattern_async.file.file,
                             &g_pattern_async.encoded[
                                 g_pattern_async.encoded_size - 1U],
                             1U, &transferred);
            }
            if ((fr == FR_OK) && (transferred == 1U))
            {
                fr = f_lseek(&g_pattern_async.file.file, 0U);
            }
            if (fr == FR_OK) g_pattern_async.state = PATTERN_ASYNC_TRANSFER;
            else pattern_async_fail();
            break;

        case PATTERN_ASYNC_TRANSFER:
            if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
            {
                fr = f_write(&g_pattern_async.file.file,
                             &g_pattern_async.encoded[g_pattern_async.offset],
                             chunk, &transferred);
            }
            else
            {
                fr = f_read(&g_pattern_async.file.file,
                            &g_pattern_async.encoded[g_pattern_async.offset],
                            chunk, &transferred);
            }
            if ((fr != FR_OK) || (transferred != chunk))
            {
                pattern_async_fail();
                break;
            }
            g_pattern_async.offset += chunk;
            if (g_pattern_async.offset == g_pattern_async.encoded_size)
            {
                g_pattern_async.state =
                    (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
                        ? PATTERN_ASYNC_SYNC : PATTERN_ASYNC_CLOSE;
            }
            break;

        case PATTERN_ASYNC_SYNC:
            if (f_sync(&g_pattern_async.file.file) == FR_OK)
                g_pattern_async.state = PATTERN_ASYNC_CLOSE;
            else pattern_async_fail();
            break;

        case PATTERN_ASYNC_CLOSE:
            fr = persistent_fatfs_close_result(&g_pattern_async.file);
            g_pattern_async.file_open = 0U;
            if (fr != FR_OK)
            {
                pattern_async_fail();
            }
            else if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
            {
                g_pattern_async.state = PATTERN_ASYNC_COMMIT;
            }
            else
            {
                g_pattern_async.state = PATTERN_ASYNC_DECODE;
            }
            break;

        case PATTERN_ASYNC_COMMIT:
            fr = persistent_fatfs_commit_replace(g_pattern_async.final_path,
                                                  g_pattern_async.temporary_path,
                                                  g_pattern_async.backup_path);
            if (fr == FR_OK)
            {
                g_present[g_pattern_async.bank][g_pattern_async.pattern] = 1U;
                pattern_async_finish(1U);
            }
            else
            {
                pattern_async_fail();
            }
            break;

        case PATTERN_ASYNC_CLEANUP_CLOSE:
            (void)persistent_fatfs_close_result(&g_pattern_async.file);
            g_pattern_async.file_open = 0U;
            g_pattern_async.state =
                (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_SAVE)
                    ? PATTERN_ASYNC_CLEANUP_TEMP : PATTERN_ASYNC_DONE;
            if (g_pattern_async.operation == PATTERN_CONTROL_BANK_ASYNC_LOAD)
                pattern_async_finish(0U);
            break;

        case PATTERN_ASYNC_CLEANUP_TEMP:
            fr = f_unlink(g_pattern_async.temporary_path);
            (void)fr;
            pattern_async_finish(0U);
            break;

        default:
            pattern_async_finish(0U);
            break;
    }
    sd_scheduler_runtime_background_end();
}

uint8_t pattern_control_bank_async_busy(void)
{
    return (g_pattern_async.state != PATTERN_ASYNC_IDLE) ? 1U : 0U;
}

uint8_t pattern_control_bank_async_take_result(
    pattern_control_bank_async_operation_t *operation,
    uint8_t *bank,
    uint8_t *pattern,
    uint8_t *success)
{
    if ((g_pattern_async.state != PATTERN_ASYNC_DONE)
        || (g_pattern_async.result_ready == 0U))
    {
        return 0U;
    }
    if (operation != NULL) *operation = g_pattern_async.operation;
    if (bank != NULL) *bank = g_pattern_async.bank;
    if (pattern != NULL) *pattern = g_pattern_async.pattern;
    if (success != NULL) *success = g_pattern_async.success;
    memset(&g_pattern_async, 0, sizeof(g_pattern_async));
    return 1U;
}

void pattern_control_bank_init(void){memset(g_present,0,sizeof(g_present));memset(&g_pattern_async,0,sizeof(g_pattern_async));g_active_set=INVALID_SET;g_staging_set=INVALID_SET;g_generation=0U;if(!acquire())return;(void)f_mkdir("0:/PATTERN");(void)f_mkdir("0:/PATTERN/S0");(void)f_mkdir("0:/PATTERN/S1");uint32_t g0=0U,g1=0U;uint8_t v0=read_commit(0U,&g0),v1=read_commit(1U,&g1);if(v0&&v1&&g0!=g1){g_active_set=((int32_t)(g1-g0)>0)?1U:0U;g_generation=(g_active_set==1U)?g1:g0;}else if(v0&&!v1){g_active_set=0U;g_generation=g0;}else if(v1&&!v0){g_active_set=1U;g_generation=g1;}if(g_active_set==INVALID_SET){clear_set(0U);clear_set(1U);uint8_t blank[32]={0};if(write_commit(0U,1U,blank)){g_active_set=0U;g_generation=1U;}}if(g_active_set<SET_COUNT)scan_active();else memset(g_present,0,sizeof(g_present));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);}
uint8_t pattern_control_bank_present(uint8_t b,uint8_t p){return valid(b,p)?g_present[b][p]:0U;}
uint8_t pattern_control_bank_delete(uint8_t b,uint8_t p){if(!valid(b,p)||!acquire())return 0U;char x[48],tmp[52],bak[52];uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&side_path(tmp,sizeof(tmp),x,"TMP")&&side_path(bak,sizeof(bak),x,"BAK");if(ok){FRESULT r=f_unlink(x);ok=(r==FR_OK||r==FR_NO_FILE);(void)f_unlink(tmp);(void)f_unlink(bak);}if(ok)g_present[b][p]=0U;sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;}
uint8_t pattern_control_bank_clear(void){if(!acquire())return 0U;uint8_t ok=1U;for(uint8_t b=0U;b<BANKS;++b)for(uint8_t p=0U;p<SLOTS;++p){char x[48],tmp[52],bak[52];if(!path_for_set(x,sizeof(x),g_active_set,b,p)||!side_path(tmp,sizeof(tmp),x,"TMP")||!side_path(bak,sizeof(bak),x,"BAK")){ok=0U;continue;}FRESULT r=f_unlink(x);if(r!=FR_OK&&r!=FR_NO_FILE)ok=0U;(void)f_unlink(tmp);(void)f_unlink(bak);}if(ok)memset(g_present,0,sizeof(g_present));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return ok;}
uint16_t pattern_control_bank_count(void){uint16_t n=0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)n+=g_present[b][p]?1U:0U;return n;}
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){char x[48];persistent_fatfs_file_t f;memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)&out->content)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}return ok;}return 0U;}
uint8_t pattern_control_bank_get_ordinal_project_path(uint16_t ordinal,char*out_path,uint32_t path_capacity,uint8_t*out_bank,uint8_t*out_pattern){if(out_path==NULL||path_capacity==0U||out_bank==NULL||out_pattern==NULL||g_active_set>=SET_COUNT)return 0U;for(uint8_t b=0U;b<BANKS;++b)for(uint8_t p=0U;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){if(!path_for_set(out_path,path_capacity,g_active_set,b,p))return 0U;*out_bank=b;*out_pattern=p;return 1U;}return 0U;}
uint8_t pattern_control_bank_begin_project(void){return begin_staging();}
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*r){if(r==NULL||r->present!=1U||!valid(r->bank,r->pattern)||g_staging_set>=SET_COUNT||!store_to_set(g_staging_set,r->bank,r->pattern,&r->content))return 0U;uint8_t slot=(uint8_t)(r->bank*SLOTS+r->pattern);g_staging_bitmap[slot>>3U]|=(uint8_t)(1U<<(slot&7U));return 1U;}
uint8_t pattern_control_bank_commit(void*context){(void)context;if(g_staging_set>=SET_COUNT)return 0U;const uint8_t old=g_active_set;const uint32_t next=g_generation+1U;if(!write_commit(g_staging_set,next,g_staging_bitmap))return 0U;g_active_set=g_staging_set;g_generation=next;g_staging_set=INVALID_SET;scan_active();clear_set(old);return 1U;}
void pattern_control_bank_abort(void*context){(void)context;if(g_staging_set<SET_COUNT){clear_set(g_staging_set);g_staging_set=INVALID_SET;}}
