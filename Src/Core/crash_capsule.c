#include "Core/crash_capsule.h"

#include <stddef.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#define CRASH_CAPSULE_MAGIC          0x42364350UL
#define CRASH_CAPSULE_VERSION        3U
#define CRASH_CAPSULE_SLOT_BYTES     512U
#define CRASH_CAPSULE_SLOT_COUNT     2U
#define CRASH_CAPSULE_BANK_COUNT     2U
#define CRASH_CAPSULE_COMMIT_WRITING 0x57524954UL
#define CRASH_CAPSULE_COMMIT_VALID   0x56414C44UL
#define CRASH_CAPSULE_CRC_INIT       0xFFFFFFFFUL
#define CRASH_CAPSULE_CRC_POLY       0xEDB88320UL

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t generation;
    uint32_t crc32;
    uint32_t commit;
    crash_capsule_snapshot_t payload;
    uint8_t reserved[CRASH_CAPSULE_SLOT_BYTES
                     - 20U - sizeof(crash_capsule_snapshot_t)];
} crash_capsule_slot_t;

_Static_assert(sizeof(crash_capsule_breadcrumb_t) == 16U,
               "Crash capsule breadcrumb layout changed");
_Static_assert(sizeof(crash_capsule_slot_t) == CRASH_CAPSULE_SLOT_BYTES,
               "Crash capsule slot must remain 512 bytes");
_Static_assert((CRASH_CAPSULE_SLOT_BYTES * CRASH_CAPSULE_SLOT_COUNT
                * CRASH_CAPSULE_BANK_COUNT) <= 4096U,
               "Crash capsule exceeds Backup SRAM");

__attribute__((section(".backup_sram"), aligned(32), used))
static volatile crash_capsule_slot_t
    g_crash_capsule_slots[CRASH_CAPSULE_SLOT_COUNT];
__attribute__((section(".backup_sram"), aligned(32), used))
static volatile crash_capsule_slot_t
    g_crash_capsule_recovery_slots[CRASH_CAPSULE_SLOT_COUNT];

static crash_capsule_snapshot_t g_crash_capsule_working;
static crash_capsule_snapshot_t g_crash_capsule_recovery;
static uint32_t g_crash_capsule_generation;
static uint32_t g_crash_capsule_recovery_generation;
static uint8_t g_crash_capsule_slot;
static uint8_t g_crash_capsule_recovery_slot;
static uint8_t g_crash_capsule_valid;
static uint8_t g_crash_capsule_recovery_valid;
static uint8_t g_crash_capsule_boot_recovery_valid;
static uint8_t g_crash_capsule_ready;
static uint8_t g_crash_capsule_reset_flags_captured;
static uint32_t g_crash_capsule_boot_reset_flags;

__attribute__((section(".dtcm_fault"), aligned(8), used, externally_visible))
uint32_t g_crash_capsule_fault_stack[256];

static uint32_t crash_capsule_crc_bytes(uint32_t crc,
                                        const void *data,
                                        uint32_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (uint32_t byte_index = 0U; byte_index < size; ++byte_index)
    {
        crc ^= bytes[byte_index];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (CRASH_CAPSULE_CRC_POLY & mask);
        }
    }
    return crc;
}

static uint32_t crash_capsule_crc(uint32_t generation,
                                  const crash_capsule_snapshot_t *payload)
{
    const uint32_t magic = CRASH_CAPSULE_MAGIC;
    const uint16_t version = CRASH_CAPSULE_VERSION;
    const uint16_t payload_size = (uint16_t)sizeof(*payload);
    uint32_t crc = CRASH_CAPSULE_CRC_INIT;
    crc = crash_capsule_crc_bytes(crc, &magic, sizeof(magic));
    crc = crash_capsule_crc_bytes(crc, &version, sizeof(version));
    crc = crash_capsule_crc_bytes(crc, &payload_size, sizeof(payload_size));
    crc = crash_capsule_crc_bytes(crc, &generation, sizeof(generation));
    crc = crash_capsule_crc_bytes(crc, payload, sizeof(*payload));
    return ~crc;
}

