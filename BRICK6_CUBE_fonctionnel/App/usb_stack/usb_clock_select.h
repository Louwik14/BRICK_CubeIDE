#ifndef APP_USB_STACK_USB_CLOCK_SELECT_H_
#define APP_USB_STACK_USB_CLOCK_SELECT_H_

#include "stm32h7xx_hal.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

#ifndef USB_CLOCK_DIAG_ENABLE
#define USB_CLOCK_DIAG_ENABLE 1
#endif

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

#if USB_CLOCK_DIAG_ENABLE
static inline void usb_stack_log_clock_diag(const char *tag, uint32_t requested_source, HAL_StatusTypeDef hal_status)
{
  extern UART_HandleTypeDef huart1;
  char buffer[192];
  RCC_PeriphCLKInitTypeDef periph_clk = {0};
  HAL_RCCEx_GetPeriphCLKConfig(&periph_clk);

  (void)snprintf(buffer, sizeof(buffer),
                 "%s req=%lu cur=%lu st=%lu D2CCIP2R=0x%08lX CR=0x%08lX PLLCFGR=0x%08lX PLL1DIVR=0x%08lX\r\n",
                 tag,
                 (unsigned long)requested_source,
                 (unsigned long)periph_clk.UsbClockSelection,
                 (unsigned long)hal_status,
                 (unsigned long)RCC->D2CCIP2R,
                 (unsigned long)RCC->CR,
                 (unsigned long)RCC->PLLCFGR,
                 (unsigned long)RCC->PLL1DIVR);
  HAL_UART_Transmit(&huart1, (uint8_t *)buffer, (uint16_t)strlen(buffer), 100U);
}
#else
static inline void usb_stack_log_clock_diag(const char *tag, uint32_t requested_source, HAL_StatusTypeDef hal_status)
{
  (void)tag;
  (void)requested_source;
  (void)hal_status;
}
#endif

#endif /* APP_USB_STACK_USB_CLOCK_SELECT_H_ */
