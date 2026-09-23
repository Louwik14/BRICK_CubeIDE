#include "Storage/groove_bank.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "Platform/groove_flash_layout.h"
#include "Platform/memory_layout.h"
#include "SD/sd_scheduler_runtime.h"
#include "Storage/groove_flash_backend.h"
#include "Storage/sd_access_gate.h"
#include "Storage/storage_shared_io.h"
#include "ff.h"

#define BGRB_MAGIC UINT32_C(0x42524742) /* BGRB */
#define BGRB_VERSION 1U
#define BGRB_RECORD_VERSION 1U
#define BGRB_CATALOG_OFFSET GROOVE_BANK_HEADER_BYTES
#define BGRB_CATALOG_BYTES \
    (GROOVE_BANK_MAX_SOURCES * GROOVE_BANK_CATALOG_ENTRY_BYTES)
#define BGRB_FLAG_OVERFLOW UINT32_C(0x00000001)
#define BGRB_FLAG_NAME_REJECTED UINT32_C(0x00000002)
#define BGRB_ENTRY_NAME 0U
#define BGRB_ENTRY_RECORD_OFFSET 64U
#define BGRB_ENTRY_RECORD_BYTES 68U
#define BGRB_ENTRY_POINT_COUNT 70U
#define BGRB_ENTRY_RUNTIME_INDEX 71U
#define BGRB_ENTRY_RECORD_CRC 72U
#define BGRB_ENTRY_STATUS 76U
#define BGRB_XML_MAX_BYTES (64U * 1024U)
#define BGRB_XML_IO_BYTES 1024U
#define BGRB_TAG_BYTES 512U
#define BGRB_SCRATCH_RECORD_OFFSET 10240U
#define BGRB_SCRATCH_XML_OFFSET 12288U
#define BGRB_SCRATCH_TAG_OFFSET 13312U
#define BGRB_HEADER_CATALOG_CRC_OFFSET 32U
#define BGRB_BASE_COUNT 6U

_Static_assert(BGRB_CATALOG_BYTES == 10160U, "BGRB catalog budget");
_Static_assert(GROOVE_BANK_DATA_OFFSET == 10432U, "BGRB data offset");
_Static_assert(GROOVE_BANK_WORST_CASE_BYTES
                   == GROOVE_BANK_DATA_OFFSET
                    + GROOVE_BANK_MAX_SOURCES * GROOVE_BANK_RECORD_MAX_BYTES,
               "BGRB worst-case budget");
_Static_assert(GROOVE_BANK_WORST_CASE_BYTES < GROOVE_FLASH_SIZE,
               "BGRB bank exceeds Groove Flash");
_Static_assert(STORAGE_SHARED_IO_BYTES >= 13824U,
               "shared Storage scratch too small for Groove import");

typedef enum
{
    GROOVE_STATE_WAIT_MEDIA = 0,
    GROOVE_STATE_SCAN_OPEN,
    GROOVE_STATE_SCAN_NEXT,
    GROOVE_STATE_SCAN_CLOSE,
    GROOVE_STATE_DECIDE,
    GROOVE_STATE_ERASE,
    GROOVE_STATE_IMPORT_OPEN,
    GROOVE_STATE_IMPORT_READ,
    GROOVE_STATE_IMPORT_CLOSE,
    GROOVE_STATE_RECORD_PROGRAM,
    GROOVE_STATE_CATALOG_PROGRAM,
    GROOVE_STATE_HEADER_PROGRAM,
    GROOVE_STATE_COMMIT,
    GROOVE_STATE_REMOVE_MARKER,
    GROOVE_STATE_READY,
    GROOVE_STATE_FAILED
} groove_state_t;

typedef struct
{
    groove_state_t state;
    DIR directory;
    FIL file;
    uint32_t media_epoch;
    uint32_t bank_data_end;
    uint32_t program_offset;
    uint32_t record_size;
    uint32_t file_bytes;
    uint32_t flags;
    uint16_t source_count;
    uint16_t import_source;
    uint8_t groove_count;
    uint8_t intrinsic_valid;
    uint8_t published;
    uint8_t marker_present;
    uint8_t directory_open;
    uint8_t file_open;
    uint8_t parser_invalid;
    uint8_t in_tag;
    uint16_t tag_length;
    uint8_t root_seen;
    uint8_t groove_seen;
    uint8_t after_clip;
    uint8_t have_loop_start;
    uint8_t have_loop_end;
    uint8_t have_signature_num;
    uint8_t have_signature_den;
    uint8_t have_base;
    uint8_t have_quantize;
    uint8_t have_timing;
    uint8_t have_random;
    uint8_t have_velocity;
    uint8_t loop_on;
    uint8_t point_count;
    uint8_t default_base;
    uint8_t default_quantize;
    uint8_t default_timing;
    uint8_t default_random;
    int8_t default_velocity;
    uint16_t signature_num;
    uint16_t signature_den;
    int64_t loop_start_q32;
    int64_t loop_end_q32;
    char path[96];
} groove_bank_runtime_t;

