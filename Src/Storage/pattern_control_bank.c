#include "Storage/pattern_control_bank.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/sd_access_gate.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

#define BANKS 16U
#define SLOTS 16U
#define SET_COUNT 2U
#define INVALID_SET 0xFFU

static uint8_t g_present[BANKS][SLOTS];
static uint8_t g_active_set;
static uint8_t g_staging_set = INVALID_SET;

static uint8_t valid(uint8_t b,uint8_t p){return(b<BANKS&&p<SLOTS)?1U:0U;}
static uint8_t path_for_set(char*out,uint32_t size,uint8_t set,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/S%u/B%02u_P%02u.B6C",set,b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t legacy_path(char*out,uint32_t size,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/B%02u_P%02u.B6C",b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t marker_path(char*out,uint32_t size,uint8_t temporary){int n=snprintf(out,size,"0:/PATTERN/ACTIVE.%s",temporary?"TMP":"BIN");return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN)==0U)return 0U;if(sd_access_fs_mount_if_needed()==0U){sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return 0U;}return 1U;}
static void scan_active(void){memset(g_present,0,sizeof(g_present));for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48];FILINFO i;if(path_for_set(x,sizeof(x),g_active_set,b,p)&&f_stat(x,&i)==FR_OK)g_present[b][p]=1U;}}
static void clear_set(uint8_t set){for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48];if(path_for_set(x,sizeof(x),set,b,p))(void)f_unlink(x);}}
static uint8_t write_marker(uint8_t set){char x[40],tmp[40];FIL f;UINT n=0U;if(!marker_path(x,sizeof(x),0U)||!marker_path(tmp,sizeof(tmp),1U)||f_open(&f,tmp,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return 0U;uint8_t ok=(f_write(&f,&set,1U,&n)==FR_OK&&n==1U&&f_sync(&f)==FR_OK);(void)f_close(&f);if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}if(!ok)(void)f_unlink(tmp);return ok;}
static uint8_t read_marker(uint8_t*out){char x[40];FIL f;UINT n=0U;uint8_t set=INVALID_SET;if(!marker_path(x,sizeof(x),0U)||f_open(&f,x,FA_READ)!=FR_OK)return 0U;uint8_t ok=(f_read(&f,&set,1U,&n)==FR_OK&&n==1U&&set<SET_COUNT);(void)f_close(&f);if(ok)*out=set;return ok;}
static void migrate_initial_set(void){for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char old[48],next[48];FILINFO i;if(legacy_path(old,sizeof(old),b,p)&&path_for_set(next,sizeof(next),0U,b,p)&&f_stat(old,&i)==FR_OK)(void)f_rename(old,next);}}
static uint8_t store_to_set(uint8_t set,uint8_t b,uint8_t p,const persist_control_pattern_t*in){char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=path_for_set(x,sizeof(x),set,b,p);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_pattern(in,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}return ok;}

void pattern_control_bank_init(void){memset(g_present,0,sizeof(g_present));g_active_set=0U;g_staging_set=INVALID_SET;if(!acquire())return;(void)f_mkdir("0:/PATTERN");(void)f_mkdir("0:/PATTERN/S0");(void)f_mkdir("0:/PATTERN/S1");if(!read_marker(&g_active_set)){migrate_initial_set();(void)write_marker(g_active_set);}scan_active();sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);}
uint8_t pattern_control_bank_present(uint8_t b,uint8_t p){return valid(b,p)?g_present[b][p]:0U;}
uint8_t pattern_control_bank_load(uint8_t b,uint8_t p,persist_control_pattern_t*out){if(!valid(b,p)||out==NULL||!g_present[b][p]||!acquire())return 0U;char x[48];persistent_fatfs_file_t f;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)out)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(!ok)g_present[b][p]=0U;return ok;}
uint8_t pattern_control_bank_store(uint8_t b,uint8_t p,const persist_control_pattern_t*in){if(!valid(b,p)||in==NULL||!acquire())return 0U;uint8_t ok=store_to_set(g_active_set,b,p,in);sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=1U;return ok;}
uint8_t pattern_control_bank_delete(uint8_t b,uint8_t p){if(!valid(b,p)||!acquire())return 0U;char x[48];FRESULT r=FR_INVALID_NAME;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p);if(ok)r=f_unlink(x);ok=(ok&&(r==FR_OK||r==FR_NO_FILE));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=0U;return ok;}
uint16_t pattern_control_bank_count(void){uint16_t n=0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)n+=g_present[b][p]?1U:0U;return n;}
uint8_t pattern_control_bank_get_ordinal(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;return pattern_control_bank_load(b,p,&out->content);}return 0U;}
uint8_t pattern_control_bank_put_record(const persist_control_pattern_record_t*r){return(r!=NULL&&r->present==1U)?pattern_control_bank_store(r->bank,r->pattern,&r->content):0U;}
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){char x[48];persistent_fatfs_file_t f;memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)&out->content)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}return ok;}return 0U;}
uint8_t pattern_control_bank_begin_project(void){if(g_staging_set!=INVALID_SET)return 0U;g_staging_set=(uint8_t)(g_active_set^1U);clear_set(g_staging_set);return 1U;}
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*r){return(r!=NULL&&r->present==1U&&valid(r->bank,r->pattern)&&g_staging_set<SET_COUNT)?store_to_set(g_staging_set,r->bank,r->pattern,&r->content):0U;}
uint8_t pattern_control_bank_commit(void*context){(void)context;if(g_staging_set>=SET_COUNT)return 0U;const uint8_t old=g_active_set;if(!write_marker(g_staging_set))return 0U;g_active_set=g_staging_set;g_staging_set=INVALID_SET;scan_active();clear_set(old);return 1U;}
void pattern_control_bank_abort(void*context){(void)context;if(g_staging_set<SET_COUNT){clear_set(g_staging_set);g_staging_set=INVALID_SET;}}
