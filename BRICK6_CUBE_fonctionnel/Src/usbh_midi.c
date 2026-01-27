/**
  ******************************************************************************
  * @file    usbh_midi.c
  * @brief   USB Host MIDI class driver (STM32Cube HAL)
  ******************************************************************************
  */

#include "usbh_midi.h"
#include <string.h>

#ifndef USB_EP_TYPE_MASK
#define USB_EP_TYPE_MASK 0x03U
#endif

typedef enum {
  USBH_MIDI_STATE_IDLE = 0U,
  USBH_MIDI_STATE_TRANSFER,
  USBH_MIDI_STATE_ERROR
} USBH_MIDI_StateTypeDef;

typedef struct {
  uint8_t data[USBH_MIDI_PACKET_SIZE];
} USBH_MIDI_PacketTypeDef;

typedef struct {
  uint8_t interface;
  uint8_t InEp;
  uint8_t OutEp;
  uint16_t InEpSize;
  uint16_t OutEpSize;
  uint8_t InPipe;
  uint8_t OutPipe;
  USBH_MIDI_StateTypeDef state;
  uint8_t rx_buffer[USBH_MIDI_RX_BUF_SIZE];
  uint16_t rx_buffer_size;
  bool rx_in_progress;
  bool tx_in_progress;
  USBH_MIDI_PacketTypeDef rx_queue[USBH_MIDI_RX_QUEUE_LEN];
  uint16_t rx_head;
  uint16_t rx_tail;
  uint16_t rx_count;
  USBH_MIDI_PacketTypeDef tx_queue[USBH_MIDI_TX_QUEUE_LEN];
  uint16_t tx_head;
  uint16_t tx_tail;
  uint16_t tx_count;
} USBH_MIDI_HandleTypeDef;

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost);

static USBH_MIDI_HandleTypeDef midi_handle;

