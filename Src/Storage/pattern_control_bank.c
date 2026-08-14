#include "Storage/pattern_control_bank.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#define BANKS 16U
#define SLOTS 16U
static uint8_t g_present[BANKS][SLOTS];
static uint8_t valid(uint8_t b,uint8_t p){return(b<BANKS&&p<SLOTS)?1U:0U;}
static uint8_t path(char*out,uint32_t size,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/B%02u_P%02u.B6C",b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN)==0U)return 0U;if(sd_access_fs_mount_if_needed()==0U){sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return 0U;}return 1U;}
void pattern_control_bank_init(void){memset(g_present,0,sizeof(g_present));if(acquire()==0U)return;(void)f_mkdir("0:/PATTERN");for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[40];FILINFO i;if(path(x,sizeof(x),b,p)&&f_stat(x,&i)==FR_OK)g_present[b][p]=1U;}sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);}
uint8_t pattern_control_bank_present(uint8_t b,uint8_t p){return valid(b,p)?g_present[b][p]:0U;}
uint8_t pattern_control_bank_load(uint8_t b,uint8_t p,persist_control_pattern_t*out){if(!valid(b,p)||out==NULL||!g_present[b][p]||!acquire())return 0U;char x[40];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)out)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(!ok)g_present[b][p]=0U;return ok;}
uint8_t pattern_control_bank_store(uint8_t b,uint8_t p,const persist_control_pattern_t*in){if(!valid(b,p)||in==NULL||!acquire())return 0U;char x[40],tmp[44];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),b,p);if(ok){(void)snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_pattern(in,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=1U;return ok;}
uint8_t pattern_control_bank_delete(uint8_t b,uint8_t p){if(!valid(b,p)||!acquire())return 0U;char x[40];FRESULT r=FR_INVALID_NAME;uint8_t ok=path(x,sizeof(x),b,p);if(ok)r=f_unlink(x);ok=(ok&&(r==FR_OK||r==FR_NO_FILE));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=0U;return ok;}
uint16_t pattern_control_bank_count(void){uint16_t n=0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)n+=g_present[b][p]?1U:0U;return n;}
uint8_t pattern_control_bank_get_ordinal(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]){if(ordinal--==0U){memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;return pattern_control_bank_load(b,p,&out->content);}}return 0U;}
uint8_t pattern_control_bank_put_record(const persist_control_pattern_record_t*r){return(r!=NULL&&r->present==1U)?pattern_control_bank_store(r->bank,r->pattern,&r->content):0U;}
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){char x[40];persistent_fatfs_file_t f;memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;uint8_t ok=path(x,sizeof(x),b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)&out->content)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}return ok;}return 0U;}
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*r){if(r==NULL||r->present!=1U||!valid(r->bank,r->pattern))return 0U;char x[40],tmp[44];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),r->bank,r->pattern);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_pattern(&r->content,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}if(ok)g_present[r->bank][r->pattern]=1U;return ok;}
void pattern_control_bank_clear_project(void){for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]){char x[40];if(path(x,sizeof(x),b,p))(void)f_unlink(x);g_present[b][p]=0U;}}
uint8_t pattern_control_bank_commit(void*context){(void)context;return 1U;}
void pattern_control_bank_abort(void*context){(void)context;}