static void crash_capsule_copy_from_volatile(
    crash_capsule_snapshot_t *destination,
    const volatile crash_capsule_snapshot_t *source)
{
    uint32_t *dst = (uint32_t *)destination;
    const volatile uint32_t *src = (const volatile uint32_t *)source;
    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(*destination) / sizeof(uint32_t));
         ++index)
    {
        dst[index] = src[index];
    }
}

static void crash_capsule_copy_to_volatile(
    volatile crash_capsule_snapshot_t *destination,
    const crash_capsule_snapshot_t *source)
{
    volatile uint32_t *dst = (volatile uint32_t *)destination;
    const uint32_t *src = (const uint32_t *)source;
    for (uint32_t index = 0U;
         index < (uint32_t)(sizeof(*source) / sizeof(uint32_t));
         ++index)
    {
        dst[index] = src[index];
    }
}

static uint8_t crash_capsule_slot_read(
    const volatile crash_capsule_slot_t *slots,
    uint8_t slot_index,
    crash_capsule_snapshot_t *out_payload,
    uint32_t *out_generation)
{
    if (out_payload == NULL)
    {
        return 0U;
    }

    const volatile crash_capsule_slot_t *slot =
        &slots[slot_index];
    if ((slot->commit != CRASH_CAPSULE_COMMIT_VALID)
        || (slot->magic != CRASH_CAPSULE_MAGIC)
        || (slot->version != CRASH_CAPSULE_VERSION)
        || (slot->payload_size != sizeof(crash_capsule_snapshot_t)))
    {
        return 0U;
    }

    crash_capsule_copy_from_volatile(out_payload, &slot->payload);
    const uint32_t generation = slot->generation;
    if (slot->crc32 != crash_capsule_crc(generation, out_payload))
    {
        return 0U;
    }

    if (out_generation != NULL)
    {
        *out_generation = generation;
    }
    return 1U;
}

static void crash_capsule_write_slot(
    volatile crash_capsule_slot_t *slots,
    uint8_t target,
    uint32_t generation,
    const crash_capsule_snapshot_t *payload)
{
    volatile crash_capsule_slot_t *slot = &slots[target];
    slot->commit = CRASH_CAPSULE_COMMIT_WRITING;
    __DSB();
    slot->magic = CRASH_CAPSULE_MAGIC;
    slot->version = CRASH_CAPSULE_VERSION;
    slot->payload_size = (uint16_t)sizeof(*payload);
    slot->generation = generation;
    crash_capsule_copy_to_volatile(&slot->payload, payload);
    slot->crc32 = crash_capsule_crc(generation, payload);
    __DSB();
    slot->commit = CRASH_CAPSULE_COMMIT_VALID;
    __DSB();
}

static void crash_capsule_commit(void)
{
    if (g_crash_capsule_ready == 0U)
    {
        return;
    }

    const uint8_t target =
        (g_crash_capsule_valid != 0U) ? (uint8_t)(g_crash_capsule_slot ^ 1U)
                                      : 0U;
    const uint32_t generation = g_crash_capsule_generation + 1U;
    crash_capsule_write_slot(g_crash_capsule_slots, target, generation,
                             &g_crash_capsule_working);

    g_crash_capsule_generation = generation;
    g_crash_capsule_slot = target;
    g_crash_capsule_valid = 1U;
}

static void crash_capsule_recovery_commit(
    const crash_capsule_snapshot_t *snapshot)
{
    const uint8_t target =
        (g_crash_capsule_recovery_valid != 0U)
            ? (uint8_t)(g_crash_capsule_recovery_slot ^ 1U) : 0U;
    const uint32_t generation = g_crash_capsule_recovery_generation + 1U;
    crash_capsule_write_slot(g_crash_capsule_recovery_slots, target,
                             generation, snapshot);
    g_crash_capsule_recovery = *snapshot;
    g_crash_capsule_recovery_generation = generation;
    g_crash_capsule_recovery_slot = target;
    g_crash_capsule_recovery_valid = 1U;
}

void crash_capsule_capture_reset_flags_early(void)
{
    g_crash_capsule_boot_reset_flags = RCC->RSR;
    g_crash_capsule_reset_flags_captured = 1U;
    RCC->RSR |= RCC_RSR_RMVF;
    __DSB();
}

