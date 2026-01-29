/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_desc.c
  * @version        : v1.0_Cube
  * @brief          : This file implements the USB device descriptors.
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
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"
#include "usbd_brick6_composite.h"

/* USER CODE BEGIN INCLUDE */

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */

/** @addtogroup USBD_DESC
  * @{
  */

/** @defgroup USBD_DESC_Private_TypesDefinitions USBD_DESC_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Defines USBD_DESC_Private_Defines
  * @brief Private defines.
  * @{
  */

#define USBD_VID     1155
#define USBD_LANGID_STRING     1033
#define USBD_MANUFACTURER_STRING     "STMicroelectronics"
#define USBD_PID_FS     22315
#define USBD_PRODUCT_STRING_FS     "STM32 USB MIDI+Audio"
#define USBD_CONFIGURATION_STRING_FS     "MIDI+Audio Config"
#define USBD_INTERFACE_STRING_FS     "Composite Interface"

#define USB_SIZ_BOS_DESC            0x0C

/* USER CODE BEGIN PRIVATE_DEFINES */

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/** @defgroup USBD_DESC_Private_Macros USBD_DESC_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_FunctionPrototypes USBD_DESC_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t * pbuf, uint8_t len);

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_FunctionPrototypes USBD_DESC_Private_FunctionPrototypes
  * @brief Private functions declaration for FS.
  * @{
  */