STORAGE_STATE_SDRAM static groove_bank_runtime_t g_groove;

static uint8_t *catalog_scratch(void) { return g_storage_shared_io; }
static uint8_t *record_scratch(void)
{ return &g_storage_shared_io[BGRB_SCRATCH_RECORD_OFFSET]; }
static uint8_t *xml_scratch(void)
{ return &g_storage_shared_io[BGRB_SCRATCH_XML_OFFSET]; }
static char *tag_scratch(void)
{ return (char *)&g_storage_shared_io[BGRB_SCRATCH_TAG_OFFSET]; }

static uint16_t read_u16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8U)); }
static uint32_t read_u32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8U)
    | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U); }
static int64_t read_i64(const uint8_t *p)
{ return (int64_t)((uint64_t)read_u32(p)
    | ((uint64_t)read_u32(p + 4U) << 32U)); }
static void write_u16(uint8_t *p,uint16_t v)
{p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8U);}
static void write_u32(uint8_t *p,uint32_t v)
{for(uint8_t i=0U;i<4U;++i)p[i]=(uint8_t)(v>>(8U*i));}
static void write_i64(uint8_t *p,int64_t v)
{write_u32(p,(uint32_t)(uint64_t)v);write_u32(p+4U,(uint32_t)((uint64_t)v>>32U));}

static uint32_t crc32_update(uint32_t crc,const uint8_t *data,uint32_t length)
{
    for(uint32_t i=0U;i<length;++i){crc^=data[i];for(uint8_t b=0U;b<8U;++b)
        crc=(crc>>1U)^(UINT32_C(0xEDB88320)&(0U-(crc&1U)));}
    return crc;
}

static int name_compare(const char *a,const char *b)
{
    while((*a!='\0')&&(*b!='\0')){uint8_t ca=(uint8_t)*a++,cb=(uint8_t)*b++;
        if(ca>='A'&&ca<='Z')ca=(uint8_t)(ca+('a'-'A'));
        if(cb>='A'&&cb<='Z')cb=(uint8_t)(cb+('a'-'A'));
        if(ca!=cb)return(ca<cb)?-1:1;}
    if (*a == *b) return strcmp(a, b);
    return (*a == '\0') ? -1 : 1;
}

static uint8_t is_agr(const char *name,size_t *stem_length)
{
    const size_t n=(name!=0)?strlen(name):0U;if(n<5U)return 0U;
    const char *e=&name[n-4U];
    if(e[0]!='.'||!((e[1]=='a')||(e[1]=='A'))
            ||!((e[2]=='g')||(e[2]=='G'))||!((e[3]=='r')||(e[3]=='R')))return 0U;
    if (stem_length != 0) *stem_length = n - 4U;
    return 1U;
}

static uint8_t *scratch_entry(uint16_t index)
{return &catalog_scratch()[(uint32_t)index*GROOVE_BANK_CATALOG_ENTRY_BYTES];}
static const uint8_t *flash_entry(uint16_t index)
{return(const uint8_t *)(GROOVE_FLASH_BASE+BGRB_CATALOG_OFFSET
    +(uint32_t)index*GROOVE_BANK_CATALOG_ENTRY_BYTES);}

static void insert_source_name(const char *name,size_t length)
{
    if(length==0U||length>=GROOVE_BANK_NAME_BYTES){g_groove.flags|=BGRB_FLAG_NAME_REJECTED;return;}
    uint16_t pos=0U;while(pos<g_groove.source_count
        &&name_compare((const char *)scratch_entry(pos),name)<0)++pos;
    if(pos<g_groove.source_count
        &&name_compare((const char *)scratch_entry(pos),name)==0)return;
    if(g_groove.source_count>=GROOVE_BANK_MAX_SOURCES){g_groove.flags|=BGRB_FLAG_OVERFLOW;
        if(pos>=GROOVE_BANK_MAX_SOURCES)return;
        memmove(scratch_entry(pos+1U),scratch_entry(pos),
            (GROOVE_BANK_MAX_SOURCES-pos-1U)*GROOVE_BANK_CATALOG_ENTRY_BYTES);}
    else{memmove(scratch_entry(pos+1U),scratch_entry(pos),
        (g_groove.source_count-pos)*GROOVE_BANK_CATALOG_ENTRY_BYTES);++g_groove.source_count;}
    uint8_t *entry=scratch_entry(pos);memset(entry,0xFF,GROOVE_BANK_CATALOG_ENTRY_BYTES);
    memcpy(entry,name,length);entry[length]='\0';entry[BGRB_ENTRY_STATUS]=GROOVE_BANK_ENTRY_INVALID;
    entry[BGRB_ENTRY_RUNTIME_INDEX]=0U;
}

