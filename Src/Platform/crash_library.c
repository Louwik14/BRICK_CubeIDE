#include "Platform/crash_library.h"

#include <stddef.h>
#include <string.h>

#include "IPC/control_audio_fifo_layout.h"
#include "stm32h7xx_hal.h"
#include "tim.h"

#define CRASH_SECTOR_MAGIC  0x43534543UL /* CSEC */
#define CRASH_CAPSULE_MAGIC 0x43524153UL /* CRAS */
#define CRASH_COMMIT_MAGIC  0x434F4D54UL /* COMT */
#define CRASH_VIEW_MAGIC    0x43525657UL /* CRVW */
#define CRASH_FORMAT_VERSION 1U
#define CRASH_EXTRA_WORDS   128U
#define CRASH_FIFO_SNAPSHOT 8U

typedef struct
{
    uint32_t magic, version, generation, header_crc;
    uint32_t reserved[4];
} crash_sector_header_t;

typedef struct
{
    uint32_t magic, version, capsule_size, sequence;
    uint32_t crc32, payload_size, flags, reserved;
} crash_capsule_header_t;

typedef struct
{
    char firmware_id[32];
    char message[96];
    char file[96];
    char function[64];
    uint32_t line, code, entity, context, requested, capacity;
    uint32_t pc, lr, sp, xpsr, msp, psp, control, ipsr;
    uint32_t primask, basepri, faultmask, exc_return;
    uint32_t cfsr, hfsr, dfsr, afsr, mmfar, bfar;
    uint32_t reset_reason, hal_tick, tim5_count, fifo_head;
    uint32_t fifo_tail, fifo_count, fifo_overflow, fifo_invariant_failures;
    control_audio_command_t fifo[CRASH_FIFO_SNAPSHOT];
    uint32_t extra_count;
    uint32_t extra[CRASH_EXTRA_WORDS];
} crash_payload_t;

#define CRASH_BODY_RESERVED (CRASH_SLOT_SIZE - 32U - sizeof(crash_capsule_header_t) - sizeof(crash_payload_t))
typedef struct
{
    crash_capsule_header_t header;
    crash_payload_t payload;
    uint8_t reserved[CRASH_BODY_RESERVED];
    uint32_t commit_magic, commit_sequence, commit_crc, commit_inverse;
    uint32_t commit_reserved[4];
} crash_capsule_t;

typedef struct
{
    uint32_t sequence, address, crc32, status;
} crash_gdb_descriptor_t;

typedef struct
{
    uint32_t magic, version, valid_count, newest_sequence;
    uint32_t oldest_sequence, newest_address, next_address, active_generation;
    crash_gdb_descriptor_t descriptors[CRASH_SLOT_COUNT];
    uint8_t reserved[156];
    volatile uint32_t clear_command;
} crash_gdb_view_t;

_Static_assert(sizeof(crash_sector_header_t) == 32U, "flashword header");
_Static_assert(sizeof(crash_capsule_t) == CRASH_SLOT_SIZE, "capsule ABI");
_Static_assert(offsetof(crash_capsule_t, commit_magic) == CRASH_SLOT_SIZE - 32U, "commit flashword");
_Static_assert(sizeof(crash_gdb_view_t) == 512U, "fixed GDB ABI");

__attribute__((section(".backup_sram.crash_gdb"), used))
static crash_gdb_view_t g_crash_gdb_view;
static __attribute__((aligned(32))) crash_capsule_t g_crash_capsule;
static volatile uint32_t g_writer_active;

__attribute__((weak)) uint32_t crash_library_capture_extra(uint32_t *words, uint32_t capacity)
{
    (void)words; (void)capacity; return 0U;
}

static uint32_t crc32_bytes(const void *data, uint32_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    while (size-- != 0U) {
        crc ^= *p++;
        for (uint32_t bit = 0U; bit < 8U; ++bit)
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320UL);
    }
    return ~crc;
}