uint8_t crash_capsule_init(void)
{
    if (g_crash_capsule_reset_flags_captured == 0U)
    {
        crash_capsule_capture_reset_flags_early();
    }
    const uint32_t boot_reset_flags = g_crash_capsule_boot_reset_flags;
    g_crash_capsule_ready = 0U;
    g_crash_capsule_valid = 0U;
    g_crash_capsule_recovery_valid = 0U;
    g_crash_capsule_boot_recovery_valid = 0U;
    g_crash_capsule_generation = 0U;
    g_crash_capsule_recovery_generation = 0U;
    g_crash_capsule_slot = 0U;
    g_crash_capsule_recovery_slot = 0U;
    memset(&g_crash_capsule_working, 0, sizeof(g_crash_capsule_working));
    memset(&g_crash_capsule_recovery, 0, sizeof(g_crash_capsule_recovery));

    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_BKPRAM_CLK_ENABLE();
    if (HAL_PWREx_EnableBkUpReg() != HAL_OK)
    {
        return 0U;
    }
    g_crash_capsule_ready = 1U;
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk
                  | SCB_SHCSR_BUSFAULTENA_Msk
                  | SCB_SHCSR_USGFAULTENA_Msk;
    __DSB();
    __ISB();

    crash_capsule_snapshot_t candidate;
    uint32_t candidate_generation = 0U;
    if (crash_capsule_slot_read(g_crash_capsule_slots, 0U, &candidate,
                                &candidate_generation) != 0U)
    {
        g_crash_capsule_working = candidate;
        g_crash_capsule_generation = candidate_generation;
        g_crash_capsule_slot = 0U;
        g_crash_capsule_valid = 1U;
    }
    if ((crash_capsule_slot_read(g_crash_capsule_slots, 1U, &candidate,
                                 &candidate_generation) != 0U)
        && ((g_crash_capsule_valid == 0U)
            || ((int32_t)(candidate_generation
                          - g_crash_capsule_generation) > 0)))
    {
        g_crash_capsule_working = candidate;
        g_crash_capsule_generation = candidate_generation;
        g_crash_capsule_slot = 1U;
        g_crash_capsule_valid = 1U;
    }

    candidate_generation = 0U;
    if (crash_capsule_slot_read(g_crash_capsule_recovery_slots, 0U,
                                &candidate, &candidate_generation) != 0U)
    {
        g_crash_capsule_recovery = candidate;
        g_crash_capsule_recovery_generation = candidate_generation;
        g_crash_capsule_recovery_slot = 0U;
        g_crash_capsule_recovery_valid = 1U;
    }
    if ((crash_capsule_slot_read(g_crash_capsule_recovery_slots, 1U,
                                 &candidate, &candidate_generation) != 0U)
        && ((g_crash_capsule_recovery_valid == 0U)
            || ((int32_t)(candidate_generation
                          - g_crash_capsule_recovery_generation) > 0)))
    {
        g_crash_capsule_recovery = candidate;
        g_crash_capsule_recovery_generation = candidate_generation;
        g_crash_capsule_recovery_slot = 1U;
        g_crash_capsule_recovery_valid = 1U;
    }

    if ((g_crash_capsule_valid != 0U)
        && ((g_crash_capsule_working.session_state
             == CRASH_CAPSULE_SESSION_FAULTED)
            || ((g_crash_capsule_working.session_state
                 == CRASH_CAPSULE_SESSION_RUNNING)
                && ((boot_reset_flags & RCC_RSR_IWDG1RSTF) != 0U))))
    {
        g_crash_capsule_working.reset_flags = boot_reset_flags;
        if (g_crash_capsule_working.session_state
            == CRASH_CAPSULE_SESSION_RUNNING)
        {
            g_crash_capsule_working.session_state =
                CRASH_CAPSULE_SESSION_FAULTED;
            g_crash_capsule_working.fault_type =
                CRASH_CAPSULE_FAULT_WATCHDOG;
            g_crash_capsule_working.crash_count++;
        }
        crash_capsule_commit();
        crash_capsule_recovery_commit(&g_crash_capsule_working);
        g_crash_capsule_boot_recovery_valid = 1U;
    }
    else if ((g_crash_capsule_valid != 0U)
             && (g_crash_capsule_working.session_state
                 == CRASH_CAPSULE_SESSION_RUNNING))
    {
        g_crash_capsule_working.reset_flags = boot_reset_flags;
        g_crash_capsule_working.session_state =
            CRASH_CAPSULE_SESSION_STOPPED;
        crash_capsule_commit();
    }
    return 1U;
}