static uint8_t bank_intrinsic_valid(void)
{
    const uint8_t *h=(const uint8_t *)GROOVE_FLASH_BASE;
    if(read_u32(h)!=BGRB_MAGIC||read_u16(h+4U)!=BGRB_VERSION
        ||read_u16(h+6U)!=BGRB_RECORD_VERSION
        ||read_u16(h+8U)!=GROOVE_BANK_HEADER_BYTES
        ||read_u16(h+10U)!=GROOVE_BANK_CATALOG_ENTRY_BYTES)return 0U;
    const uint16_t sources=read_u16(h+12U),grooves=read_u16(h+14U);
    const uint32_t catalog=read_u32(h+20U),data=read_u32(h+24U),end=read_u32(h+28U);
    if(sources>GROOVE_BANK_MAX_SOURCES||grooves>sources
        ||catalog!=BGRB_CATALOG_OFFSET||data!=GROOVE_BANK_DATA_OFFSET
        ||(data&31U)!=0U||end<data||end>GROOVE_FLASH_SIZE)return 0U;
    const uint8_t *cat=(const uint8_t *)(GROOVE_FLASH_BASE+catalog);
    uint32_t crc=crc32_update(UINT32_C(0xFFFFFFFF),cat,BGRB_CATALOG_BYTES)^UINT32_C(0xFFFFFFFF);
    if(crc!=read_u32(h+BGRB_HEADER_CATALOG_CRC_OFFSET))return 0U;
    uint8_t ready=0U;
    for(uint16_t i=0U;i<sources;++i){const uint8_t *e=flash_entry(i);
        if(memchr(e,'\0',GROOVE_BANK_NAME_BYTES)==0)return 0U;
        const uint8_t status=e[BGRB_ENTRY_STATUS],runtime=e[BGRB_ENTRY_RUNTIME_INDEX];
        if(status==GROOVE_BANK_ENTRY_READY){++ready;if(runtime!=ready)return 0U;
            const uint32_t off=read_u32(e+BGRB_ENTRY_RECORD_OFFSET);
            const uint16_t bytes=read_u16(e+BGRB_ENTRY_RECORD_BYTES);
            if((off&31U)!=0U||off<data||bytes<GROOVE_BANK_RECORD_HEADER_BYTES
                ||bytes>GROOVE_BANK_RECORD_MAX_BYTES||off+bytes>end)return 0U;}
        else if(status!=GROOVE_BANK_ENTRY_INVALID||runtime!=0U)return 0U;}
    return(ready==grooves)?1U:0U;
}

static uint8_t bank_precommit_valid(void)
{
    const uint8_t *const flash_catalog = (const uint8_t *)(GROOVE_FLASH_BASE + BGRB_CATALOG_OFFSET);
    if (memcmp(flash_catalog, catalog_scratch(), BGRB_CATALOG_BYTES) != 0) {
        return 0U;
    }

    for (uint16_t i = 0U; i < g_groove.source_count; ++i) {
        const uint8_t *const entry = scratch_entry(i);
        if (entry[BGRB_ENTRY_STATUS] != GROOVE_BANK_ENTRY_READY) {
            continue;
        }

        const uint32_t offset = read_u32(entry + BGRB_ENTRY_RECORD_OFFSET);
        const uint16_t bytes = read_u16(entry + BGRB_ENTRY_RECORD_BYTES);
        if ((offset < GROOVE_BANK_DATA_OFFSET) ||
            ((offset + bytes) > g_groove.bank_data_end)) {
            return 0U;
        }

        const uint32_t actual_crc = crc32_update(
            UINT32_C(0xFFFFFFFF),
            (const uint8_t *)(GROOVE_FLASH_BASE + offset),
            bytes) ^ UINT32_C(0xFFFFFFFF);
        if (actual_crc != read_u32(entry + BGRB_ENTRY_RECORD_CRC)) {
            return 0U;
        }
    }
    return 1U;
}

static void publish_bank(void)
{groove_flash_backend_publish_barrier();g_groove.published=g_groove.intrinsic_valid;g_groove.state=GROOVE_STATE_READY;}

static uint8_t storage_begin(sd_scheduler_background_kind_t kind,uint32_t bytes)
{const sd_scheduler_background_request_t r={bytes,sd_access_media_epoch(),kind};
 return(sd_scheduler_runtime_background_try_begin(&r)==SD_SCHEDULER_BACKGROUND_GO)?1U:0U;}
static void storage_end(void){sd_scheduler_runtime_background_end();}

static const char *attribute(const char *tag,const char *name)
{
    const size_t n=strlen(name);const char *p=tag;
    while ((p = strstr(p, name)) != 0)
    {
        if ((p == tag || p[-1] == ' ' || p[-1] == '\t')
            && p[n] == '=' && p[n + 1U] == '"') return p + n + 2U;
        p += n;
    }
    return 0;
}

