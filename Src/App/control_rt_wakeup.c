#include "App/control_rt_wakeup.h"

#include "cmsis_os.h"

extern osThreadId_t CONTROL_RTHandle;

void control_rt_wakeup(uint32_t flags)
{
    if ((flags == 0U) || (CONTROL_RTHandle == NULL))
    {
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        return;
    }

    (void)osThreadFlagsSet(CONTROL_RTHandle, flags);
}
