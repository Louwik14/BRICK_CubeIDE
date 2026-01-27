/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file            : usb_host.c
  * @version         : v1.0_Cube
  * @brief           : This file implements the USB Host
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/

#include "usb_host.h"
#include "usbh_core.h"
#include "usbh_audio.h"
#include "usbh_cdc.h"
#include "usbh_msc.h"
#include "usbh_hid.h"
#include "usbh_mtp.h"

/* USER CODE BEGIN Includes */
#include "usbh_midi.h"
#include "usart.h"
#include <stdio.h>
#include <string.h>

/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/* Private function prototypes -----------------------------------------------*/

/* USER CODE END PFP */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/*
 * -- Insert your variables declaration here --
 */
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

/*
 * user callback declaration
 */
static void USBH_UserProcess(USBH_HandleTypeDef *phost, uint8_t id);

/*
 * -- Insert your external function declaration here --
 */
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * Init USB host library, add supported class and start the library
  * @retval None
  */
void MX_USB_HOST_Init(void)
{
  /* USER CODE BEGIN USB_HOST_Init_PreTreatment */
  uart_log("USBH: MX_USB_HOST_Init begin\r\n");
  /* USER CODE END USB_HOST_Init_PreTreatment */

  /* Init host Library, add supported class and start the library. */
  uart_log("USBH: USBH_Init begin\r\n");
  USBH_StatusTypeDef status = USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_HS);
  uart_log_status("USBH: USBH_Init end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }
  uart_log("USBH: RegisterClass AUDIO begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, USBH_AUDIO_CLASS);
  uart_log_status("USBH: RegisterClass AUDIO end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }
  uart_log("USBH: RegisterClass CDC begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, USBH_CDC_CLASS);
  uart_log_status("USBH: RegisterClass CDC end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }
  uart_log("USBH: RegisterClass MSC begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, USBH_MSC_CLASS);
  uart_log_status("USBH: RegisterClass MSC end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }
  uart_log("USBH: RegisterClass HID begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, USBH_HID_CLASS);
  uart_log_status("USBH: RegisterClass HID end", status);
  if (status != USBH_OK)
  {
    Error_Handler();
  }
  uart_log("USBH: RegisterClass MTP begin\r\n");
  status = USBH_RegisterClass(&hUsbHostHS, USBH_MTP_CLASS);
  uart_log_status("USBH: RegisterClass MTP end", status);
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
  /* USER CODE BEGIN USB_HOST_Init_PostTreatment */
  uart_log("USBH: MX_USB_HOST_Init end\r\n");
  /* USER CODE END USB_HOST_Init_PostTreatment */
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  /* USB Host Background task */
  USBH_Process(&hUsbHostHS);
}
/*
 * user callback definition
 */
static void USBH_UserProcess  (USBH_HandleTypeDef *phost, uint8_t id)
{
  /* USER CODE BEGIN CALL_BACK_1 */
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
  /* USER CODE END CALL_BACK_1 */
}

/**
  * @}
  */

/**
  * @}
  */
