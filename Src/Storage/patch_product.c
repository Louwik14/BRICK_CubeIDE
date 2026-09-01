#include "Storage/patch_product.h"
#include "Storage/persistent_patch_control.h"
#include "Storage/persistent_fatfs_io.h"
#include "Storage/persistent_key_catalog.h"
#include "Storage/sd_access_gate.h"
#include "Storage/project_control.h"
#include "Storage/project_load_quiesce.h"
#include "Sampler/sampler_ram_pool.h"
#include "Platform/memory_layout.h"
#include "Track/track_catalog.h"
#include "Track/track_state.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
static uint8_t g_present[PATCH_PRODUCT_SLOT_COUNT],g_invalid[PATCH_PRODUCT_SLOT_COUNT];
STORAGE_STATE_SDRAM static patch_product_metadata_t g_meta[PATCH_PRODUCT_SLOT_COUNT];
static uint16_t g_current=PATCH_PRODUCT_INVALID_SLOT;
STORAGE_STATE_SDRAM static persist_codec_patch_staging_t g_stage;
typedef struct { uint8_t active; uint16_t slot,target_mask; } patch_apply_runtime_t;
STORAGE_STATE_SDRAM static patch_apply_runtime_t g_patch_apply;
#define PATCH_PRODUCT_SECTION_BODY 0x3001U
static uint32_t crc32(uint32_t crc,const uint8_t*d,uint32_t n){for(uint32_t i=0;i<n;++i){crc^=d[i];for(uint8_t b=0;b<8U;++b)crc=(crc>>1U)^(0xEDB88320UL&((uint32_t)-(int32_t)(crc&1U)));}return crc;}
static uint8_t path(char*out,uint32_t size,uint16_t slot){int n=snprintf(out,size,"0:/BRICK/PATCH/P%04u.B6C",slot);return(n>0&&(uint32_t)n<size)?1U:0U;}
static uint8_t acquire(void){if(!sd_access_gate_try_acquire(SD_ACCESS_CLIENT_PATCH))return 0U;if(!sd_access_fs_mount_if_needed()){sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);return 0U;}return 1U;}
static void meta_from_patch(uint16_t slot,const persist_control_patch_t*p){patch_product_metadata_t*m=&g_meta[slot];memset(m,0,sizeof(*m));uint16_t n=p->name_length;if(n>32U)n=32U;memcpy(m->name,p->name,n);track_family_t f;track_type_t t;if(persist_key_family_from_disk(p->family,&f))m->family=(uint8_t)f;if(persist_key_type_from_disk(p->type,&t))m->type=(uint8_t)t;m->summary_family=m->family;m->summary_type=m->type;}
static uint16_t le16(const uint8_t*p){return(uint16_t)((uint16_t)p[0]|((uint16_t)p[1]<<8U));}
static uint32_t le32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8U)|((uint32_t)p[2]<<16U)|((uint32_t)p[3]<<24U);}
static uint8_t scan_meta(uint16_t slot)
{
    char x[48];FIL f;UINT n=0U;uint8_t h[PERSIST_CODEC_HEADER_BYTES];
    if(!path(x,sizeof(x),slot)||f_open(&f,x,FA_READ)!=FR_OK)return 0U;
    uint8_t ok=(f_read(&f,h,sizeof(h),&n)==FR_OK&&n==sizeof(h));
    const uint32_t total=ok?le32(&h[12]):0U;
    const uint32_t header_crc=ok?le32(&h[20]):0U;
    ok=(ok&&h[0]=='B'&&h[1]=='6'&&h[2]=='P'&&h[3]=='C'
        &&h[4]==PERSIST_CODEC_VERSION&&h[5]==0U
        &&h[6]==PERSIST_CODEC_DOCUMENT_PATCH&&h[7]==0U
        &&le16(&h[8])==1U&&h[10]==0U&&h[11]==0U
        &&total==(uint32_t)f_size(&f)
        &&total<=PERSIST_CODEC_MAX_DOCUMENT_BYTES
        &&total>=PERSIST_CODEC_HEADER_BYTES+PERSIST_CODEC_SECTION_HEADER_BYTES+10U
        &&header_crc==~crc32(0xFFFFFFFFUL,h,20U));
    uint8_t sh[PERSIST_CODEC_SECTION_HEADER_BYTES];
    if(ok)ok=(f_read(&f,sh,sizeof(sh),&n)==FR_OK&&n==sizeof(sh));
    const uint32_t section_length=ok?le32(&sh[4]):0U;
    ok=(ok&&le16(sh)==PATCH_PRODUCT_SECTION_BODY&&le16(&sh[2])==1U
        &&section_length==total-PERSIST_CODEC_HEADER_BYTES-PERSIST_CODEC_SECTION_HEADER_BYTES
        &&section_length>=10U);
    uint8_t nl[2];if(ok)ok=(f_read(&f,nl,sizeof(nl),&n)==FR_OK&&n==sizeof(nl));
    const uint16_t name_len=ok?le16(nl):0U;
    ok=(ok&&name_len<=PERSIST_CONTROL_PATCH_NAME_BYTES
        &&section_length>=(uint32_t)name_len+10U);
    if(ok){uint8_t buf[PERSIST_CONTROL_PATCH_NAME_BYTES+8U];ok=(f_read(&f,buf,name_len+8U,&n)==FR_OK&&n==name_len+8U);if(ok){persist_control_patch_t p;memset(&p,0,sizeof(p));p.name_length=name_len;memcpy(p.name,buf,name_len);p.family=le32(&buf[name_len]);p.type=le32(&buf[name_len+4U]);track_family_t family;track_type_t type;ok=(persist_key_family_from_disk(p.family,&family)&&persist_key_type_from_disk(p.type,&type));if(ok)meta_from_patch(slot,&p);}}
    (void)f_close(&f);g_present[slot]=1U;g_invalid[slot]=ok?0U:1U;return ok;
}
static uint8_t load(uint16_t slot,persist_control_patch_t*out){if(slot>=PATCH_PRODUCT_SLOT_COUNT||out==NULL||!g_present[slot]||!acquire())return 0U;char x[48];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot)&&persistent_fatfs_open_read(&f,x);if(ok){persist_codec_source_t s=persistent_fatfs_source(&f);ok=(persist_codec_decode_patch(&s,&g_stage)==PERSIST_CODEC_OK);persistent_fatfs_close(&f);}sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);if(ok){*out=g_stage.patch;meta_from_patch(slot,out);g_invalid[slot]=0U;}else g_invalid[slot]=1U;return ok;}
static uint8_t store(uint16_t slot,const persist_control_patch_t*p){if(slot>=PATCH_PRODUCT_SLOT_COUNT||p==NULL||!acquire())return 0U;char x[48],tmp[52];persistent_fatfs_file_t f;uint8_t ok=path(x,sizeof(x),slot);if(ok){snprintf(tmp,sizeof(tmp),"%s.TMP",x);ok=persistent_fatfs_open_write(&f,tmp);if(ok){persist_codec_sink_t s=persistent_fatfs_sink(&f);ok=(persist_codec_encode_patch(p,&s,NULL)==PERSIST_CODEC_OK)&&(f_sync(&f.file)==FR_OK);persistent_fatfs_close(&f);}if(ok){(void)f_unlink(x);ok=(f_rename(tmp,x)==FR_OK);}else(void)f_unlink(tmp);}sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);if(ok){g_present[slot]=1U;g_invalid[slot]=0U;meta_from_patch(slot,p);}return ok;}
void patch_product_init(void){memset(g_present,0,sizeof(g_present));memset(g_invalid,0,sizeof(g_invalid));memset(g_meta,0,sizeof(g_meta));memset(&g_patch_apply,0,sizeof(g_patch_apply));g_current=PATCH_PRODUCT_INVALID_SLOT;if(!acquire())return;(void)f_mkdir("0:/BRICK");(void)f_mkdir("0:/BRICK/PATCH");for(uint16_t s=0;s<PATCH_PRODUCT_SLOT_COUNT;++s){char x[48];FILINFO i;if(path(x,sizeof(x),s)&&f_stat(x,&i)==FR_OK)(void)scan_meta(s);}sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);}
uint16_t patch_product_first_empty(void){for(uint16_t s=0;s<PATCH_PRODUCT_SLOT_COUNT;++s)if(!g_present[s])return s;return PATCH_PRODUCT_INVALID_SLOT;}
patch_product_result_t patch_product_save(uint8_t e,uint16_t*out){if(g_patch_apply.active!=0U)return PATCH_PRODUCT_IO_BUSY;uint16_t s=(g_current<PATCH_PRODUCT_SLOT_COUNT&&!g_invalid[g_current])?g_current:patch_product_first_empty();if(s==PATCH_PRODUCT_INVALID_SLOT)return PATCH_PRODUCT_NO_SLOT;persist_control_patch_t p;char generated[33];const char*name=(g_present[s]&&g_meta[s].name[0])?g_meta[s].name:generated;if(name==generated)(void)snprintf(generated,sizeof(generated),"T%02u %s",(unsigned)(e+1U),track_catalog_family_short_name(track_state_get_family(e)));if(persistent_patch_control_capture(e,name,&p)!=PERSIST_CODEC_OK)return PATCH_PRODUCT_INVALID;if(!store(s,&p))return PATCH_PRODUCT_IO_ERROR;g_current=s;if(out)*out=s;return PATCH_PRODUCT_OK;}
patch_product_result_t patch_product_apply(uint16_t s,uint8_t e)
{
    if (project_replacement_is_active() != 0U)
        return PATCH_PRODUCT_IO_BUSY;
    if(g_patch_apply.active!=0U)
    {
        if(s==g_patch_apply.slot&&e<BRICK_ENTITY_CAPACITY)
        {
            g_patch_apply.target_mask|=(uint16_t)(1UL<<e);
            return PATCH_PRODUCT_PENDING;
        }
        return PATCH_PRODUCT_IO_BUSY;
    }
    if(s>=PATCH_PRODUCT_SLOT_COUNT)return PATCH_PRODUCT_INVALID;
    if(!g_present[s])return PATCH_PRODUCT_EMPTY;
    persist_control_patch_t p;
    if(!load(s,&p))return PATCH_PRODUCT_IO_ERROR;
    if(persistent_patch_control_validate(&p,e)!=PERSIST_CODEC_OK)return PATCH_PRODUCT_INVALID;
    for(uint8_t asset_index=0U;asset_index<p.asset_count;++asset_index)
    {
        const persist_control_asset_ref_t*const selected=&p.assets[asset_index];
        if(selected->kind!=PERSIST_ASSET_SAMPLE_RAM)continue;
        char asset_path[PERSIST_CONTROL_ASSET_PATH_BYTES+1U];uint16_t logical=0U;
        memcpy(asset_path,selected->canonical_path,selected->path_length);asset_path[selected->path_length]='\0';
        const project_control_asset_result_t asset=
            project_control_ensure_asset(selected->kind,asset_path,&logical);
        if(asset==PROJECT_CONTROL_ASSET_FAILED
            || asset==PROJECT_CONTROL_ASSET_FAILED_INTERNAL)
            return PATCH_PRODUCT_INVALID;
        if(asset==PROJECT_CONTROL_ASSET_PENDING)
        {
            g_patch_apply.active=1U;g_patch_apply.target_mask=(uint16_t)(1UL<<e);g_patch_apply.slot=s;
            return PATCH_PRODUCT_PENDING;
        }
    }
    if(persistent_patch_control_apply(&p,e)!=PERSIST_CODEC_OK)return PATCH_PRODUCT_INVALID;
    g_current=s;return PATCH_PRODUCT_OK;
}