static uint8_t parse_q(const char *p,uint8_t fraction_bits,int64_t *out)
{
    if (p == 0 || out == 0) return 0U;
    uint8_t neg = 0U;
    if (*p == '-') { neg = 1U; ++p; }
    else if (*p == '+') ++p;
    if (*p < '0' || *p > '9') return 0U;
    double value = 0.0;
    while (*p >= '0' && *p <= '9') value = value * 10.0 + (double)(*p++ - '0');
    if (*p == '.')
    {
        double place = 0.1;
        ++p;
        while (*p >= '0' && *p <= '9')
        {
            value += (double)(*p++ - '0') * place;
            place *= 0.1;
        }
    }
    int32_t exponent = 0;
    uint8_t exponent_negative = 0U;
    if (*p == 'e' || *p == 'E')
    {
        ++p;
        if (*p == '-') { exponent_negative = 1U; ++p; }
        else if (*p == '+') ++p;
        if (*p < '0' || *p > '9') return 0U;
        while (*p >= '0' && *p <= '9')
        {
            if (exponent > 1000) return 0U;
            exponent = exponent * 10 + (*p++ - '0');
        }
    }
    if (*p != '"' && *p != ' ' && *p != '\0' && *p != '/') return 0U;
    if (exponent > 64) return 0U;
    while (exponent-- > 0) value = exponent_negative ? value / 10.0 : value * 10.0;
    if (neg != 0U) value = -value;
    const double scaled = value * (double)(UINT64_C(1) << fraction_bits);
    if (scaled > (double)INT64_MAX || scaled < (double)INT64_MIN) return 0U;
    *out = (int64_t)(scaled + ((scaled >= 0.0) ? 0.5 : -0.5));
    return 1U;
}

static uint8_t parse_percent(const char *p,uint8_t *out)
{int64_t q;if(parse_q(p,0U,&q)==0U||q<0||q>100)return 0U;*out=(uint8_t)q;return 1U;}

static void parser_reset(void)
{
    g_groove.parser_invalid=0U;g_groove.in_tag=0U;g_groove.tag_length=0U;
    g_groove.root_seen=0U;g_groove.groove_seen=0U;g_groove.after_clip=0U;
    g_groove.have_loop_start=0U;g_groove.have_loop_end=0U;
    g_groove.have_signature_num=0U;g_groove.have_signature_den=0U;
    g_groove.have_base=0U;g_groove.have_quantize=0U;g_groove.have_timing=0U;
    g_groove.have_random=0U;g_groove.have_velocity=0U;g_groove.loop_on=0U;
    g_groove.point_count=0U;g_groove.file_bytes=0U;
    memset(record_scratch(),0xFF,GROOVE_BANK_RECORD_MAX_BYTES);
}

static void parser_tag(const char *tag)
{
    const char *v;int64_t q;
    if(strncmp(tag,"Ableton ",8U)==0||strcmp(tag,"Ableton")==0)g_groove.root_seen=1U;
    else if(strncmp(tag,"Groove",6U)==0)g_groove.groove_seen=1U;
    else if(strcmp(tag,"/Clip")==0)g_groove.after_clip=1U;
    else if(strncmp(tag,"LoopStart ",10U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_q(v,32U,&g_groove.loop_start_q32))g_groove.parser_invalid=1U;else g_groove.have_loop_start=1U;}
    else if(strncmp(tag,"LoopEnd ",8U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_q(v,32U,&g_groove.loop_end_q32))g_groove.parser_invalid=1U;else g_groove.have_loop_end=1U;}
    else if(strncmp(tag,"LoopOn ",7U)==0&&(v=attribute(tag,"Value"))!=0)g_groove.loop_on=(strncmp(v,"true",4U)==0)?1U:0U;
    else if(!g_groove.have_signature_num&&strncmp(tag,"Numerator ",10U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_q(v,0U,&q)||q<1||q>32)g_groove.parser_invalid=1U;else{g_groove.signature_num=(uint16_t)q;g_groove.have_signature_num=1U;}}
    else if(!g_groove.have_signature_den&&strncmp(tag,"Denominator ",12U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_q(v,0U,&q)||(q!=1&&q!=2&&q!=4&&q!=8&&q!=16&&q!=32))g_groove.parser_invalid=1U;else{g_groove.signature_den=(uint16_t)q;g_groove.have_signature_den=1U;}}
    else if(strncmp(tag,"MidiNoteEvent ",14U)==0){const char *enabled=attribute(tag,"IsEnabled");if(enabled!=0&&strncmp(enabled,"false",5U)==0)return;
        const char *time=attribute(tag,"Time"),*velocity=attribute(tag,"Velocity");int64_t tq,vq;
        if(!parse_q(time,32U,&tq)||!parse_q(velocity,16U,&vq)||vq<0||vq>((int64_t)127<<16U)||g_groove.point_count>=GROOVE_BANK_MAX_POINTS){g_groove.parser_invalid=1U;return;}
        uint8_t *point=record_scratch()+GROOVE_BANK_RECORD_HEADER_BYTES+(uint32_t)g_groove.point_count*GROOVE_BANK_POINT_BYTES;
        write_i64(point,tq);write_u32(point+8U,(uint32_t)vq);++g_groove.point_count;}
    else if(g_groove.after_clip&&strncmp(tag,"Grid ",5U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_q(v,0U,&q)||q<0||q>5)g_groove.parser_invalid=1U;else{g_groove.default_base=(uint8_t)q;g_groove.have_base=1U;}}
    else if(g_groove.after_clip&&strncmp(tag,"QuantizationAmount ",19U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_percent(v,&g_groove.default_quantize))g_groove.parser_invalid=1U;else g_groove.have_quantize=1U;}
    else if(g_groove.after_clip&&strncmp(tag,"TimingAmount ",13U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_percent(v,&g_groove.default_timing))g_groove.parser_invalid=1U;else g_groove.have_timing=1U;}
    else if(g_groove.after_clip&&strncmp(tag,"RandomAmount ",13U)==0&&(v=attribute(tag,"Value"))!=0){if(!parse_percent(v,&g_groove.default_random))g_groove.parser_invalid=1U;else g_groove.have_random=1U;}
    else if(g_groove.after_clip&&strncmp(tag,"VelocityAmount ",15U)==0&&(v=attribute(tag,"Value"))!=0){uint8_t x;if(!parse_percent(v,&x))g_groove.parser_invalid=1U;else{g_groove.default_velocity=(int8_t)x;g_groove.have_velocity=1U;}}
}

