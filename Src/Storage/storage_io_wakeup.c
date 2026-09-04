#include "Storage/storage_io_wakeup.h"

#include "cmsis_os.h"

extern osThreadId_t STORAGE_IOHandle;

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
