#include "Storage/persistent_fatfs_io.h"
#include <string.h>
static uint8_t io_write(void *ctx,const uint8_t *data,uint32_t length){persistent_fatfs_file_t*f=ctx;UINT done=0U;if(f==NULL||data==NULL)return 0U;f->requested=length;f->last_result=f_write(&f->file,data,length,&done);f->transferred=done;if(f->last_result!=FR_OK||done!=length)return 0U;f->size+=done;return 1U;}
static uint8_t io_read(void *ctx,uint8_t *data,uint32_t length){persistent_fatfs_file_t*f=ctx;UINT done=0U;if(f==NULL||data==NULL)return 0U;f->requested=length;f->last_result=f_read(&f->file,data,length,&done);f->transferred=done;return(f->last_result==FR_OK&&done==length)?1U:0U;}
static uint8_t io_reset(void *ctx){persistent_fatfs_file_t*f=ctx;return(f!=NULL&&f_lseek(&f->file,0U)==FR_OK)?1U:0U;}
static uint8_t io_size(void *ctx,uint32_t*out){persistent_fatfs_file_t*f=ctx;if(f==NULL||out==NULL)return 0U;*out=(uint32_t)f_size(&f->file);return 1U;}
uint8_t persistent_fatfs_open_read(persistent_fatfs_file_t*f,const char*path){if(f==NULL||path==NULL)return 0U;memset(f,0,sizeof(*f));if(f_open(&f->file,path,FA_READ)!=FR_OK)return 0U;f->size=(uint32_t)f_size(&f->file);return 1U;}
FRESULT persistent_fatfs_open_write_result(persistent_fatfs_file_t*f,const char*path){if(f==NULL||path==NULL)return FR_INVALID_PARAMETER;memset(f,0,sizeof(*f));f->last_result=f_open(&f->file,path,FA_CREATE_ALWAYS|FA_WRITE);return f->last_result;}
uint8_t persistent_fatfs_open_write(persistent_fatfs_file_t*f,const char*path){return(persistent_fatfs_open_write_result(f,path)==FR_OK)?1U:0U;}
void persistent_fatfs_close(persistent_fatfs_file_t*f){if(f!=NULL)(void)f_close(&f->file);}
FRESULT persistent_fatfs_close_result(persistent_fatfs_file_t*f){return(f!=NULL)?f_close(&f->file):FR_INVALID_OBJECT;}

static uint8_t io_exists(const char *path)
{
    FILINFO info;
    return (path != NULL && f_stat(path, &info) == FR_OK) ? 1U : 0U;
}

FRESULT persistent_fatfs_recover_replace(const char *final_path,
                                         const char *temporary_path,
                                         const char *backup_path)
{
    if(final_path==NULL||temporary_path==NULL||backup_path==NULL)return FR_INVALID_PARAMETER;
    const uint8_t final_exists=io_exists(final_path);
    const uint8_t backup_exists=io_exists(backup_path);
    if(final_exists!=0U)
    {
        FRESULT fr=f_unlink(temporary_path);if(fr!=FR_OK&&fr!=FR_NO_FILE)return fr;
        fr=f_unlink(backup_path);return(fr==FR_OK||fr==FR_NO_FILE)?FR_OK:fr;
    }
    if(backup_exists!=0U)
    {
        FRESULT fr=f_rename(backup_path,final_path);if(fr!=FR_OK)return fr;
    }
    FRESULT fr=f_unlink(temporary_path);return(fr==FR_OK||fr==FR_NO_FILE)?FR_OK:fr;
}

FRESULT persistent_fatfs_commit_replace(const char *final_path,
                                        const char *temporary_path,
                                        const char *backup_path)
{
    if(final_path==NULL||temporary_path==NULL||backup_path==NULL)return FR_INVALID_PARAMETER;
    const uint8_t had_final=io_exists(final_path);
    FRESULT fr=f_unlink(backup_path);if(fr!=FR_OK&&fr!=FR_NO_FILE)return fr;
    if(had_final!=0U)
    {
        fr=f_rename(final_path,backup_path);if(fr!=FR_OK)return fr;
    }
    fr=f_rename(temporary_path,final_path);
    if(fr!=FR_OK)
    {
        if(had_final!=0U)(void)f_rename(backup_path,final_path);
        return fr;
    }
    if(had_final!=0U)
    {
        (void)f_unlink(backup_path);
    }
    return FR_OK;
}
persist_codec_source_t persistent_fatfs_source(persistent_fatfs_file_t*f){return(persist_codec_source_t){io_read,io_reset,io_size,f};}
persist_codec_sink_t persistent_fatfs_sink(persistent_fatfs_file_t*f){return(persist_codec_sink_t){io_write,f};}
