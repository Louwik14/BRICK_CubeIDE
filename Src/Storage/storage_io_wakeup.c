#include "Storage/storage_io_wakeup.h"

#include "cmsis_os.h"
#include "stm32h7xx.h"

extern osThreadId_t STORAGE_IOHandle;

static volatile uint32_t g_storage_io_sample_wakeup = UINT32_MAX;

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

void storage_io_schedule_sample_wakeup(uint64_t sample_time)
{
    const uint32_t due = (uint32_t)sample_time;
    const uint32_t current = g_storage_io_sample_wakeup;
    if ((current == UINT32_MAX)
            || ((int32_t)(due - current) < 0))
    {
        g_storage_io_sample_wakeup = due;
        __DMB();
    }
}

void storage_io_sample_event(uint64_t sample_time)
{
    const uint32_t due = g_storage_io_sample_wakeup;
    if ((due != UINT32_MAX)
            && ((int32_t)((uint32_t)sample_time - due) >= 0)
            && (g_storage_io_sample_wakeup == due))
    {
        g_storage_io_sample_wakeup = UINT32_MAX;
        storage_io_wakeup(STORAGE_IO_WAKE_WORK);
    }
}
