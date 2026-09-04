/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.h
  * @version        : v1.0_Cube
  * @brief          : Header for usb_device.c file.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USB_DEVICE__H__
#define __USB_DEVICE__H__

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/** @addtogroup BRICK6_TINYUSB_DEVICE
  * @{
  */

/** @defgroup BRICK6_TINYUSB_DEVICE_API BRICK6_TINYUSB_DEVICE_API
  * @brief Device file for Usb otg low level driver.
  * @{
  */

/** @defgroup BRICK6_TINYUSB_DEVICE_STATE BRICK6_TINYUSB_DEVICE_STATE
  * @brief Public variables.
  * @{
  */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/*
 * -- Insert your variables declaration here --
 */
/* USER CODE BEGIN VARIABLES */

/* USER CODE END VARIABLES */
/**
  * @}
  */

/** @defgroup BRICK6_TINYUSB_DEVICE_FUNCTIONS BRICK6_TINYUSB_DEVICE_FUNCTIONS
  * @brief Declaration of public functions for Usb device.
  * @{
  */

/** USB Device initialization function. */
/* Device startup is owned by usb_role_manager. */

/*
 * -- Insert functions declaration here --
 */
/* USER CODE BEGIN FD */
uint8_t usb_device_start(void);
uint8_t usb_device_stop(void);
uint8_t usb_device_is_started(void);
uint8_t usb_device_is_ready(void);
void usb_device_process(void);
void usb_device_irq(void);
uint16_t usb_device_send_packets(const uint8_t *packets, uint16_t bytes_len);

/* USER CODE END FD */
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */

#ifdef __cplusplus
}
#endif

#endif /* __USB_DEVICE__H__ */
