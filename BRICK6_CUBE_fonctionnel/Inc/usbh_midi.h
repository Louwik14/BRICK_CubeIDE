/**
  ******************************************************************************
  * @file    usbh_midi.h
  * @brief   USB Host MIDI class driver header
  ******************************************************************************
  */

#ifndef __USBH_MIDI_H
#define __USBH_MIDI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbh_core.h"
#include <stdbool.h>
#include <stdint.h>

#define USBH_MIDI_CLASS    0x01U
#define USBH_MIDI_SUBCLASS 0x03U
#define USBH_MIDI_PROTOCOL 0x00U

#define USBH_MIDI_PACKET_SIZE   4U
#define USBH_MIDI_RX_QUEUE_LEN  64U
#define USBH_MIDI_TX_QUEUE_LEN  64U
#define USBH_MIDI_RX_BUF_SIZE   64U
#define USBH_MIDI_TX_BUF_SIZE   64U

#ifndef USBH_MIDI_DEBUG
#define USBH_MIDI_DEBUG 1
#endif

USBH_StatusTypeDef USBH_MIDI_ReadPacket(USBH_HandleTypeDef *phost,
                                        uint8_t packet[USBH_MIDI_PACKET_SIZE]);
USBH_StatusTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost,
                                      const uint8_t packet[USBH_MIDI_PACKET_SIZE]);
bool USBH_MIDI_IsReady(USBH_HandleTypeDef *phost);
void USBH_MIDI_DebugDump(void);

extern USBH_ClassTypeDef USBH_MIDI_Class;

#ifdef __cplusplus
}
#endif

#endif /* __USBH_MIDI_H */