static uint32_t capsule_crc(const crash_capsule_t *capsule)
{
    const uint8_t *p = (const uint8_t *)capsule;
    uint32_t crc = 0xFFFFFFFFUL;
    for (uint32_t i = 0U; i < CRASH_SLOT_SIZE - 32U; ++i) {
        uint8_t value = ((i >= offsetof(crash_capsule_t, header.crc32))
            && (i < offsetof(crash_capsule_t, header.crc32) + sizeof(uint32_t))) ? 0U : p[i];
        crc ^= value;
        for (uint32_t bit=0U;bit<8U;++bit)
            crc=(crc>>1)^((0U-(crc&1U))&0xEDB88320UL);
    }
    return ~crc;
}

static uint8_t sector_valid(uint32_t base)
{
    const crash_sector_header_t *h = (const crash_sector_header_t *)base;
    crash_sector_header_t copy;
    if ((h->magic != CRASH_SECTOR_MAGIC) || (h->version != CRASH_FORMAT_VERSION)) return 0U;
    memcpy(&copy, h, sizeof(copy)); copy.header_crc = 0U;
    return h->header_crc == crc32_bytes(&copy, sizeof(copy));
}

static uint8_t capsule_valid(uint32_t address)
{
    const crash_capsule_t *c = (const crash_capsule_t *)address;
    return (c->header.magic == CRASH_CAPSULE_MAGIC)
        && (c->header.version == CRASH_FORMAT_VERSION)
        && (c->header.capsule_size == CRASH_SLOT_SIZE)
        && (c->commit_magic == CRASH_COMMIT_MAGIC)
        && (c->commit_sequence == c->header.sequence)
        && (c->commit_crc == c->header.crc32)
        && (c->commit_inverse == ~c->header.crc32)
        && (c->header.crc32 == capsule_crc(c));
}

static uint8_t flashword_program(uint32_t address, const void *source)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address,
        (uint32_t)(uintptr_t)source) == HAL_OK;
}

static uint8_t erase_sector(uint32_t sector)
{
    FLASH_EraseInitTypeDef e = {0}; uint32_t error = 0U;
    e.TypeErase = FLASH_TYPEERASE_SECTORS; e.Banks = FLASH_BANK_2;
    e.Sector = sector; e.NbSectors = 1U; e.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    return HAL_FLASHEx_Erase(&e, &error) == HAL_OK;
}

static uint8_t write_sector_header(uint32_t base, uint32_t generation)
{
    __attribute__((aligned(32))) crash_sector_header_t h;
    memset(&h, 0xFF, sizeof(h)); h.magic = CRASH_SECTOR_MAGIC;
    h.version = CRASH_FORMAT_VERSION; h.generation = generation; h.header_crc = 0U;
    h.header_crc = crc32_bytes(&h, sizeof(h));
    return flashword_program(base, &h);
}

static uint8_t write_capsule(uint32_t address, const crash_capsule_t *c)
{
    const uint8_t *p = (const uint8_t *)c;
    for (uint32_t off = 0U; off < CRASH_SLOT_SIZE - 32U; off += 32U)
        if (!flashword_program(address + off, p + off)) return 0U;
    return flashword_program(address + CRASH_SLOT_SIZE - 32U,
                             p + CRASH_SLOT_SIZE - 32U);
}

static uint32_t slot_address(uint32_t base, uint32_t slot)
{ return base + CRASH_SLOT_AREA_OFFSET + slot * CRASH_SLOT_SIZE; }