void patch_product_apply_service(void)
{
    if(g_patch_apply.active==0U)return;
    sampler_ram_result_t result=SAMPLER_RAM_RESULT_INVALID_ARG;
    uint16_t backend=SAMPLER_RAM_POOL_INVALID_SLOT;
    uint16_t runtime=SAMPLE_GLOBAL_POOL_INVALID_INDEX;
    const char *path_value=NULL;
    if(sampler_ram_pool_load_async_take_result(&result,&backend,&runtime,&path_value)==0U)return;
    project_control_complete_ram_runtime(path_value,backend,runtime,
                                         (result==SAMPLER_RAM_RESULT_OK)?1U:0U);
    uint8_t applied=0U;
    if(result==SAMPLER_RAM_RESULT_OK)
        for(uint8_t target=0U;target<BRICK_ENTITY_CAPACITY;++target)
            if((g_patch_apply.target_mask&(uint16_t)(1UL<<target))!=0U
                &&persistent_patch_control_apply(&g_stage.patch,target)==PERSIST_CODEC_OK)
                applied=1U;
    if(applied!=0U)g_current=g_patch_apply.slot;
    memset(&g_patch_apply,0,sizeof(g_patch_apply));
}
patch_product_result_t patch_product_rename(uint16_t s,const char*n){if(g_patch_apply.active!=0U)return PATCH_PRODUCT_IO_BUSY;persist_control_patch_t p;if(n==NULL||!load(s,&p))return PATCH_PRODUCT_IO_ERROR;size_t z=strlen(n);if(z>PERSIST_CONTROL_PATCH_NAME_BYTES)return PATCH_PRODUCT_INVALID;memset(p.name,0,sizeof(p.name));memcpy(p.name,n,z);p.name_length=(uint16_t)z;return store(s,&p)?PATCH_PRODUCT_OK:PATCH_PRODUCT_IO_ERROR;}
patch_product_result_t patch_product_delete(uint16_t s,uint16_t*out){if(g_patch_apply.active!=0U)return PATCH_PRODUCT_IO_BUSY;if(s>=PATCH_PRODUCT_SLOT_COUNT)return PATCH_PRODUCT_INVALID;if(!g_present[s])return PATCH_PRODUCT_EMPTY;if(!acquire())return PATCH_PRODUCT_IO_BUSY;char x[48];FRESULT r=FR_INVALID_NAME;if(path(x,sizeof(x),s))r=f_unlink(x);sd_access_gate_release(SD_ACCESS_CLIENT_PATCH);if(r!=FR_OK&&r!=FR_NO_FILE)return PATCH_PRODUCT_IO_ERROR;g_present[s]=g_invalid[s]=0U;memset(&g_meta[s],0,sizeof(g_meta[s]));uint16_t next=PATCH_PRODUCT_INVALID_SLOT;for(uint16_t i=1;i<=PATCH_PRODUCT_SLOT_COUNT;++i){uint16_t c=(uint16_t)((s+i)%PATCH_PRODUCT_SLOT_COUNT);if(g_present[c]&&!g_invalid[c]){next=c;break;}}g_current=next;if(out)*out=next;return PATCH_PRODUCT_OK;}
patch_product_slot_state_t patch_product_slot_state(uint16_t s){return(s>=PATCH_PRODUCT_SLOT_COUNT||g_invalid[s])?PATCH_PRODUCT_SLOT_INVALID:(g_present[s]?PATCH_PRODUCT_SLOT_VALID:PATCH_PRODUCT_SLOT_EMPTY);}
uint8_t patch_product_metadata(uint16_t s,patch_product_metadata_t*out){if(out==NULL||patch_product_slot_state(s)!=PATCH_PRODUCT_SLOT_VALID)return 0U;*out=g_meta[s];return 1U;}
void patch_product_set_current(uint16_t s){if(s<PATCH_PRODUCT_SLOT_COUNT)g_current=s;}
uint16_t patch_product_get_current(void){return g_current;}
const char*patch_product_result_label(patch_product_result_t r){switch(r){case PATCH_PRODUCT_OK:return "OK";case PATCH_PRODUCT_PENDING:return "LOADING";case PATCH_PRODUCT_EMPTY:return "EMPTY";case PATCH_PRODUCT_IO_BUSY:return "SD BUSY";case PATCH_PRODUCT_NO_SLOT:return "BANK FULL";case PATCH_PRODUCT_IO_ERROR:return "SD ERROR";default:return "INVALID";}}
