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
#define COMMIT_BYTES 44U

static uint8_t g_present[BANKS][SLOTS];
static uint8_t g_active_set;
static uint8_t g_staging_set = INVALID_SET;
static uint32_t g_generation;
static uint8_t g_staging_bitmap[32];

static uint8_t valid(uint8_t b,uint8_t p){return(b<BANKS&&p<SLOTS)?1U:0U;}
static uint8_t path_for_set(char*out,uint32_t size,uint8_t set,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/S%u/B%02u_P%02u.B6C",set,b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t legacy_path(char*out,uint32_t size,uint8_t b,uint8_t p){int n=snprintf(out,size,"0:/PATTERN/B%02u_P%02u.B6C",b,p);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t commit_path(char*out,uint32_t size,uint8_t set,uint8_t temporary){int n=snprintf(out,size,"0:/PATTERN/S%u/COMMIT.%s",set,temporary?"TMP":"BIN");return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATTERN)==0U)return 0U;if(sd_access_fs_mount_if_needed()==0U){sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);return 0U;}return 1U;}
static uint32_t crc32(uint32_t crc,const uint8_t*d,uint32_t n){for(uint32_t i=0U;i<n;++i){crc^=d[i];for(uint8_t b=0U;b<8U;++b)crc=(crc>>1U)^(0xEDB88320UL&((uint32_t)-(int32_t)(crc&1U)));}return crc;}
static uint32_t le32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);}
static void put32(uint8_t*p,uint32_t v){for(uint8_t i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(8U*i));}
static void scan_bitmap(uint8_t set,uint8_t*out){memset(out,0,32U);for(uint8_t b=0U;b<BANKS;++b)for(uint8_t p=0U;p<SLOTS;++p){char x[48];FILINFO i;if(path_for_set(x,sizeof(x),set,b,p)&&f_stat(x,&i)==FR_OK){uint8_t slot=(uint8_t)(b*SLOTS+p);out[slot>>3U]|=(uint8_t)(1U<<(slot&7U));}}}
static void scan_active(void){memset(g_present,0,sizeof(g_present));for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48];FILINFO i;if(path_for_set(x,sizeof(x),g_active_set,b,p)&&f_stat(x,&i)==FR_OK)g_present[b][p]=1U;}}
static void clear_set(uint8_t set){char commit[48],tmp[48];if(commit_path(commit,sizeof(commit),set,0U))(void)f_unlink(commit);if(commit_path(tmp,sizeof(tmp),set,1U))(void)f_unlink(tmp);for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char x[48];if(path_for_set(x,sizeof(x),set,b,p))(void)f_unlink(x);}}
static uint8_t write_commit(uint8_t set,uint32_t generation,const uint8_t*bitmap){uint8_t r[COMMIT_BYTES]={0};r[0]='B';r[1]='6';r[2]='P';r[3]='B';r[4]=1U;r[5]=set;put32(&r[8],generation);memcpy(&r[12],bitmap,32U);put32(&r[40],~crc32(0xFFFFFFFFUL,r,40U));char x[48],tmp[48];FIL f;UINT n=0U;if(!commit_path(x,sizeof(x),set,0U)||!commit_path(tmp,sizeof(tmp),set,1U)||f_open(&f,tmp,FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK)return 0U;uint8_t ok=(f_write(&f,r,sizeof(r),&n)==FR_OK&&n==sizeof(r)&&f_sync(&f)==FR_OK);(void)f_close(&f);if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}if(!ok)(void)f_unlink(tmp);return ok;}
static uint8_t read_commit(uint8_t set,uint32_t*out_generation){uint8_t r[COMMIT_BYTES],actual[32];char x[48];FIL f;UINT n=0U;if(!commit_path(x,sizeof(x),set,0U)||f_open(&f,x,FA_READ)!=FR_OK)return 0U;uint8_t ok=((uint32_t)f_size(&f)==sizeof(r)&&f_read(&f,r,sizeof(r),&n)==FR_OK&&n==sizeof(r));(void)f_close(&f);ok=(ok&&r[0]=='B'&&r[1]=='6'&&r[2]=='P'&&r[3]=='B'&&r[4]==1U&&r[5]==set&&r[6]==0U&&r[7]==0U&&le32(&r[40])==~crc32(0xFFFFFFFFUL,r,40U));if(ok){scan_bitmap(set,actual);ok=(memcmp(actual,&r[12],32U)==0);}if(ok)*out_generation=le32(&r[8]);return ok;}
static uint8_t read_legacy_marker(uint8_t*out){FIL f;UINT n=0U;uint8_t set=INVALID_SET;if(f_open(&f,"0:/PATTERN/ACTIVE.BIN",FA_READ)!=FR_OK)return 0U;uint8_t ok=(f_read(&f,&set,1U,&n)==FR_OK&&n==1U&&set<SET_COUNT);(void)f_close(&f);if(ok)*out=set;return ok;}
static void migrate_initial_set(void){for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p){char old[48],next[48];FILINFO i;if(legacy_path(old,sizeof(old),b,p)&&path_for_set(next,sizeof(next),0U,b,p)&&f_stat(old,&i)==FR_OK)(void)f_rename(old,next);}}
static uint8_t store_to_set(uint8_t set,uint8_t b,uint8_t p,const persist_control_pattern_t*in){char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=path_for_set(x,sizeof(x),set,b,p);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_pattern(in,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}return ok;}