USBH_ClassTypeDef USBH_MIDI_Class = {
  "MIDI",
  USBH_MIDI_CLASS,
  USBH_MIDI_InterfaceInit,
  USBH_MIDI_InterfaceDeInit,
  USBH_MIDI_ClassRequest,
  USBH_MIDI_Process,
  NULL,
  NULL,
};

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost)
{
  if (phost == NULL)
  {
    return USBH_FAIL;
  }

  uint8_t interface = 0xFFU;
  uint8_t max_itf = (phost->device.CfgDesc.bNumInterfaces <= USBH_MAX_NUM_INTERFACES)
                      ? phost->device.CfgDesc.bNumInterfaces
                      : USBH_MAX_NUM_INTERFACES;

  for (uint8_t idx = 0U; idx < max_itf; idx++)
  {
    USBH_InterfaceDescTypeDef *itf = &phost->device.CfgDesc.Itf_Desc[idx];
    if ((itf->bInterfaceClass == USBH_MIDI_CLASS)
        && (itf->bInterfaceSubClass == USBH_MIDI_SUBCLASS))
    {
      interface = idx;
      break;
    }
  }

  if (interface == 0xFFU)
  {
    return USBH_FAIL;
  }

  if (USBH_SelectInterface(phost, interface) != USBH_OK)
  {
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = &midi_handle;
  (void)USBH_memset(handle, 0, sizeof(*handle));
  phost->pActiveClass->pData = handle;
  handle->interface = interface;
  handle->InPipe = 0xFFU;
  handle->OutPipe = 0xFFU;

  USBH_InterfaceDescTypeDef *itf = &phost->device.CfgDesc.Itf_Desc[interface];
  uint8_t max_ep = (itf->bNumEndpoints <= USBH_MAX_NUM_ENDPOINTS)
                     ? itf->bNumEndpoints
                     : USBH_MAX_NUM_ENDPOINTS;

  for (uint8_t idx = 0U; idx < max_ep; idx++)
  {
    USBH_EpDescTypeDef *ep = &itf->Ep_Desc[idx];

    if ((ep->bmAttributes & USB_EP_TYPE_MASK) != USB_EP_TYPE_BULK)
    {
      continue;
    }

    if ((ep->bEndpointAddress & 0x80U) != 0U)
    {
      if (handle->InEp == 0U)
      {
        handle->InEp = ep->bEndpointAddress;
        handle->InEpSize = ep->wMaxPacketSize;
      }
    }
    else
    {
      if (handle->OutEp == 0U)
      {
        handle->OutEp = ep->bEndpointAddress;
        handle->OutEpSize = ep->wMaxPacketSize;
      }
    }
  }

  if ((handle->InEp == 0U) || (handle->OutEp == 0U))
  {
    return USBH_FAIL;
  }

  if ((handle->InEpSize == 0U) || (handle->OutEpSize == 0U))
  {
    return USBH_FAIL;
  }

  handle->InPipe = USBH_AllocPipe(phost, handle->InEp);
  handle->OutPipe = USBH_AllocPipe(phost, handle->OutEp);

  if ((handle->InPipe == 0xFFU) || (handle->OutPipe == 0xFFU))
  {
    return USBH_FAIL;
  }

  (void)USBH_OpenPipe(phost, handle->InPipe, handle->InEp,
                      phost->device.address, phost->device.speed,
                      USB_EP_TYPE_BULK, handle->InEpSize);
  (void)USBH_OpenPipe(phost, handle->OutPipe, handle->OutEp,
                      phost->device.address, phost->device.speed,
                      USB_EP_TYPE_BULK, handle->OutEpSize);
  (void)USBH_LL_SetToggle(phost, handle->InPipe, 0U);
  (void)USBH_LL_SetToggle(phost, handle->OutPipe, 0U);

  handle->rx_buffer_size = (handle->InEpSize <= USBH_MIDI_RX_BUF_SIZE)
                             ? handle->InEpSize
                             : USBH_MIDI_RX_BUF_SIZE;
  handle->state = USBH_MIDI_STATE_TRANSFER;

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
  if ((phost == NULL) || (phost->pActiveClass == NULL))
  {
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;

  if (handle == NULL)
  {
    return USBH_FAIL;
  }

  if (handle->InPipe != 0xFFU)
  {
    (void)USBH_ClosePipe(phost, handle->InPipe);
    (void)USBH_FreePipe(phost, handle->InPipe);
    handle->InPipe = 0xFFU;
  }

  if (handle->OutPipe != 0xFFU)
  {
    (void)USBH_ClosePipe(phost, handle->OutPipe);
    (void)USBH_FreePipe(phost, handle->OutPipe);
    handle->OutPipe = 0xFFU;
  }

  (void)USBH_memset(handle, 0, sizeof(*handle));
  phost->pActiveClass->pData = NULL;

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost)
{
  (void)phost;
  return USBH_OK;
}

static void USBH_MIDI_PushRx(USBH_MIDI_HandleTypeDef *handle,
                            const uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  if (handle->rx_count >= USBH_MIDI_RX_QUEUE_LEN)
  {
    return;
  }

  handle->rx_queue[handle->rx_head].data[0] = packet[0];
  handle->rx_queue[handle->rx_head].data[1] = packet[1];
  handle->rx_queue[handle->rx_head].data[2] = packet[2];
  handle->rx_queue[handle->rx_head].data[3] = packet[3];
  handle->rx_head = (uint16_t)((handle->rx_head + 1U) % USBH_MIDI_RX_QUEUE_LEN);
  handle->rx_count++;
}

static bool USBH_MIDI_PopTx(USBH_MIDI_HandleTypeDef *handle,
                           uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  if (handle->tx_count == 0U)
  {
    return false;
  }

  packet[0] = handle->tx_queue[handle->tx_tail].data[0];
  packet[1] = handle->tx_queue[handle->tx_tail].data[1];
  packet[2] = handle->tx_queue[handle->tx_tail].data[2];
  packet[3] = handle->tx_queue[handle->tx_tail].data[3];
  handle->tx_tail = (uint16_t)((handle->tx_tail + 1U) % USBH_MIDI_TX_QUEUE_LEN);
  handle->tx_count--;
  return true;
}

static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost)
{
  if ((phost == NULL) || (phost->pActiveClass == NULL))
  {
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;
  if ((handle == NULL) || (handle->state != USBH_MIDI_STATE_TRANSFER))
  {
    return USBH_OK;
  }

  if (!handle->rx_in_progress)
  {
    (void)USBH_BulkReceiveData(phost, handle->rx_buffer, handle->rx_buffer_size, handle->InPipe);
    handle->rx_in_progress = true;
  }

  if (handle->rx_in_progress)
  {
    USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->InPipe);

    if (urb_state == USBH_URB_DONE)
    {
      uint16_t received = USBH_LL_GetLastXferSize(phost, handle->InPipe);
      for (uint16_t offset = 0U; (offset + 3U) < received; offset += USBH_MIDI_PACKET_SIZE)
      {
        USBH_MIDI_PushRx(handle, &handle->rx_buffer[offset]);
      }
      handle->rx_in_progress = false;
    }
    else if ((urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
    {
      (void)USBH_ClrFeature(phost, handle->InEp);
      handle->rx_in_progress = false;
    }
  }

  if (handle->tx_in_progress)
  {
    USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->OutPipe);
    if ((urb_state == USBH_URB_DONE) || (urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
    {
      handle->tx_in_progress = false;
    }
  }

  if ((!handle->tx_in_progress) && (handle->tx_count > 0U))
  {
    uint8_t packet[USBH_MIDI_PACKET_SIZE];
    if (USBH_MIDI_PopTx(handle, packet))
    {
      (void)USBH_BulkSendData(phost, packet, USBH_MIDI_PACKET_SIZE, handle->OutPipe, 1U);
      handle->tx_in_progress = true;
    }
  }

  return USBH_OK;
}

USBH_StatusTypeDef USBH_MIDI_ReadPacket(USBH_HandleTypeDef *phost,
                                        uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  if ((phost == NULL) || (phost->pActiveClass == NULL) || (phost->pActiveClass != &USBH_MIDI_Class))
  {
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;

  if ((handle == NULL) || (handle->rx_count == 0U))
  {
    return USBH_BUSY;
  }

  packet[0] = handle->rx_queue[handle->rx_tail].data[0];
  packet[1] = handle->rx_queue[handle->rx_tail].data[1];
  packet[2] = handle->rx_queue[handle->rx_tail].data[2];
  packet[3] = handle->rx_queue[handle->rx_tail].data[3];
  handle->rx_tail = (uint16_t)((handle->rx_tail + 1U) % USBH_MIDI_RX_QUEUE_LEN);
  handle->rx_count--;

  return USBH_OK;
}

USBH_StatusTypeDef USBH_MIDI_Transmit(USBH_HandleTypeDef *phost,
                                      const uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  if ((phost == NULL) || (phost->pActiveClass == NULL) || (phost->pActiveClass != &USBH_MIDI_Class))
  {
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;

  if ((handle == NULL) || (handle->tx_count >= USBH_MIDI_TX_QUEUE_LEN))
  {
    return USBH_BUSY;
  }

  handle->tx_queue[handle->tx_head].data[0] = packet[0];
  handle->tx_queue[handle->tx_head].data[1] = packet[1];
  handle->tx_queue[handle->tx_head].data[2] = packet[2];
  handle->tx_queue[handle->tx_head].data[3] = packet[3];
  handle->tx_head = (uint16_t)((handle->tx_head + 1U) % USBH_MIDI_TX_QUEUE_LEN);
  handle->tx_count++;

  return USBH_OK;
}

bool USBH_MIDI_IsReady(USBH_HandleTypeDef *phost)
{
  if ((phost == NULL) || (phost->pActiveClass == NULL) || (phost->pActiveClass != &USBH_MIDI_Class))
  {
    return false;
  }

  USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;
  return (handle != NULL) && (handle->state == USBH_MIDI_STATE_TRANSFER);
}
