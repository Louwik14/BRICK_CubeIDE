/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file            : usb_host.c
  * @version         : v1.0_Cube
  * @brief           : This file implements the USB Host (MIDI only)
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_host.h"
#include "usbh_core.h"

/* USER CODE BEGIN Includes */
#include "usbh_midi.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
/* USER CODE END PV */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/* USER CODE BEGIN 0 */
static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 10);
}

static void uart_log_status(const char *label, USBH_StatusTypeDef status)
{
  char buffer[96];
  snprintf(buffer, sizeof(buffer), "%s status=%d\r\n", label, (int)status);
  uart_log(buffer);
}
/* USER CODE END 0 */

/* user callback declaration */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  uart_log("USBH: MX_USB_HOST_Init begin\r\n");
  /* USER CODE END USB_HOST_Init_PreTreatment */

  USBH_StatusTypeDef status;

  uart_log("USBH: USBH_Init begin\r\n");
  status = USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS);
  uart_log_status("USBH: USBH_Init end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }

  uart_log("USBH: RegisterClass MIDI begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, &USBH_MIDI_Class);
  uart_log_status("USBH: RegisterClass MIDI end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }

  uart_log("USBH: USBH_Start begin\r\n");
  status = USBH_Start(&hUsbHostHS);
  uart_log_status("USBH: USBH_Start end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }

  uart_log("USBH: MX_USB_HOST_Init end\r\n");
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  USBH_Process(&hUsbHostHS);
}

/*
 * user callback definition
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id)
{
  (void)phost;

  switch(id)
  {
  case HOST_USER_SELECT_CONFIGURATION:
    uart_log("USBH: USER_SELECT_CONFIGURATION\r\n");
    break;

  case HOST_USER_DISCONNECTION:
    uart_log("USBH: USER_DISCONNECTION\r\n");
    Appli_state = APPLICATION_DISCONNECT;
    break;

  case HOST_USER_CLASS_ACTIVE:
    uart_log("USBH: USER_CLASS_ACTIVE\r\n");
    Appli_state = APPLICATION_READY;
    break;

  case HOST_USER_CONNECTION:
    uart_log("USBH: USER_CONNECTION\r\n");
    Appli_state = APPLICATION_START;
    break;

  default:
    uart_log("USBH: USER_EVENT_UNKNOWN\r\n");
    break;
  }
}