static void parser_feed(const uint8_t *data,uint32_t bytes)
{
    char *tag=tag_scratch();
    for(uint32_t i=0U;i<bytes;++i){const char c=(char)data[i];
        if(g_groove.in_tag==0U){if(c=='<'){g_groove.in_tag=1U;g_groove.tag_length=0U;}continue;}
        if(c=='>'){tag[g_groove.tag_length]='\0';parser_tag(tag);g_groove.in_tag=0U;continue;}
        if(g_groove.tag_length+1U>=BGRB_TAG_BYTES){g_groove.parser_invalid=1U;g_groove.in_tag=0U;continue;}
        tag[g_groove.tag_length++]=c;}
}

static void sort_points(void)
{
    uint8_t *base=record_scratch()+GROOVE_BANK_RECORD_HEADER_BYTES;uint8_t temp[12];
    for(uint16_t i=1U;i<g_groove.point_count;++i){memcpy(temp,base+(uint32_t)i*12U,12U);const int64_t key=read_i64(temp);uint16_t j=i;
        while(j>0U&&read_i64(base+(uint32_t)(j-1U)*12U)>key){memcpy(base+(uint32_t)j*12U,base+(uint32_t)(j-1U)*12U,12U);--j;}memcpy(base+(uint32_t)j*12U,temp,12U);}
}

static uint8_t parser_finalize(void)
{
    if(g_groove.in_tag||g_groove.parser_invalid||!g_groove.root_seen||!g_groove.groove_seen
        ||!g_groove.after_clip||!g_groove.have_loop_start||!g_groove.have_loop_end
        ||!g_groove.have_signature_num||!g_groove.have_signature_den||!g_groove.have_base
        ||!g_groove.have_quantize||!g_groove.have_timing||!g_groove.have_random||!g_groove.have_velocity)return 0U;
    const int64_t period=g_groove.loop_end_q32-g_groove.loop_start_q32;
    if(period<=0||period>((int64_t)64<<32U))return 0U;
    uint8_t *record=record_scratch();for(uint16_t i=0U;i<g_groove.point_count;++i){uint8_t *p=record+32U+(uint32_t)i*12U;write_i64(p,read_i64(p)-g_groove.loop_start_q32);}
    sort_points();write_u16(record,BGRB_RECORD_VERSION);write_u16(record+2U,GROOVE_BANK_RECORD_HEADER_BYTES);
    write_u16(record+4U,g_groove.point_count);write_u16(record+6U,g_groove.loop_on?1U:0U);
    write_i64(record+8U,period);write_u16(record+16U,g_groove.signature_num);write_u16(record+18U,g_groove.signature_den);
    record[20U]=g_groove.default_base;record[21U]=g_groove.default_quantize;record[22U]=g_groove.default_timing;
    record[23U]=g_groove.default_random;record[24U]=(uint8_t)g_groove.default_velocity;
    g_groove.record_size=(GROOVE_BANK_RECORD_HEADER_BYTES+(uint32_t)g_groove.point_count*12U+31U)&~31U;
    return 1U;
}

static uint8_t catalog_matches_flash(void)
{
    const uint8_t *h=(const uint8_t *)GROOVE_FLASH_BASE;
    if(read_u16(h+12U)!=g_groove.source_count)return 0U;
    if((read_u32(h+16U)&(BGRB_FLAG_OVERFLOW|BGRB_FLAG_NAME_REJECTED))
        !=(g_groove.flags&(BGRB_FLAG_OVERFLOW|BGRB_FLAG_NAME_REJECTED)))return 0U;
    for(uint16_t i=0U;i<g_groove.source_count;++i)
        if(strcmp((const char *)scratch_entry(i),(const char *)flash_entry(i))!=0)return 0U;
    return 1U;
}