uint8_t crash_capsule_is_ready(void)
{
    return g_crash_capsule_ready;
}

uint8_t crash_capsule_get_latest(crash_capsule_snapshot_t *out_snapshot)
{
    if ((out_snapshot == NULL) || (g_crash_capsule_valid == 0U))
    {
        return 0U;
    }
    *out_snapshot = g_crash_capsule_working;
    return 1U;
}

uint8_t crash_capsule_get_recovery(crash_capsule_snapshot_t *out_snapshot)
{
    if ((out_snapshot == NULL) || (g_crash_capsule_recovery_valid == 0U))
    {
        return 0U;
    }
    *out_snapshot = g_crash_capsule_recovery;
    return 1U;
}

uint8_t crash_capsule_get_boot_recovery(
    crash_capsule_snapshot_t *out_snapshot)
{
    if (g_crash_capsule_boot_recovery_valid == 0U)
    {
        return 0U;
    }
    return crash_capsule_get_recovery(out_snapshot);
}

uint8_t crash_capsule_recovery_mark_reported(void)
{
    if ((g_crash_capsule_recovery_valid == 0U)
        || (g_crash_capsule_recovery.session_state
            != CRASH_CAPSULE_SESSION_FAULTED))
    {
        return 0U;
    }
    g_crash_capsule_recovery.session_state =
        CRASH_CAPSULE_SESSION_REPORTED;
    crash_capsule_recovery_commit(&g_crash_capsule_recovery);
    return 1U;
}

void crash_capsule_begin_session(uint32_t seed)
{
    memset(&g_crash_capsule_working, 0, sizeof(g_crash_capsule_working));
    g_crash_capsule_working.session_state = CRASH_CAPSULE_SESSION_RUNNING;
    g_crash_capsule_working.seed = seed;
    crash_capsule_commit();
}

void crash_capsule_watchdog_arm(uint32_t engine_tick)
{
    if (g_crash_capsule_working.session_state
        != CRASH_CAPSULE_SESSION_RUNNING)
    {
        return;
    }
    g_crash_capsule_working.watchdog_armed = 1U;
    g_crash_capsule_working.heartbeat_count = 0U;
    g_crash_capsule_working.heartbeat_engine_tick = engine_tick;
    crash_capsule_commit();
}

void crash_capsule_watchdog_checkpoint(uint32_t heartbeat_count,
                                       uint32_t engine_tick)
{
    if (g_crash_capsule_working.session_state
        != CRASH_CAPSULE_SESSION_RUNNING)
    {
        return;
    }
    g_crash_capsule_working.heartbeat_count = heartbeat_count;
    g_crash_capsule_working.heartbeat_engine_tick = engine_tick;
    crash_capsule_commit();
}

void crash_capsule_record_breadcrumb(
    const crash_capsule_breadcrumb_t *breadcrumb,
    uint32_t warning_count,
    uint32_t error_count,
    uint32_t crash_count)
{
    if ((breadcrumb == NULL)
        || (g_crash_capsule_working.session_state
            != CRASH_CAPSULE_SESSION_RUNNING))
    {
        return;
    }

    const uint32_t write_index =
        g_crash_capsule_working.breadcrumb_write_index
        % CRASH_CAPSULE_BREADCRUMB_CAPACITY;
    g_crash_capsule_working.breadcrumbs[write_index] = *breadcrumb;
    g_crash_capsule_working.breadcrumb_write_index =
        (write_index + 1U) % CRASH_CAPSULE_BREADCRUMB_CAPACITY;
    if (g_crash_capsule_working.breadcrumb_count
        < CRASH_CAPSULE_BREADCRUMB_CAPACITY)
    {
        g_crash_capsule_working.breadcrumb_count++;
    }
    g_crash_capsule_working.action_index = breadcrumb->index;
    g_crash_capsule_working.logical_tick = breadcrumb->logical_tick;
    g_crash_capsule_working.warning_count = warning_count;
    g_crash_capsule_working.error_count = error_count;
    g_crash_capsule_working.crash_count = crash_count;
    crash_capsule_commit();
}

