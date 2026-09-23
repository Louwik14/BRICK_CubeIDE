#include "Storage/boot_context_sd.h"
#include <stddef.h>
#include "Storage/sd_access_gate.h"
#include "ff.h"
#define FINAL_PATH "0:/BRICK/BOOT.B6C"
#define TMP_PATH "0:/BRICK/BOOT.TMP"
#define BOOT_MAGIC 0x544F4F42UL
typedef struct { uint32_t magic; uint16_t version,size; uint8_t slot,reserved[3]; uint32_t crc; } record_t;
static uint32_t crc32(const void *data,uint32_t size){const uint8_t*p=data;uint32_t c=0xFFFFFFFFUL;while(size--){c^=*p++;for(uint32_t b=0;b<8U;b++)c=(c>>1)^((0U-(c&1U))&0xEDB88320UL);}return ~c;}
static uint8_t acquire(void){return sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PROJECT);}
uint8_t boot_context_sd_load(boot_context_sd_data_t*out){if(out==0||!acquire())return 0U;record_t r;FIL f;UINT n=0U;uint8_t ok=0U;if(sd_access_fs_mount_if_needed()&&f_open(&f,FINAL_PATH,FA_READ)==FR_OK){FSIZE_t s=f_size(&f);FRESULT rr=f_read(&f,&r,sizeof(r),&n);FRESULT cr=f_close(&f);if(rr==FR_OK&&cr==FR_OK&&s==sizeof(r)&&n==sizeof(r)&&r.magic==BOOT_MAGIC&&r.version==1U&&r.size==sizeof(r)&&r.crc==crc32(&r,offsetof(record_t,crc))){out->active_project_slot=r.slot;ok=1U;}}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return ok;}
uint8_t boot_context_sd_commit(uint8_t slot){if(!acquire())return 0U;record_t r={.magic=BOOT_MAGIC,.version=1U,.size=sizeof(r),.slot=slot};r.crc=crc32(&r,offsetof(record_t,crc));FIL f;UINT n=0U;uint8_t ok=0U;if(sd_access_fs_mount_if_needed()){(void)f_mkdir("0:/BRICK");(void)f_unlink(TMP_PATH);if(f_open(&f,TMP_PATH,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){ok=(f_write(&f,&r,sizeof(r),&n)==FR_OK&&n==sizeof(r)&&f_sync(&f)==FR_OK&&f_close(&f)==FR_OK);if(ok){(void)f_unlink(FINAL_PATH);ok=(f_rename(TMP_PATH,FINAL_PATH)==FR_OK);}}if(!ok)(void)f_unlink(TMP_PATH);}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);return ok;}
void boot_context_sd_clear(void){if(!acquire())return;if(sd_access_fs_mount_if_needed()){(void)f_unlink(TMP_PATH);(void)f_unlink(FINAL_PATH);}sd_access_gate_release(SD_ACCESS_CLIENT_PROJECT);}