uint8_t * USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);
uint8_t * USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length);

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Variables USBD_DESC_Private_Variables
  * @brief Private variables.
  * @{
  */

USBD_DescriptorsTypeDef FS_Desc =
{
  USBD_FS_DeviceDescriptor
, USBD_FS_LangIDStrDescriptor
, USBD_FS_ManufacturerStrDescriptor
, USBD_FS_ProductStrDescriptor
, USBD_FS_SerialStrDescriptor
, USBD_FS_ConfigStrDescriptor
, USBD_FS_InterfaceStrDescriptor
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/** USB standard device descriptor. */
__ALIGN_BEGIN uint8_t USBD_FS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END =
{
  0x12,                       /*bLength */
  USB_DESC_TYPE_DEVICE,       /*bDescriptorType*/
  0x00,                       /*bcdUSB */
  0x02,
  0x00,                       /*bDeviceClass*/
  0x00,                       /*bDeviceSubClass*/
  0x00,                       /*bDeviceProtocol*/
  USB_MAX_EP0_SIZE,           /*bMaxPacketSize*/
  LOBYTE(USBD_VID),           /*idVendor*/
  HIBYTE(USBD_VID),           /*idVendor*/
  LOBYTE(USBD_PID_FS),        /*idProduct*/
  HIBYTE(USBD_PID_FS),        /*idProduct*/
  0x00,                       /*bcdDevice rel. 2.00*/
  0x02,
  USBD_IDX_MFC_STR,           /*Index of manufacturer  string*/
  USBD_IDX_PRODUCT_STR,       /*Index of product string*/
  USBD_IDX_SERIAL_STR,        /*Index of serial number string*/
  USBD_MAX_NUM_CONFIGURATION  /*bNumConfigurations*/
};

__ALIGN_BEGIN uint8_t USBD_Brick6_Composite_CfgDesc[BRICK6_COMPOSITE_CONFIG_DESC_SIZE] __ALIGN_END =
{
  0x09,                           /* bLength: Configuration Descriptor size */
  USB_DESC_TYPE_CONFIGURATION,    /* bDescriptorType: Configuration */
  LOBYTE(BRICK6_COMPOSITE_CONFIG_DESC_SIZE),
  HIBYTE(BRICK6_COMPOSITE_CONFIG_DESC_SIZE),
  0x03,                           /* bNumInterfaces */
  0x01,                           /* bConfigurationValue */
  0x00,                           /* iConfiguration */
  0x80,                           /* bmAttributes: Bus Powered */
  0xFA,                           /* MaxPower 500 mA */

  /* Audio Function IAD */
  0x08,                           /* bLength */
  0x0B,                           /* bDescriptorType: IAD */
  BRICK6_AUDIO_CONTROL_IF,        /* bFirstInterface */
  0x02,                           /* bInterfaceCount */
  0x01,                           /* bFunctionClass: AUDIO */
  0x00,                           /* bFunctionSubClass */
  0x00,                           /* bFunctionProtocol */
  0x00,                           /* iFunction */

  /* Audio Control Interface */
  0x09,                           /* bLength */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */
  BRICK6_AUDIO_CONTROL_IF,        /* bInterfaceNumber */
  0x00,                           /* bAlternateSetting */
  0x00,                           /* bNumEndpoints */
  0x01,                           /* bInterfaceClass: AUDIO */
  0x01,                           /* bInterfaceSubClass: AUDIOCONTROL */
  0x00,                           /* bInterfaceProtocol */
  0x00,                           /* iInterface */

  /* Class-specific AC Header */
  0x09,                           /* bLength */
  0x24,                           /* bDescriptorType: CS_INTERFACE */
  0x01,                           /* bDescriptorSubtype: HEADER */
  0x00, 0x01,                     /* bcdADC: 1.00 */
  0x1E, 0x00,                     /* wTotalLength */
  0x01,                           /* bInCollection */
  BRICK6_AUDIO_STREAMING_IF,      /* baInterfaceNr(1) */

  /* Input Terminal Descriptor */
  0x0C,                           /* bLength */
  0x24,                           /* bDescriptorType: CS_INTERFACE */
  0x02,                           /* bDescriptorSubtype: INPUT_TERMINAL */
  0x01,                           /* bTerminalID */
  0x01, 0x02,                     /* wTerminalType: Microphone */
  0x00,                           /* bAssocTerminal */
  0x02,                           /* bNrChannels */
  0x03, 0x00,                     /* wChannelConfig */
  0x00,                           /* iChannelNames */
  0x00,                           /* iTerminal */

  /* Output Terminal Descriptor */
  0x09,                           /* bLength */
  0x24,                           /* bDescriptorType: CS_INTERFACE */
  0x03,                           /* bDescriptorSubtype: OUTPUT_TERMINAL */
  0x02,                           /* bTerminalID */
  0x01, 0x01,                     /* wTerminalType: USB Streaming */
  0x00,                           /* bAssocTerminal */
  0x01,                           /* bSourceID */
  0x00,                           /* iTerminal */

  /* Audio Streaming Interface Alt 0 */
  0x09,                           /* bLength */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */
  BRICK6_AUDIO_STREAMING_IF,      /* bInterfaceNumber */
  0x00,                           /* bAlternateSetting */
  0x00,                           /* bNumEndpoints */
  0x01,                           /* bInterfaceClass: AUDIO */
  0x02,                           /* bInterfaceSubClass: AUDIOSTREAMING */
  0x00,                           /* bInterfaceProtocol */
  0x00,                           /* iInterface */

  /* Audio Streaming Interface Alt 1 */
  0x09,                           /* bLength */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType */
  BRICK6_AUDIO_STREAMING_IF,      /* bInterfaceNumber */
  0x01,                           /* bAlternateSetting */
  0x01,                           /* bNumEndpoints */
  0x01,                           /* bInterfaceClass: AUDIO */
  0x02,                           /* bInterfaceSubClass: AUDIOSTREAMING */
  0x00,                           /* bInterfaceProtocol */
  0x00,                           /* iInterface */

  /* Class-specific AS General Descriptor */
  0x07,                           /* bLength */
  0x24,                           /* bDescriptorType: CS_INTERFACE */
  0x01,                           /* bDescriptorSubtype: AS_GENERAL */
  0x02,                           /* bTerminalLink */
  0x01,                           /* bDelay */
  0x01, 0x00,                     /* wFormatTag: PCM */

  /* Format Type Descriptor */
  0x0B,                           /* bLength */
  0x24,                           /* bDescriptorType: CS_INTERFACE */
  0x02,                           /* bDescriptorSubtype: FORMAT_TYPE */
  0x01,                           /* bFormatType: FORMAT_TYPE_I */
  0x02,                           /* bNrChannels */
  0x04,                           /* bSubFrameSize */
  0x18,                           /* bBitResolution */
  0x01,                           /* bSamFreqType */
  0x80, 0xBB, 0x00,               /* tSamFreq: 48 kHz */

  /* Audio Streaming Endpoint Descriptor */
  0x09,                           /* bLength */
  USB_DESC_TYPE_ENDPOINT,         /* bDescriptorType */
  BRICK6_AUDIO_EP_IN_ADDR,         /* bEndpointAddress */
  0x01,                           /* bmAttributes: Isochronous */
  LOBYTE(BRICK6_AUDIO_EP_IN_SIZE),
  HIBYTE(BRICK6_AUDIO_EP_IN_SIZE),
  0x01,                           /* bInterval */
  0x00,                           /* bRefresh */
  0x00,                           /* bSynchAddress */

  /* Class-specific Audio Endpoint Descriptor */
  0x07,                           /* bLength */
  0x25,                           /* bDescriptorType: CS_ENDPOINT */
  0x01,                           /* bDescriptorSubtype: EP_GENERAL */
  0x00,                           /* bmAttributes */
  0x00,                           /* bLockDelayUnits */
  0x00, 0x00,                     /* wLockDelay */

  /************** MIDI Adapter Standard MS Interface Descriptor ****************/
  0x09,                           /* bLength: Interface Descriptor size */
  USB_DESC_TYPE_INTERFACE,        /* bDescriptorType: Interface descriptor type */
  BRICK6_MIDI_STREAMING_IF,        /* bInterfaceNumber: Index of this interface. */
  0x00,                           /* bAlternateSetting: Alternate setting */
  0x02,                           /* bNumEndpoints */
  0x01,                           /* bInterfaceClass: AUDIO */
  0x03,                           /* bInterfaceSubClass: MIDISTREAMING */
  0x00,                           /* bInterfaceProtocol: Unused */
  0x00,                           /* iInterface: Unused */

  /******************** MIDI Adapter Class-specific MS Interface Descriptor ********************/
  0x07,                           /* bLength: Descriptor size */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x01,                           /* bDescriptorSubtype: MS_HEADER subtype */
  0x00,
  0x01,                           /* BcdADC: Revision of this class specification */
  LOBYTE(BRICK6_MIDI_CLASS_DESC_SIZE),
  HIBYTE(BRICK6_MIDI_CLASS_DESC_SIZE), /* wTotalLength: Total size of class-specific descriptors */

#if MIDI_IN_PORTS_NUM >= 1
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_1,                    /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_2,                    /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_1,                    /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 2
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_3,                    /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_4,                    /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_3,                    /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 3
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_5,                    /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_6,                    /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_5,                    /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 4
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_7,                    /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_8,                    /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_7,                    /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 5
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_9,                    /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_10,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_9,                    /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 6
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_11,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_12,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_11,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 7
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_13,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_14,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_13,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_IN_PORTS_NUM >= 8
  /******************** MIDI Adapter MIDI IN Jack Descriptor (External) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_15,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (Embedded) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_16,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_15,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 1
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_17,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_18,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_17,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 2
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_19,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_20,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_19,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 3
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_21,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_22,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_21,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 4
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_23,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_24,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_23,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 5
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_25,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_26,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_25,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 6
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_27,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_28,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_27,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 7
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_29,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_30,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_29,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

#if MIDI_OUT_PORTS_NUM >= 8
  /******************** MIDI Adapter MIDI IN Jack Descriptor (Embedded) ********************/
  0x06,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x02,                           /* bDescriptorSubtype: MIDI_IN_JACK subtype */
  0x01,                           /* bJackType: EMBEDDED */
  MIDI_JACK_31,                   /* bJackID: ID of this Jack */
  0x00,                           /* iJack: Unused */

  /******************** MIDI Adapter MIDI OUT Jack Descriptor (External) ********************/
  0x09,                           /* bLength: Size of this descriptor, in bytes */
  0x24,                           /* bDescriptorType: CS_INTERFACE descriptor */
  0x03,                           /* bDescriptorSubtype: MIDI_OUT_JACK subtype */
  0x02,                           /* bJackType: EXTERNAL */
  MIDI_JACK_32,                   /* bJackID: ID of this Jack */
  0x01,                           /* bNrInputPins: Number of Input Pins of this Jack */
  MIDI_JACK_31,                   /* BaSourceID(1): ID of the Entity to which this Pin is connected */
  0x01,                           /* BaSourcePin(1): Output Pin number of the Entity to which this Input Pin is connected */
  0x00,                           /* iJack: Unused */
#endif

  /******************** MIDI Adapter Standard Bulk OUT Endpoint Descriptor ********************/
  0x07,                           /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,         /* bDescriptorType: Endpoint */
  MIDI_EPOUT_ADDR,                /* bEndpointAddress: OUT Endpoint */
  0x02,                           /* bmAttributes: Bulk */
  MIDI_EPOUT_SIZE,
  0x00,
  0x00,                           /* bInterval */

  /******************** MIDI Adapter Class-specific Bulk OUT Endpoint Descriptor ********************/
  (4 + MIDI_OUT_PORTS_NUM),       /* bLength: Size of this descriptor, in bytes */
  0x25,                           /* bDescriptorType: CS_ENDPOINT descriptor */
  0x01,                           /* bDescriptorSubtype: MS_GENERAL */
  MIDI_OUT_PORTS_NUM,             /* bNumEmbMIDIJack: Number of embedded MIDI IN Jacks */
#if MIDI_OUT_PORTS_NUM >= 1
  MIDI_JACK_17,                   /* BaAssocJackID(1): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 2
  MIDI_JACK_19,                   /* BaAssocJackID(2): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 3
  MIDI_JACK_21,                   /* BaAssocJackID(3): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 4
  MIDI_JACK_23,                   /* BaAssocJackID(4): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 5
  MIDI_JACK_25,                   /* BaAssocJackID(5): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 6
  MIDI_JACK_27,                   /* BaAssocJackID(6): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 7
  MIDI_JACK_29,                   /* BaAssocJackID(7): ID of the Embedded MIDI IN Jack */
#endif
#if MIDI_OUT_PORTS_NUM >= 8
  MIDI_JACK_31,                   /* BaAssocJackID(8): ID of the Embedded MIDI IN Jack */
#endif

  /******************** MIDI Adapter Standard Bulk IN Endpoint Descriptor ********************/
  0x07,                           /* bLength: Endpoint Descriptor size */
  USB_DESC_TYPE_ENDPOINT,         /* bDescriptorType: Endpoint */
  MIDI_EPIN_ADDR,                 /* bEndpointAddress: IN Endpoint */
  0x02,                           /* bmAttributes: Bulk */
  MIDI_EPIN_SIZE,
  0x00,
  0x00,                           /* bInterval */

  /******************** MIDI Adapter Class-specific Bulk IN Endpoint Descriptor ********************/
  (4 + MIDI_IN_PORTS_NUM),        /* bLength: Size of this descriptor, in bytes */
  0x25,                           /* bDescriptorType: CS_ENDPOINT descriptor */
  0x01,                           /* bDescriptorSubtype: MS_GENERAL */
  MIDI_IN_PORTS_NUM,              /* bNumEmbMIDIJack: Number of embedded MIDI OUT Jacks */
#if MIDI_IN_PORTS_NUM >= 1
  MIDI_JACK_2,                    /* BaAssocJackID(1): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 2
  MIDI_JACK_4,                    /* BaAssocJackID(2): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 3
  MIDI_JACK_6,                    /* BaAssocJackID(3): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 4
  MIDI_JACK_8,                    /* BaAssocJackID(4): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 5
  MIDI_JACK_10,                   /* BaAssocJackID(5): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 6
  MIDI_JACK_12,                   /* BaAssocJackID(6): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 7
  MIDI_JACK_14,                   /* BaAssocJackID(7): ID of the Embedded MIDI OUT Jack */
#endif
#if MIDI_IN_PORTS_NUM >= 8
  MIDI_JACK_16,                   /* BaAssocJackID(8): ID of the Embedded MIDI OUT Jack */
#endif
};

/* USB_DeviceDescriptor */
/** BOS descriptor. */
#if (USBD_LPM_ENABLED == 1)
#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
__ALIGN_BEGIN uint8_t USBD_FS_BOSDesc[USB_SIZ_BOS_DESC] __ALIGN_END =
{
  0x5,
  USB_DESC_TYPE_BOS,
  0xC,
  0x0,
  0x1,  /* 1 device capability*/
        /* device capability*/
  0x7,
  USB_DEVICE_CAPABITY_TYPE,
  0x2,
  0x2,  /* LPM capability bit set*/
  0x0,
  0x0,
  0x0
};
#endif /* (USBD_LPM_ENABLED == 1) */

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Variables USBD_DESC_Private_Variables
  * @brief Private variables.
  * @{
  */

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */

/** USB lang identifier descriptor. */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END =
{
     USB_LEN_LANGID_STR_DESC,
     USB_DESC_TYPE_STRING,
     LOBYTE(USBD_LANGID_STRING),
     HIBYTE(USBD_LANGID_STRING)
};

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif /* defined ( __ICCARM__ ) */
/* Internal string descriptor. */
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

#if defined ( __ICCARM__ ) /*!< IAR Compiler */
  #pragma data_alignment=4
#endif
__ALIGN_BEGIN uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
  USB_SIZ_STRING_SERIAL,
  USB_DESC_TYPE_STRING,
};

/**
  * @}
  */

/** @defgroup USBD_DESC_Private_Functions USBD_DESC_Private_Functions
  * @brief Private functions.
  * @{
  */

/**
  * @brief  Return the device descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_FS_DeviceDesc);
  return USBD_FS_DeviceDesc;
}

/**
  * @brief  Return the LangID string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = sizeof(USBD_LangIDDesc);
  return USBD_LangIDDesc;
}

/**
  * @brief  Return the product string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the manufacturer string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
  return USBD_StrDesc;
}

/**
  * @brief  Return the serial number string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  UNUSED(speed);
  *length = USB_SIZ_STRING_SERIAL;

  /* Update the serial number string descriptor with the data from the unique
   * ID */
  Get_SerialNum();
  /* USER CODE BEGIN USBD_FS_SerialStrDescriptor */

  /* USER CODE END USBD_FS_SerialStrDescriptor */
  return (uint8_t *) USBD_StringSerial;
}

/**
  * @brief  Return the configuration string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == USBD_SPEED_HIGH)
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Return the interface string descriptor
  * @param  speed : Current device speed
  * @param  length : Pointer to data length variable
  * @retval Pointer to descriptor buffer
  */
uint8_t * USBD_FS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
  if(speed == 0)
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_FS, USBD_StrDesc, length);
  }
  else
  {
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING_FS, USBD_StrDesc, length);
  }
  return USBD_StrDesc;
}