void crash_capsule_end_session(uint32_t warning_count,
                               uint32_t error_count,
                               uint32_t crash_count)
{
    if (g_crash_capsule_working.session_state != CRASH_CAPSULE_SESSION_RUNNING)
    {
        return;
    }
    g_crash_capsule_working.session_state = CRASH_CAPSULE_SESSION_STOPPED;
    g_crash_capsule_working.warning_count = warning_count;
    g_crash_capsule_working.error_count = error_count;
    g_crash_capsule_working.crash_count = crash_count;
    crash_capsule_commit();
}

static uint8_t crash_capsule_stack_range_valid(const uint32_t *stack_pointer,
                                                uint32_t word_count)
{
    const uintptr_t begin = (uintptr_t)stack_pointer;
    const uintptr_t end = begin + ((uintptr_t)word_count * sizeof(uint32_t));
    if ((stack_pointer == NULL) || (end < begin))
    {
        return 0U;
    }

    return (((begin >= 0x20000000UL) && (end <= 0x20020000UL))
            || ((begin >= 0x24000000UL) && (end <= 0x24080000UL))
            || ((begin >= 0x30000000UL) && (end <= 0x30048000UL))
            || ((begin >= 0x38000000UL) && (end <= 0x38010000UL)))
        ? 1U : 0U;
}

__attribute__((noreturn, noinline, used, externally_visible))
void crash_capsule_fault_capture_and_reset(
    const uint32_t *stack_pointer,
    uint32_t exc_return,
    crash_capsule_fault_type_t fault_type)
{
    __disable_irq();

    if ((g_crash_capsule_ready != 0U)
        && (g_crash_capsule_working.session_state
            == CRASH_CAPSULE_SESSION_RUNNING))
    {
        const uint32_t extended_words =
            ((exc_return & (1UL << 4U)) == 0U) ? 18U : 0U;
        g_crash_capsule_working.session_state =
            CRASH_CAPSULE_SESSION_FAULTED;
        g_crash_capsule_working.fault_type = (uint32_t)fault_type;
        g_crash_capsule_working.sp = (uint32_t)(uintptr_t)stack_pointer;
        g_crash_capsule_working.exc_return = exc_return;
        g_crash_capsule_working.cfsr = SCB->CFSR;
        g_crash_capsule_working.hfsr = SCB->HFSR;
        g_crash_capsule_working.dfsr = SCB->DFSR;
        g_crash_capsule_working.afsr = SCB->AFSR;
        g_crash_capsule_working.bfar = SCB->BFAR;
        g_crash_capsule_working.mmfar = SCB->MMFAR;
        g_crash_capsule_working.icsr = SCB->ICSR;
        g_crash_capsule_working.shcsr = SCB->SHCSR;
        g_crash_capsule_working.crash_count++;

        if (crash_capsule_stack_range_valid(
                stack_pointer, extended_words + 8U) != 0U)
        {
            const uint32_t *core_frame = stack_pointer + extended_words;
            g_crash_capsule_working.r0 = core_frame[0];
            g_crash_capsule_working.r1 = core_frame[1];
            g_crash_capsule_working.r2 = core_frame[2];
            g_crash_capsule_working.r3 = core_frame[3];
            g_crash_capsule_working.r12 = core_frame[4];
            g_crash_capsule_working.lr = core_frame[5];
            g_crash_capsule_working.pc = core_frame[6];
            g_crash_capsule_working.xpsr = core_frame[7];
        }
        crash_capsule_commit();
    }

#if defined(DEBUG)
    if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0U)
    {
        __BKPT(0);
    }
#endif
    __DSB();
    __ISB();
    NVIC_SystemReset();
}
