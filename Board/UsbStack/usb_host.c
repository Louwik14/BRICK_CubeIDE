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
#include "midi_host.h"

/* USER CODE BEGIN Includes */
#include "usbh_midi.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/
/* USER CODE END PV */

/* USB Host core handle declaration */
USBH_HandleTypeDef hUsbHostHS;
ApplicationTypeDef Appli_state = APPLICATION_IDLE;

/* USER CODE BEGIN 0 */
static uint8_t g_usb_host_started = 0U;
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
  /* USER CODE END USB_HOST_Init_PreTreatment */

  (void)usb_host_start();
}

uint8_t usb_host_start(void)
{
  if (g_usb_host_started != 0U)
  {
    return 1U;
  }

  USBH_StatusTypeDef status;

  status = USBH_Init(&hUsbHostHS, USBH_UserProcess, HOST_FS);
  if (status != USBH_OK)
  {
    return 0U;
  }

  status = USBH_RegisterClass(&hUsbHostHS, &USBH_MIDI_Class);
  if (status != USBH_OK)
  {
    (void)USBH_DeInit(&hUsbHostHS);
    return 0U;
  }

  status = USBH_Start(&hUsbHostHS);
  if (status != USBH_OK)
  {
    (void)USBH_DeInit(&hUsbHostHS);
    return 0U;
  }

  g_usb_host_started = 1U;
  return 1U;
}

uint8_t usb_host_stop(void)
{
  midi_host_rx_discard_pending();

  if (g_usb_host_started == 0U)
  {
    return 1U;
  }

  (void)USBH_Stop(&hUsbHostHS);
  (void)USBH_DeInit(&hUsbHostHS);
  Appli_state = APPLICATION_IDLE;
  g_usb_host_started = 0U;
  return 1U;
}

uint8_t usb_host_is_started(void)
{
  return g_usb_host_started;
}

/*
 * Background task
 */
void MX_USB_HOST_Process(void)
{
  if (g_usb_host_started != 0U)
  {
    USBH_Process(&hUsbHostHS);
  }
}

void usb_host_tasklet_poll_bounded(uint32_t max_packets)
{
  uint32_t n = 0U;
  if (g_usb_host_started == 0U)
  {
    return;
  }

  for (; n < max_packets; n++)
  {
    USBH_Process(&hUsbHostHS);
    if (hUsbHostHS.gState == HOST_IDLE)
    {
      break;
    }
  }

  if ((max_packets > 0U) && (n >= max_packets) && (hUsbHostHS.gState != HOST_IDLE))
  {
  }
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
    break;

  case HOST_USER_DISCONNECTION:
    Appli_state = APPLICATION_DISCONNECT;
    break;

  case HOST_USER_CLASS_ACTIVE:
    Appli_state = APPLICATION_READY;
    break;

  case HOST_USER_CLASS_SELECTED:
    break;

  case HOST_USER_CONNECTION:
    Appli_state = APPLICATION_START;
    break;

  case HOST_USER_UNRECOVERED_ERROR:
    Appli_state = APPLICATION_DISCONNECT;
    break;

  default:
    break;
  }
}