static void mark_import_result(uint8_t valid)
{
    uint8_t *entry=scratch_entry(g_groove.import_source);
    if(valid){++g_groove.groove_count;entry[BGRB_ENTRY_STATUS]=GROOVE_BANK_ENTRY_READY;
        entry[BGRB_ENTRY_RUNTIME_INDEX]=g_groove.groove_count;
        write_u32(entry+BGRB_ENTRY_RECORD_OFFSET,g_groove.bank_data_end);
        write_u16(entry+BGRB_ENTRY_RECORD_BYTES,(uint16_t)g_groove.record_size);
        entry[BGRB_ENTRY_POINT_COUNT]=g_groove.point_count;
        const uint32_t crc=crc32_update(UINT32_C(0xFFFFFFFF),record_scratch(),g_groove.record_size)^UINT32_C(0xFFFFFFFF);
        write_u32(entry+BGRB_ENTRY_RECORD_CRC,crc);g_groove.program_offset=0U;g_groove.state=GROOVE_STATE_RECORD_PROGRAM;}
    else{entry[BGRB_ENTRY_STATUS]=GROOVE_BANK_ENTRY_INVALID;entry[BGRB_ENTRY_RUNTIME_INDEX]=0U;
        write_u32(entry+BGRB_ENTRY_RECORD_OFFSET,0U);write_u16(entry+BGRB_ENTRY_RECORD_BYTES,0U);entry[BGRB_ENTRY_POINT_COUNT]=0U;write_u32(entry+BGRB_ENTRY_RECORD_CRC,0U);
        ++g_groove.import_source;g_groove.state=GROOVE_STATE_IMPORT_OPEN;}
}

void groove_bank_init(void)
{
    memset(&g_groove,0,sizeof(g_groove));g_groove.intrinsic_valid=bank_intrinsic_valid();
    g_groove.state=GROOVE_STATE_WAIT_MEDIA;
}

