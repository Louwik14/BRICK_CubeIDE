#ifndef APP_USB_STACK_USB_CLOCK_SELECT_H_
#define APP_USB_STACK_USB_CLOCK_SELECT_H_

#include "stm32h7xx_hal.h"

static inline uint32_t usb_stack_get_rcc_usb_clock_source(void)
{
  RCC_PeriphCLKInitTypeDef periph_clk = {0};
  HAL_RCCEx_GetPeriphCLKConfig(&periph_clk);

  if ((periph_clk.UsbClockSelection == RCC_USBCLKSOURCE_PLL) ||
      (periph_clk.UsbClockSelection == RCC_USBCLKSOURCE_PLL3) ||
      (periph_clk.UsbClockSelection == RCC_USBCLKSOURCE_HSI48))
  {
    return periph_clk.UsbClockSelection;
  }

  return RCC_USBCLKSOURCE_HSI48;
}

#endif /* APP_USB_STACK_USB_CLOCK_SELECT_H_ */
