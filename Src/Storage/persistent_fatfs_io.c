#include "Storage/persistent_fatfs_io.h"
#include <string.h>
static uint8_t io_write(void *ctx,const uint8_t *data,uint32_t length){persistent_fatfs_file_t*f=ctx;UINT done=0U;if(f==NULL||data==NULL||f_write(&f->file,data,length,&done)!=FR_OK||done!=length)return 0U;f->size+=done;return 1U;}
static uint8_t io_read(void *ctx,uint8_t *data,uint32_t length){persistent_fatfs_file_t*f=ctx;UINT done=0U;return(f!=NULL&&data!=NULL&&f_read(&f->file,data,length,&done)==FR_OK&&done==length)?1U:0U;}
static uint8_t io_reset(void *ctx){persistent_fatfs_file_t*f=ctx;return(f!=NULL&&f_lseek(&f->file,0U)==FR_OK)?1U:0U;}
static uint8_t io_size(void *ctx,uint32_t*out){persistent_fatfs_file_t*f=ctx;if(f==NULL||out==NULL)return 0U;*out=(uint32_t)f_size(&f->file);return 1U;}
uint8_t persistent_fatfs_open_read(persistent_fatfs_file_t*f,const char*path){if(f==NULL||path==NULL)return 0U;memset(f,0,sizeof(*f));if(f_open(&f->file,path,FA_READ)!=FR_OK)return 0U;f->size=(uint32_t)f_size(&f->file);return 1U;}
uint8_t persistent_fatfs_open_write(persistent_fatfs_file_t*f,const char*path){if(f==NULL||path==NULL)return 0U;memset(f,0,sizeof(*f));return(f_open(&f->file,path,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK)?1U:0U;}
void persistent_fatfs_close(persistent_fatfs_file_t*f){if(f!=NULL)(void)f_close(&f->file);}
persist_codec_source_t persistent_fatfs_source(persistent_fatfs_file_t*f){return(persist_codec_source_t){io_read,io_reset,io_size,f};}
persist_codec_sink_t persistent_fatfs_sink(persistent_fatfs_file_t*f){return(persist_codec_sink_t){io_write,f};}