void groove_bank_service(void)
{
    FRESULT fr;UINT amount=0U;
    switch(g_groove.state)
    {
        case GROOVE_STATE_WAIT_MEDIA:
            if(sd_access_storage_status()==SD_STORAGE_STATUS_NO_MEDIA||sd_access_storage_status()==SD_STORAGE_STATUS_FAULT){publish_bank();return;}
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            if (sd_access_fs_mount_if_needed() != 0U)
                g_groove.state = GROOVE_STATE_SCAN_OPEN;
            storage_end();
            return;
        case GROOVE_STATE_SCAN_OPEN:
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            memset(catalog_scratch(),0xFF,BGRB_CATALOG_BYTES);g_groove.source_count=0U;g_groove.flags=0U;g_groove.marker_present=0U;
            fr=f_opendir(&g_groove.directory,"0:/Grooves");g_groove.directory_open=(fr==FR_OK);g_groove.state=(fr==FR_OK)?GROOVE_STATE_SCAN_NEXT:GROOVE_STATE_DECIDE;storage_end();return;
        case GROOVE_STATE_SCAN_NEXT:{if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;FILINFO info;memset(&info,0,sizeof(info));fr=f_readdir(&g_groove.directory,&info);
            if(fr!=FR_OK||info.fname[0]=='\0')g_groove.state=GROOVE_STATE_SCAN_CLOSE;
            else if ((info.fattrib & AM_DIR) == 0U)
            {
                if (strcmp(info.fname, "REBUILD.BRK") == 0)
                    g_groove.marker_present = 1U;
                else
                {
                    size_t stem;
                    if (is_agr(info.fname, &stem) != 0U)
                        insert_source_name(info.fname, stem);
                }
            }
            storage_end();
            return;}
        case GROOVE_STATE_SCAN_CLOSE:
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            (void)f_closedir(&g_groove.directory);
            g_groove.directory_open=0U;
            g_groove.state=GROOVE_STATE_DECIDE;
            storage_end();
            return;
        case GROOVE_STATE_DECIDE:
            if(g_groove.marker_present==0U&&g_groove.intrinsic_valid&&catalog_matches_flash()){publish_bank();return;}
            g_groove.published=0U;g_groove.state=GROOVE_STATE_ERASE;return;
        case GROOVE_STATE_ERASE:
            if(!groove_flash_backend_erase_all()){g_groove.state=GROOVE_STATE_FAILED;return;}
            g_groove.bank_data_end=GROOVE_BANK_DATA_OFFSET;g_groove.import_source=0U;g_groove.groove_count=0U;g_groove.state=GROOVE_STATE_IMPORT_OPEN;return;
        case GROOVE_STATE_IMPORT_OPEN:
            if(g_groove.import_source>=g_groove.source_count){g_groove.program_offset=0U;g_groove.state=GROOVE_STATE_CATALOG_PROGRAM;return;}
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            {const char *name=(const char *)scratch_entry(g_groove.import_source);const int n=snprintf(g_groove.path,sizeof(g_groove.path),"0:/Grooves/%s.agr",name);
             parser_reset();fr=(n>0&&(size_t)n<sizeof(g_groove.path))?f_open(&g_groove.file,g_groove.path,FA_READ):FR_INVALID_NAME;
             if(fr==FR_OK&&f_size(&g_groove.file)<=BGRB_XML_MAX_BYTES){g_groove.file_open=1U;g_groove.state=GROOVE_STATE_IMPORT_READ;}
             else{if(fr==FR_OK)(void)f_close(&g_groove.file);mark_import_result(0U);}storage_end();return;}
        case GROOVE_STATE_IMPORT_READ:
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_DATA,BGRB_XML_IO_BYTES))return;
            fr=f_read(&g_groove.file,xml_scratch(),BGRB_XML_IO_BYTES,&amount);if(fr!=FR_OK){g_groove.parser_invalid=1U;g_groove.state=GROOVE_STATE_IMPORT_CLOSE;}
            else if (amount == 0U) g_groove.state = GROOVE_STATE_IMPORT_CLOSE;
            else
            {
                g_groove.file_bytes += amount;
                parser_feed(xml_scratch(), amount);
            }
            storage_end();
            return;
        case GROOVE_STATE_IMPORT_CLOSE:
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            if(g_groove.file_open)(void)f_close(&g_groove.file);
            g_groove.file_open=0U;
            mark_import_result(parser_finalize());
            storage_end();
            return;
        case GROOVE_STATE_RECORD_PROGRAM:
            if(g_groove.bank_data_end+g_groove.record_size>GROOVE_FLASH_SIZE){g_groove.state=GROOVE_STATE_FAILED;return;}
            if(!groove_flash_backend_program(GROOVE_FLASH_BASE+g_groove.bank_data_end+g_groove.program_offset,record_scratch()+g_groove.program_offset)){g_groove.state=GROOVE_STATE_FAILED;return;}
            g_groove.program_offset+=32U;if(g_groove.program_offset>=g_groove.record_size){g_groove.bank_data_end+=g_groove.record_size;++g_groove.import_source;g_groove.state=GROOVE_STATE_IMPORT_OPEN;}return;
        case GROOVE_STATE_CATALOG_PROGRAM:
            if(g_groove.program_offset<BGRB_CATALOG_BYTES){uint8_t word[32];memset(word,0xFF,sizeof(word));uint32_t remain=BGRB_CATALOG_BYTES-g_groove.program_offset;if(remain>32U)remain=32U;memcpy(word,catalog_scratch()+g_groove.program_offset,remain);
                if(!groove_flash_backend_program(GROOVE_FLASH_BASE+BGRB_CATALOG_OFFSET+g_groove.program_offset,word)){g_groove.state=GROOVE_STATE_FAILED;return;}g_groove.program_offset+=32U;return;}
            g_groove.state=GROOVE_STATE_HEADER_PROGRAM;return;
        case GROOVE_STATE_HEADER_PROGRAM:{uint8_t word[32];memset(word,0xFF,sizeof(word));const uint32_t crc=crc32_update(UINT32_C(0xFFFFFFFF),(const uint8_t *)(GROOVE_FLASH_BASE+BGRB_CATALOG_OFFSET),BGRB_CATALOG_BYTES)^UINT32_C(0xFFFFFFFF);write_u32(word,crc);
            if(!groove_flash_backend_program(GROOVE_FLASH_BASE+BGRB_HEADER_CATALOG_CRC_OFFSET,word)){g_groove.state=GROOVE_STATE_FAILED;return;}g_groove.state=GROOVE_STATE_COMMIT;return;}
        case GROOVE_STATE_COMMIT:{uint8_t word[32];if(!bank_precommit_valid()){g_groove.state=GROOVE_STATE_FAILED;return;}memset(word,0xFF,sizeof(word));write_u32(word,BGRB_MAGIC);write_u16(word+4U,BGRB_VERSION);write_u16(word+6U,BGRB_RECORD_VERSION);write_u16(word+8U,GROOVE_BANK_HEADER_BYTES);write_u16(word+10U,GROOVE_BANK_CATALOG_ENTRY_BYTES);write_u16(word+12U,g_groove.source_count);write_u16(word+14U,g_groove.groove_count);write_u32(word+16U,g_groove.flags);write_u32(word+20U,BGRB_CATALOG_OFFSET);write_u32(word+24U,GROOVE_BANK_DATA_OFFSET);write_u32(word+28U,g_groove.bank_data_end);
            if(!groove_flash_backend_program(GROOVE_FLASH_BASE,word)){g_groove.state=GROOVE_STATE_FAILED;return;}g_groove.intrinsic_valid=bank_intrinsic_valid();if(!g_groove.intrinsic_valid){g_groove.state=GROOVE_STATE_FAILED;return;}g_groove.state=g_groove.marker_present?GROOVE_STATE_REMOVE_MARKER:GROOVE_STATE_READY;if(!g_groove.marker_present)publish_bank();return;}
        case GROOVE_STATE_REMOVE_MARKER:
            if(!storage_begin(SD_SCHEDULER_BACKGROUND_METADATA,0U))return;
            fr=f_unlink("0:/Grooves/REBUILD.BRK");
            storage_end();
            if(fr==FR_OK||fr==FR_NO_FILE)publish_bank();
            else g_groove.state=GROOVE_STATE_FAILED;
            return;
        case GROOVE_STATE_FAILED:g_groove.published=0U;return;
        case GROOVE_STATE_READY:default:return;
    }
}

