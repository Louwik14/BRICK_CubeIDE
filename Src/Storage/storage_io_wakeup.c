#include "Storage/storage_io_wakeup.h"

#include "cmsis_os.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx.h"

extern osThreadId_t STORAGE_IOHandle;

static volatile uint32_t g_storage_io_sample_wakeup = UINT32_MAX;
static volatile uint32_t g_storage_io_runnable_bitmap;
static volatile uint32_t g_storage_io_resource_wait_bitmap;
static volatile uint32_t g_storage_io_deadlines_ms[STORAGE_OWNER_COUNT];
static volatile uint32_t g_storage_io_sample_deadlines[STORAGE_OWNER_COUNT];

static uint8_t storage_io_owner_valid(storage_io_owner_t owner)
{
    return (owner < STORAGE_OWNER_COUNT) ? 1U : 0U;
}

static uint8_t storage_io_deadline_before(uint32_t a, uint32_t b)
{
    return ((int32_t)(a - b) < 0) ? 1U : 0U;
}

void storage_io_init(void)
{
    __disable_irq();
    g_storage_io_runnable_bitmap = 0U;
    g_storage_io_resource_wait_bitmap = 0U;
    g_storage_io_sample_wakeup = UINT32_MAX;
    for (uint32_t i = 0U; i < STORAGE_OWNER_COUNT; ++i)
    {
        g_storage_io_deadlines_ms[i] = UINT32_MAX;
        g_storage_io_sample_deadlines[i] = UINT32_MAX;
    }
    __enable_irq();
}

void storage_io_wakeup(uint32_t flags)
{
    if ((flags == 0U) || (STORAGE_IOHandle == NULL))
    {
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        return;
    }

    (void)osThreadFlagsSet(STORAGE_IOHandle, flags);
}

void storage_io_owner_wakeup(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    storage_io_owner_set(owner);
    storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
}

void storage_io_owner_set(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    g_storage_io_runnable_bitmap |= (1UL << (uint32_t)owner);
    __enable_irq();
}

void storage_io_owner_clear(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    g_storage_io_runnable_bitmap &= ~(1UL << (uint32_t)owner);
    __enable_irq();
}

void storage_io_owner_wait_resource(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    g_storage_io_runnable_bitmap &= ~(1UL << (uint32_t)owner);
    g_storage_io_resource_wait_bitmap |= (1UL << (uint32_t)owner);
    __enable_irq();
}

void storage_io_resource_available(void)
{
    uint32_t waiting;
    __disable_irq();
    waiting = g_storage_io_resource_wait_bitmap;
    g_storage_io_resource_wait_bitmap = 0U;
    g_storage_io_runnable_bitmap |= waiting;
    __enable_irq();
    if (waiting != 0U)
        storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
}

uint8_t storage_io_owner_test(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return 0U;
    return ((storage_io_owner_snapshot() & (1UL << (uint32_t)owner)) != 0U)
        ? 1U : 0U;
}

uint32_t storage_io_owner_snapshot(void)
{
    uint32_t bitmap;
    const uint32_t now_ms = HAL_GetTick();
    __disable_irq();
    for (uint32_t i = 0U; i < STORAGE_OWNER_COUNT; ++i)
    {
        if ((g_storage_io_deadlines_ms[i] != UINT32_MAX)
            && ((int32_t)(now_ms - g_storage_io_deadlines_ms[i]) >= 0))
        {
            g_storage_io_deadlines_ms[i] = UINT32_MAX;
            g_storage_io_runnable_bitmap |= (1UL << i);
        }
    }
    bitmap = g_storage_io_runnable_bitmap;
    __enable_irq();
    return bitmap;
}

void storage_io_schedule_owner_deadline_ms(storage_io_owner_t owner,
                                           uint32_t deadline_ms)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    if ((g_storage_io_deadlines_ms[owner] == UINT32_MAX)
        || storage_io_deadline_before(deadline_ms, g_storage_io_deadlines_ms[owner]))
    {
        g_storage_io_deadlines_ms[owner] = deadline_ms;
    }
    __enable_irq();
}

void storage_io_clear_owner_deadline_ms(storage_io_owner_t owner)
{
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    g_storage_io_deadlines_ms[owner] = UINT32_MAX;
    __enable_irq();
}

uint8_t storage_io_next_deadline_ms(uint32_t now_ms, uint32_t *out_deadline_ms)
{
    uint32_t best = UINT32_MAX;
    (void)now_ms;
    __disable_irq();
    for (uint32_t i = 0U; i < STORAGE_OWNER_COUNT; ++i)
    {
        if ((g_storage_io_deadlines_ms[i] != UINT32_MAX)
            && ((best == UINT32_MAX)
                || storage_io_deadline_before(g_storage_io_deadlines_ms[i], best)))
        {
            best = g_storage_io_deadlines_ms[i];
        }
    }
    __enable_irq();
    if (best == UINT32_MAX)
        return 0U;
    if (out_deadline_ms != NULL)
        *out_deadline_ms = best;
    return 1U;
}

void storage_io_schedule_sample_wakeup(storage_io_owner_t owner,
                                       uint64_t sample_time)
{
    const uint32_t due = (uint32_t)sample_time;
    if (storage_io_owner_valid(owner) == 0U)
        return;
    __disable_irq();
    if ((g_storage_io_sample_deadlines[owner] == UINT32_MAX)
        || storage_io_deadline_before(due, g_storage_io_sample_deadlines[owner]))
    {
        g_storage_io_sample_deadlines[owner] = due;
        if ((g_storage_io_sample_wakeup == UINT32_MAX)
            || storage_io_deadline_before(due, g_storage_io_sample_wakeup))
            g_storage_io_sample_wakeup = due;
    }
    __enable_irq();
}

void storage_io_sample_event(uint64_t sample_time)
{
    /* This is a physical M7 resource-retirement fence: Storage may reclaim
     * a stopped sample resource only after AUDIO has crossed its grace
     * sample.  It is not a musical wake or a generalized CONTROL tick. */
    const uint32_t now = (uint32_t)sample_time;
    uint8_t woke = 0U;
    for (uint32_t i = 0U; i < STORAGE_OWNER_COUNT; ++i)
    {
        const uint32_t due = g_storage_io_sample_deadlines[i];
        if ((due != UINT32_MAX) && ((int32_t)(now - due) >= 0))
        {
            g_storage_io_sample_deadlines[i] = UINT32_MAX;
            storage_io_owner_set((storage_io_owner_t)i);
            woke = 1U;
        }
    }
    if (woke != 0U)
    {
        g_storage_io_sample_wakeup = UINT32_MAX;
        for (uint32_t i = 0U; i < STORAGE_OWNER_COUNT; ++i)
        {
            const uint32_t due = g_storage_io_sample_deadlines[i];
            if ((due != UINT32_MAX)
                && ((g_storage_io_sample_wakeup == UINT32_MAX)
                    || storage_io_deadline_before(due, g_storage_io_sample_wakeup)))
            {
                g_storage_io_sample_wakeup = due;
            }
        }
        storage_io_wakeup(STORAGE_IO_WAKE_RUNNABLE);
    }
}
