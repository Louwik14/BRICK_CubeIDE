/**
  ******************************************************************************
  * @file    usbh_midi.c
  * @brief   USB Host MIDI class driver (STM32Cube HAL)
  ******************************************************************************
  */

#include "usbh_midi.h"
#include "midi_host.h"

#ifndef USB_EP_TYPE_MASK
#define USB_EP_TYPE_MASK 0x03U
#endif

#define USBH_MIDI_ALIGN_32 __attribute__((aligned(32)))

typedef enum {
  USBH_MIDI_STATE_IDLE = 0U,
  USBH_MIDI_STATE_TRANSFER,
  USBH_MIDI_STATE_ERROR
} USBH_MIDI_StateTypeDef;

typedef enum {
  USBH_MIDI_RX_IDLE = 0U,
  USBH_MIDI_RX_RECEIVE,
  USBH_MIDI_RX_POLL,
  USBH_MIDI_RX_DONE,
  USBH_MIDI_RX_DISPATCH
} USBH_MIDI_RxStateTypeDef;

typedef enum {
  USBH_MIDI_TX_IDLE = 0U,
  USBH_MIDI_TX_SEND,
  USBH_MIDI_TX_POLL,
  USBH_MIDI_TX_DONE
} USBH_MIDI_TxStateTypeDef;

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
  USBH_MIDI_RxStateTypeDef rx_state;
  USBH_MIDI_TxStateTypeDef tx_state;
  uint16_t rx_buffer_size;
  uint16_t rx_last_count;
  bool rx_busy;
  bool tx_busy;
  uint8_t rx_buffer[USBH_MIDI_RX_BUF_SIZE] USBH_MIDI_ALIGN_32;
  uint8_t tx_buffer[USBH_MIDI_TX_BUF_SIZE] USBH_MIDI_ALIGN_32;
  USBH_MIDI_PacketTypeDef rx_queue[USBH_MIDI_RX_QUEUE_LEN] USBH_MIDI_ALIGN_32;
  uint16_t rx_head;
  uint16_t rx_tail;
  uint16_t rx_count;
  USBH_MIDI_PacketTypeDef tx_queue[USBH_MIDI_TX_QUEUE_LEN] USBH_MIDI_ALIGN_32;
  uint16_t tx_head;
  uint16_t tx_tail;
  uint16_t tx_count;
} USBH_MIDI_HandleTypeDef;

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost)
{
    (void)phost;
    return USBH_OK;
}

static USBH_MIDI_HandleTypeDef midi_handle;

USBH_ClassTypeDef USBH_MIDI_Class = {
  "MIDI",
  USBH_MIDI_CLASS,
  USBH_MIDI_InterfaceInit,
  USBH_MIDI_InterfaceDeInit,
  USBH_MIDI_ClassRequest,
  USBH_MIDI_Process,
  USBH_MIDI_SOFProcess,   // <-- IMPORTANT
  NULL,
};


