#include "UI/ui_service_wakeup.h"

#include "cmsis_os.h"

extern osThreadId_t UI_SERVICEHandle;

void ui_service_wakeup(uint32_t flags)
{
    if ((flags == 0U) || (UI_SERVICEHandle == NULL))
    {
        return;
    }

    if (osKernelGetState() != osKernelRunning)
    {
        return;
    }

    (void)osThreadFlagsSet(UI_SERVICEHandle, flags);
}