/**
  * @brief  Create the serial number string descriptor
  * @param  None
  * @retval None
  */
static void Get_SerialNum(void)
{
  uint32_t deviceserial0;
  uint32_t deviceserial1;
  uint32_t deviceserial2;

  deviceserial0 = *(uint32_t *) DEVICE_ID1;
  deviceserial1 = *(uint32_t *) DEVICE_ID2;
  deviceserial2 = *(uint32_t *) DEVICE_ID3;

  deviceserial0 += deviceserial2;

  if (deviceserial0 != 0)
  {
    IntToUnicode(deviceserial0, &USBD_StringSerial[2], 8);
    IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4);
  }
}

/**
  * @brief  Convert Hex 32Bits value into char
  * @param  value: value to convert
  * @param  pbuf: pointer to the buffer
  * @param  len: buffer length
  * @retval None
  */
static void IntToUnicode(uint32_t value, uint8_t * pbuf, uint8_t len)
{
  uint8_t idx = 0;

  for (idx = 0; idx < len; idx++)
  {
    if (((value >> 28)) < 0xA)
    {
      pbuf[2 * idx] = (value >> 28) + '0';
    }
    else
    {
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;
    }

    value = value << 4;

    pbuf[2 * idx + 1] = 0;
  }
}
/**
  * @}
  */

/**
  * @}
  */

/**
  * @}
  */