static void USBH_MIDI_ResetHandle(USBH_MIDI_HandleTypeDef *handle)
{
  (void)USBH_memset(handle, 0, sizeof(*handle));
  handle->InPipe = 0xFFU;
  handle->OutPipe = 0xFFU;
  handle->rx_state = USBH_MIDI_RX_IDLE;
  handle->tx_state = USBH_MIDI_TX_IDLE;
}

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost)
{
  USBH_UsrLog("USBH_MIDI_InterfaceInit");

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
    USBH_ErrLog("USBH_MIDI_InterfaceInit: no MIDI interface");
    return USBH_FAIL;
  }

  if (USBH_SelectInterface(phost, interface) != USBH_OK)
  {
    USBH_ErrLog("USBH_MIDI_InterfaceInit: select interface failed");
    return USBH_FAIL;
  }

  USBH_MIDI_HandleTypeDef *handle = &midi_handle;
  USBH_MIDI_ResetHandle(handle);
  phost->pActiveClass->pData = handle;
  handle->interface = interface;

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
        handle->InEpSize = (ep->wMaxPacketSize <= USBH_MIDI_RX_BUF_SIZE)
                             ? ep->wMaxPacketSize
                             : USBH_MIDI_RX_BUF_SIZE;
      }
    }
    else
    {
      if (handle->OutEp == 0U)
      {
        handle->OutEp = ep->bEndpointAddress;
        handle->OutEpSize = (ep->wMaxPacketSize <= USBH_MIDI_TX_BUF_SIZE)
                              ? ep->wMaxPacketSize
                              : USBH_MIDI_TX_BUF_SIZE;
      }
    }
  }

  if ((handle->InEp == 0U) || (handle->OutEp == 0U))
  {
    USBH_ErrLog("USBH_MIDI_InterfaceInit: missing endpoints");
    return USBH_FAIL;
  }

  if ((handle->InEpSize == 0U) || (handle->OutEpSize == 0U))
  {
    USBH_ErrLog("USBH_MIDI_InterfaceInit: invalid endpoint sizes");
    return USBH_FAIL;
  }

  handle->InPipe = USBH_AllocPipe(phost, handle->InEp);
  handle->OutPipe = USBH_AllocPipe(phost, handle->OutEp);

  if ((handle->InPipe == 0xFFU) || (handle->OutPipe == 0xFFU))
  {
    USBH_ErrLog("USBH_MIDI_InterfaceInit: pipe alloc failed");
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

  handle->rx_buffer_size = handle->InEpSize;
  handle->state = USBH_MIDI_STATE_TRANSFER;
  handle->rx_state = USBH_MIDI_RX_RECEIVE;
  handle->tx_state = USBH_MIDI_TX_IDLE;
  handle->rx_busy = false;
  handle->tx_busy = false;

  USBH_UsrLog("USBH_MIDI_InterfaceInit: IN=0x%02X OUT=0x%02X", handle->InEp, handle->OutEp);

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
  USBH_UsrLog("USBH_MIDI_InterfaceDeInit (disconnect)");

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

  midi_host_on_disconnect();
  USBH_MIDI_ResetHandle(handle);
  phost->pActiveClass->pData = NULL;

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost)
{
  (void)phost;
  USBH_UsrLog("USBH_MIDI_ClassRequest");
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

static void USBH_MIDI_ProcessRx(USBH_HandleTypeDef *phost, USBH_MIDI_HandleTypeDef *handle)
{
  switch (handle->rx_state)
  {
    case USBH_MIDI_RX_IDLE:
      handle->rx_state = USBH_MIDI_RX_RECEIVE;
      break;

    case USBH_MIDI_RX_RECEIVE:
      if (!handle->rx_busy)
      {
        (void)USBH_BulkReceiveData(phost, handle->rx_buffer, handle->rx_buffer_size, handle->InPipe);
        handle->rx_busy = true;
        handle->rx_state = USBH_MIDI_RX_POLL;
        USBH_UsrLog("USBH_MIDI_RX_RECEIVE -> RX_POLL (arm)");
      }
      break;

    case USBH_MIDI_RX_POLL:
    {
      USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->InPipe);
      if (urb_state == USBH_URB_DONE)
      {
        handle->rx_last_count = USBH_LL_GetLastXferSize(phost, handle->InPipe);
        handle->rx_busy = false;
        handle->rx_state = USBH_MIDI_RX_DONE;
        USBH_UsrLog("USBH_MIDI_RX_DONE: %u bytes", (unsigned int)handle->rx_last_count);
      }
      else if (urb_state == USBH_URB_NOTREADY)
      {
        /* Stay in POLL until data is ready. */
      }
      else if ((urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
      {
        USBH_ErrLog("USBH_MIDI_RX_URB_ERROR: %u", (unsigned int)urb_state);
        (void)USBH_ClrFeature(phost, handle->InEp);
        handle->rx_busy = false;
        handle->rx_state = USBH_MIDI_RX_RECEIVE;
      }
      break;
    }

    case USBH_MIDI_RX_DONE:
      handle->rx_state = USBH_MIDI_RX_DISPATCH;
      USBH_UsrLog("USBH_MIDI_RX_DONE -> RX_DISPATCH");
      break;

    case USBH_MIDI_RX_DISPATCH:
      for (uint16_t offset = 0U;
           (offset + (USBH_MIDI_PACKET_SIZE - 1U)) < handle->rx_last_count;
           offset = (uint16_t)(offset + USBH_MIDI_PACKET_SIZE))
      {
        USBH_MIDI_PushRx(handle, &handle->rx_buffer[offset]);
      }
      handle->rx_state = USBH_MIDI_RX_RECEIVE;
      break;

    default:
      handle->rx_state = USBH_MIDI_RX_RECEIVE;
      break;
  }
}

static void USBH_MIDI_ProcessTx(USBH_HandleTypeDef *phost, USBH_MIDI_HandleTypeDef *handle)
{
  switch (handle->tx_state)
  {
    case USBH_MIDI_TX_IDLE:
      if (!handle->tx_busy && (handle->tx_count > 0U))
      {
        if (USBH_MIDI_PopTx(handle, handle->tx_buffer))
        {
          (void)USBH_BulkSendData(phost, handle->tx_buffer, USBH_MIDI_PACKET_SIZE,
                                  handle->OutPipe, 1U);
          handle->tx_busy = true;
          handle->tx_state = USBH_MIDI_TX_POLL;
          USBH_UsrLog("USBH_MIDI_TX_IDLE -> TX_SEND");
        }
      }
      break;

    case USBH_MIDI_TX_POLL:
    {
      USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->OutPipe);
      if (urb_state == USBH_URB_DONE)
      {
        handle->tx_busy = false;
        handle->tx_state = USBH_MIDI_TX_DONE;
        USBH_UsrLog("USBH_MIDI_TX_POLL -> TX_DONE");
      }
      else if (urb_state == USBH_URB_NOTREADY)
      {
        /* Stay in POLL until data is sent. */
      }
      else if ((urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
      {
        USBH_ErrLog("USBH_MIDI_TX_URB_ERROR: %u", (unsigned int)urb_state);
        (void)USBH_ClrFeature(phost, handle->OutEp);
        handle->tx_busy = false;
        handle->tx_state = USBH_MIDI_TX_DONE;
      }
      break;
    }

    case USBH_MIDI_TX_DONE:
      handle->tx_state = USBH_MIDI_TX_IDLE;
      break;

    default:
      handle->tx_state = USBH_MIDI_TX_IDLE;
      break;
  }
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

  USBH_MIDI_ProcessRx(phost, handle);
  USBH_MIDI_ProcessTx(phost, handle);

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