static void rebuild_view(void)
{
    crash_gdb_view_t next; memset(&next, 0, sizeof(next));
    next.magic = CRASH_VIEW_MAGIC; next.version = CRASH_FORMAT_VERSION;
    uint32_t active = 0U;
    if (sector_valid(CRASH_LIBRARY_BASE)) active = CRASH_LIBRARY_BASE;
    if (sector_valid(CRASH_LIBRARY_SECTOR_B) && ((active == 0U) ||
        ((int32_t)(((const crash_sector_header_t *)CRASH_LIBRARY_SECTOR_B)->generation -
                   ((const crash_sector_header_t *)active)->generation) > 0))) active = CRASH_LIBRARY_SECTOR_B;
    if (active != 0U) next.active_generation = ((const crash_sector_header_t *)active)->generation;
    for (uint32_t sector = 0U; sector < 2U; ++sector) {
        uint32_t base = sector ? CRASH_LIBRARY_SECTOR_B : CRASH_LIBRARY_BASE;
        if (!sector_valid(base)) continue;
        for (uint32_t slot = 0U; slot < CRASH_SLOT_COUNT; ++slot) {
            uint32_t address = slot_address(base, slot);
            if (!capsule_valid(address)) continue;
            const crash_capsule_t *c = (const crash_capsule_t *)address;
            uint32_t pos = next.valid_count;
            if (pos < CRASH_SLOT_COUNT) next.valid_count++;
            else { pos = 0U; for (uint32_t i=1U;i<CRASH_SLOT_COUNT;++i)
                if ((int32_t)(next.descriptors[i].sequence-next.descriptors[pos].sequence)<0) pos=i; }
            next.descriptors[pos] = (crash_gdb_descriptor_t){c->header.sequence,address,c->header.crc32,1U};
        }
    }
    for (uint32_t i=0U;i<next.valid_count;++i) for (uint32_t j=i+1U;j<next.valid_count;++j)
        if ((int32_t)(next.descriptors[j].sequence-next.descriptors[i].sequence)<0) {
            crash_gdb_descriptor_t t=next.descriptors[i]; next.descriptors[i]=next.descriptors[j]; next.descriptors[j]=t; }
    if (next.valid_count) {
        next.oldest_sequence=next.descriptors[0].sequence;
        next.newest_sequence=next.descriptors[next.valid_count-1U].sequence;
        next.newest_address=next.descriptors[next.valid_count-1U].address;
    }
    if (active) {
        uint32_t slot=0U; while ((slot<CRASH_SLOT_COUNT) &&
            (*(const uint32_t *)slot_address(active,slot)!=0xFFFFFFFFUL)) ++slot;
        if (slot<CRASH_SLOT_COUNT) next.next_address=slot_address(active,slot);
    }
    next.clear_command=0U; memcpy(&g_crash_gdb_view,&next,sizeof(next)); __DMB();
}

static void compact_if_full(void)
{
    if ((g_crash_gdb_view.valid_count < CRASH_SLOT_COUNT) || (g_crash_gdb_view.next_address != 0U)) return;
    uint32_t source = g_crash_gdb_view.newest_address < CRASH_LIBRARY_SECTOR_B ? CRASH_LIBRARY_BASE : CRASH_LIBRARY_SECTOR_B;
    uint32_t target = source == CRASH_LIBRARY_BASE ? CRASH_LIBRARY_SECTOR_B : CRASH_LIBRARY_BASE;
    uint32_t target_sector = target == CRASH_LIBRARY_BASE ? FLASH_SECTOR_4 : FLASH_SECTOR_5;
    HAL_FLASH_Unlock();
    if (erase_sector(target_sector) && write_sector_header(target,g_crash_gdb_view.active_generation+1U)) {
        for (uint32_t i=1U;i<CRASH_SLOT_COUNT;++i) {
            memcpy(&g_crash_capsule,(const void *)g_crash_gdb_view.descriptors[i].address,CRASH_SLOT_SIZE);
            if (!write_capsule(slot_address(target,i-1U),&g_crash_capsule)) break;
        }
    }
    HAL_FLASH_Lock(); rebuild_view();
}

void crash_library_init(void)
{
    if (g_crash_gdb_view.clear_command == CRASH_GDB_CLEAR_MAGIC) {
        HAL_FLASH_Unlock(); (void)erase_sector(FLASH_SECTOR_4); (void)erase_sector(FLASH_SECTOR_5); HAL_FLASH_Lock();
    }
    rebuild_view();
    if ((g_crash_gdb_view.active_generation == 0U) && (g_crash_gdb_view.valid_count == 0U)) {
        HAL_FLASH_Unlock(); (void)write_sector_header(CRASH_LIBRARY_BASE,1U); HAL_FLASH_Lock(); rebuild_view();
    }
    compact_if_full();
}