void pattern_control_bank_init(void){memset(g_present,0,sizeof(g_present));g_active_set=INVALID_SET;g_staging_set=INVALID_SET;g_generation=0U;if(!acquire())return;(void)f_mkdir("0:/PATTERN");(void)f_mkdir("0:/PATTERN/S0");(void)f_mkdir("0:/PATTERN/S1");uint32_t g0=0U,g1=0U;uint8_t v0=read_commit(0U,&g0),v1=read_commit(1U,&g1);if(v0&&v1&&g0!=g1){g_active_set=((int32_t)(g1-g0)>0)?1U:0U;g_generation=(g_active_set==1U)?g1:g0;}else if(v0&&!v1){g_active_set=0U;g_generation=g0;}else if(v1&&!v0){g_active_set=1U;g_generation=g1;}else if(!v0&&!v1){uint8_t candidate;if(!read_legacy_marker(&candidate)){candidate=0U;migrate_initial_set();}uint8_t bitmap[32];scan_bitmap(candidate,bitmap);if(write_commit(candidate,1U,bitmap)){g_active_set=candidate;g_generation=1U;}}if(g_active_set==INVALID_SET){clear_set(0U);clear_set(1U);uint8_t blank[32]={0};if(write_commit(0U,1U,blank)){g_active_set=0U;g_generation=1U;}}(void)f_unlink("0:/PATTERN/ACTIVE.BIN");(void)f_unlink("0:/PATTERN/ACTIVE.TMP");if(g_active_set<SET_COUNT)scan_active();else memset(g_present,0,sizeof(g_present));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);}
uint8_t pattern_control_bank_present(uint8_t b,uint8_t p){return valid(b,p)?g_present[b][p]:0U;}
uint8_t pattern_control_bank_load(uint8_t b,uint8_t p,persist_control_pattern_t*out){if(!valid(b,p)||out==NULL||!g_present[b][p]||!acquire())return 0U;char x[48];persistent_fatfs_file_t f;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)out)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(!ok)g_present[b][p]=0U;return ok;}
uint8_t pattern_control_bank_store(uint8_t b,uint8_t p,const persist_control_pattern_t*in){if(!valid(b,p)||in==NULL||!acquire())return 0U;uint8_t ok=store_to_set(g_active_set,b,p,in);sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=1U;return ok;}
uint8_t pattern_control_bank_delete(uint8_t b,uint8_t p){if(!valid(b,p)||!acquire())return 0U;char x[48];FRESULT r=FR_INVALID_NAME;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p);if(ok)r=f_unlink(x);ok=(ok&&(r==FR_OK||r==FR_NO_FILE));sd_access_gate_release(SD_ACCESS_CLIENT_PATTERN);if(ok)g_present[b][p]=0U;return ok;}
uint16_t pattern_control_bank_count(void){uint16_t n=0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)n+=g_present[b][p]?1U:0U;return n;}
uint8_t pattern_control_bank_get_ordinal(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;return pattern_control_bank_load(b,p,&out->content);}return 0U;}
uint8_t pattern_control_bank_put_record(const persist_control_pattern_record_t*r){return(r!=NULL&&r->present==1U)?pattern_control_bank_store(r->bank,r->pattern,&r->content):0U;}
uint8_t pattern_control_bank_get_ordinal_project(uint16_t ordinal,persist_control_pattern_record_t*out){if(out==NULL)return 0U;for(uint8_t b=0;b<BANKS;++b)for(uint8_t p=0;p<SLOTS;++p)if(g_present[b][p]&&ordinal--==0U){char x[48];persistent_fatfs_file_t f;memset(out,0,sizeof(*out));out->bank=b;out->pattern=p;out->present=1U;uint8_t ok=path_for_set(x,sizeof(x),g_active_set,b,p)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_pattern(&s,(persist_codec_pattern_staging_t*)&out->content)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}return ok;}return 0U;}
uint8_t pattern_control_bank_begin_project(void){if(g_active_set>=SET_COUNT||g_staging_set!=INVALID_SET)return 0U;g_staging_set=(uint8_t)(g_active_set^1U);clear_set(g_staging_set);memset(g_staging_bitmap,0,sizeof(g_staging_bitmap));return 1U;}
uint8_t pattern_control_bank_put_record_project(const persist_control_pattern_record_t*r){if(r==NULL||r->present!=1U||!valid(r->bank,r->pattern)||g_staging_set>=SET_COUNT||!store_to_set(g_staging_set,r->bank,r->pattern,&r->content))return 0U;uint8_t slot=(uint8_t)(r->bank*SLOTS+r->pattern);g_staging_bitmap[slot>>3U]|=(uint8_t)(1U<<(slot&7U));return 1U;}
uint8_t pattern_control_bank_commit(void*context){(void)context;if(g_staging_set>=SET_COUNT)return 0U;const uint8_t old=g_active_set;const uint32_t next=g_generation+1U;if(!write_commit(g_staging_set,next,g_staging_bitmap))return 0U;g_active_set=g_staging_set;g_generation=next;g_staging_set=INVALID_SET;scan_active();clear_set(old);return 1U;}
void pattern_control_bank_abort(void*context){(void)context;if(g_staging_set<SET_COUNT){clear_set(g_staging_set);g_staging_set=INVALID_SET;}}