uint8_t groove_bank_boot_complete(void)
{return(g_groove.state==GROOVE_STATE_READY||g_groove.state==GROOVE_STATE_FAILED)?1U:0U;}
uint8_t groove_bank_ready(void){return g_groove.published;}
uint8_t groove_bank_overflow(void)
{return(g_groove.published&&((((const uint8_t *)GROOVE_FLASH_BASE)[16U]&1U)!=0U))?1U:0U;}
uint8_t groove_bank_count(void)
{return g_groove.published?(uint8_t)read_u16((const uint8_t *)GROOVE_FLASH_BASE+14U):0U;}

uint8_t groove_bank_get(uint8_t runtime_index,groove_bank_entry_view_t *out)
{
    if(!g_groove.published||out==0||runtime_index==0U)return 0U;
    const uint16_t sources=read_u16((const uint8_t *)GROOVE_FLASH_BASE+12U);
    for(uint16_t i=0U;i<sources;++i){const uint8_t *e=flash_entry(i);if(e[BGRB_ENTRY_RUNTIME_INDEX]!=runtime_index)continue;
        out->name=(const char *)e;out->record=(const uint8_t *)(GROOVE_FLASH_BASE+read_u32(e+BGRB_ENTRY_RECORD_OFFSET));out->record_bytes=read_u16(e+BGRB_ENTRY_RECORD_BYTES);out->point_count=e[BGRB_ENTRY_POINT_COUNT];out->runtime_index=runtime_index;out->record_crc32=read_u32(e+BGRB_ENTRY_RECORD_CRC);out->status=(groove_bank_entry_status_t)e[BGRB_ENTRY_STATUS];return 1U;}return 0U;
}

uint8_t groove_bank_find_name(const char *name,uint8_t *out_runtime_index)
{
    if(!g_groove.published||name==0||out_runtime_index==0)return 0U;
    const uint16_t sources=read_u16((const uint8_t *)GROOVE_FLASH_BASE+12U);
    for(uint16_t i=0U;i<sources;++i){const uint8_t *e=flash_entry(i);if(strcmp(name,(const char *)e)==0&&e[BGRB_ENTRY_STATUS]==GROOVE_BANK_ENTRY_READY){*out_runtime_index=e[BGRB_ENTRY_RUNTIME_INDEX];return 1U;}}return 0U;
}

uint8_t groove_bank_validate_record(uint8_t runtime_index)
{
    groove_bank_entry_view_t e;if(!groove_bank_get(runtime_index,&e)||e.status!=GROOVE_BANK_ENTRY_READY)return 0U;
    const uint16_t points=read_u16(e.record+4U);
    const int64_t period=read_i64(e.record+8U);
    const uint32_t expected_bytes=(GROOVE_BANK_RECORD_HEADER_BYTES
        +(uint32_t)points*GROOVE_BANK_POINT_BYTES+31U)&~31U;
    if(read_u16(e.record)!=BGRB_RECORD_VERSION
            ||read_u16(e.record+2U)!=GROOVE_BANK_RECORD_HEADER_BYTES
            ||points!=e.point_count||points>GROOVE_BANK_MAX_POINTS
            ||expected_bytes!=e.record_bytes||period<=0
            ||period>((int64_t)64<<32U)
            ||read_u16(e.record+16U)<1U||read_u16(e.record+16U)>32U
            ||(read_u16(e.record+18U)!=1U&&read_u16(e.record+18U)!=2U
                &&read_u16(e.record+18U)!=4U&&read_u16(e.record+18U)!=8U
                &&read_u16(e.record+18U)!=16U&&read_u16(e.record+18U)!=32U)
            ||e.record[20U]>=BGRB_BASE_COUNT||e.record[21U]>100U
            ||e.record[22U]>100U||e.record[23U]>100U
            ||(int8_t)e.record[24U] < -100||(int8_t)e.record[24U] > 100)return 0U;
    const uint32_t crc=crc32_update(UINT32_C(0xFFFFFFFF),e.record,e.record_bytes)^UINT32_C(0xFFFFFFFF);return(crc==e.record_crc32)?1U:0U;
}

uint8_t groove_bank_resolve(uint8_t runtime_index,
                            groove_bank_record_view_t *out_record)
{
    groove_bank_entry_view_t entry;
    if ((out_record == NULL) || !groove_bank_get(runtime_index, &entry)
            || !groove_bank_validate_record(runtime_index)) return 0U;
    const uint8_t *const record = entry.record;
    out_record->points = record + GROOVE_BANK_RECORD_HEADER_BYTES;
    out_record->period_q32 = (uint64_t)read_i64(record + 8U);
    out_record->signature_numerator = read_u16(record + 16U);
    out_record->signature_denominator = read_u16(record + 18U);
    out_record->point_count = entry.point_count;
    out_record->base = record[20U];
    out_record->quantize = record[21U];
    out_record->timing = record[22U];
    out_record->random = record[23U];
    out_record->velocity = (int8_t)record[24U];
    out_record->loop_on = (read_u16(record + 6U) & 1U) ? 1U : 0U;
    return 1U;
}