static void bounded_copy(char *dst, uint32_t size, const char *src)
{
    uint32_t i=0U; if (src) while ((i+1U<size) && src[i]) { dst[i]=src[i]; ++i; }
    if (size) dst[i]=0;
}

void crash_library_capture_and_persist(const brick_fatal_record_t *fatal)
{
    if ((fatal == NULL) || (g_writer_active != 0U) || (g_crash_gdb_view.next_address == 0U)) return;
    g_writer_active=1U; memset(&g_crash_capsule,0,sizeof(g_crash_capsule));
    crash_capsule_t *c=&g_crash_capsule;
    c->header.magic=CRASH_CAPSULE_MAGIC; c->header.version=CRASH_FORMAT_VERSION;
    c->header.capsule_size=CRASH_SLOT_SIZE; c->header.sequence=g_crash_gdb_view.newest_sequence+1U;
    c->header.payload_size=sizeof(c->payload);
    bounded_copy(c->payload.firmware_id,sizeof(c->payload.firmware_id),__DATE__ " " __TIME__);
    bounded_copy(c->payload.message,sizeof(c->payload.message),fatal->message);
    bounded_copy(c->payload.file,sizeof(c->payload.file),fatal->file);
    bounded_copy(c->payload.function,sizeof(c->payload.function),fatal->function);
    c->payload.line=fatal->line; c->payload.code=fatal->code; c->payload.entity=fatal->entity;
    c->payload.context=fatal->context; c->payload.requested=fatal->requested; c->payload.capacity=fatal->capacity;
    c->payload.pc=fatal->caller_pc;
    c->payload.lr=(uint32_t)(uintptr_t)__builtin_return_address(0);
    c->payload.sp=fatal->caller_sp;
    c->payload.xpsr=__get_xPSR(); c->payload.msp=__get_MSP(); c->payload.psp=__get_PSP();
    c->payload.control=__get_CONTROL(); c->payload.ipsr=__get_IPSR(); c->payload.primask=__get_PRIMASK();
    c->payload.basepri=__get_BASEPRI(); c->payload.faultmask=__get_FAULTMASK();
    c->payload.cfsr=SCB->CFSR; c->payload.hfsr=SCB->HFSR; c->payload.dfsr=SCB->DFSR;
    c->payload.afsr=SCB->AFSR; c->payload.mmfar=SCB->MMFAR; c->payload.bfar=SCB->BFAR;
    c->payload.reset_reason=RCC->RSR; c->payload.hal_tick=HAL_GetTick(); c->payload.tim5_count=TIM5->CNT;
    uint32_t head=g_control_audio_fifo_layout.head, tail=g_control_audio_fifo_layout.tail;
    c->payload.fifo_head=head; c->payload.fifo_tail=tail; c->payload.fifo_count=head-tail;
    c->payload.fifo_overflow=g_control_audio_fifo_layout.overflow_count;
    c->payload.fifo_invariant_failures=g_control_audio_fifo_layout.invariant_failure_count;
    for (uint32_t i=0U;i<CRASH_FIFO_SNAPSHOT;++i) c->payload.fifo[i]=g_control_audio_fifo_commands[(tail+i)&(CONTROL_AUDIO_FIFO_CAPACITY-1U)];
    c->payload.extra_count=crash_library_capture_extra(c->payload.extra,CRASH_EXTRA_WORDS);
    if (c->payload.extra_count>CRASH_EXTRA_WORDS) c->payload.extra_count=CRASH_EXTRA_WORDS;
    c->header.crc32=capsule_crc(c); c->commit_magic=CRASH_COMMIT_MAGIC;
    c->commit_sequence=c->header.sequence; c->commit_crc=c->header.crc32; c->commit_inverse=~c->header.crc32;
    HAL_FLASH_Unlock(); (void)write_capsule(g_crash_gdb_view.next_address,c); HAL_FLASH_Lock();
}
