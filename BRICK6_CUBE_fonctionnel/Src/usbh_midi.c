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

#ifndef USBH_MIDI_DEBUG
#define USBH_MIDI_DEBUG 1
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

typedef struct {
  uint32_t process_calls;
  uint32_t rx_process_calls;
  uint32_t tx_process_calls;
  uint32_t bulk_rx_arms;
  uint32_t bulk_rx_done;
  uint32_t bulk_rx_notready;
  uint32_t bulk_rx_error;
  uint32_t bulk_tx_arms;
  uint32_t bulk_tx_done;
  uint32_t bulk_tx_notready;
  uint32_t bulk_tx_error;
} usbh_midi_debug_stats_t;

static USBH_StatusTypeDef USBH_MIDI_InterfaceInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_ClassRequest(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_Process(USBH_HandleTypeDef *phost);
static USBH_StatusTypeDef USBH_MIDI_SOFProcess(USBH_HandleTypeDef *phost)
{
  static uint32_t sof_calls = 0U;
  (void)phost;
  sof_calls++;
#if USBH_MIDI_DEBUG
  if ((sof_calls % 1000U) == 0U)
  {
    USBH_UsrLog("USBH_MIDI_SOFProcess: calls=%lu", (unsigned long)sof_calls);
  }
#endif
  return USBH_OK;
}

static USBH_MIDI_HandleTypeDef midi_handle;
static usbh_midi_debug_stats_t usbh_midi_debug_stats;
static USBH_URBStateTypeDef usbh_midi_last_urb_rx = USBH_URB_IDLE;
static USBH_URBStateTypeDef usbh_midi_last_urb_tx = USBH_URB_IDLE;
static uint32_t usbh_midi_rx_poll_calls = 0U;
static uint32_t usbh_midi_tx_poll_calls = 0U;

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
#if USBH_MIDI_DEBUG
  USBH_UsrLog("USBH_MIDI_InterfaceInit");
#endif
  (void)USBH_memset(&usbh_midi_debug_stats, 0, sizeof(usbh_midi_debug_stats));

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
#if USBH_MIDI_DEBUG
    USBH_ErrLog("USBH_MIDI_InterfaceInit: no MIDI interface");
#endif
    return USBH_FAIL;
  }

  if (USBH_SelectInterface(phost, interface) != USBH_OK)
  {
#if USBH_MIDI_DEBUG
    USBH_ErrLog("USBH_MIDI_InterfaceInit: select interface failed");
#endif
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
#if USBH_MIDI_DEBUG
    USBH_ErrLog("USBH_MIDI_InterfaceInit: missing endpoints");
#endif
    return USBH_FAIL;
  }

  if ((handle->InEpSize == 0U) || (handle->OutEpSize == 0U))
  {
#if USBH_MIDI_DEBUG
    USBH_ErrLog("USBH_MIDI_InterfaceInit: invalid endpoint sizes");
#endif
    return USBH_FAIL;
  }

  handle->InPipe = USBH_AllocPipe(phost, handle->InEp);
  handle->OutPipe = USBH_AllocPipe(phost, handle->OutEp);

  if ((handle->InPipe == 0xFFU) || (handle->OutPipe == 0xFFU))
  {
#if USBH_MIDI_DEBUG
    USBH_ErrLog("USBH_MIDI_InterfaceInit: pipe alloc failed");
#endif
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

#if USBH_MIDI_DEBUG
  USBH_UsrLog("USBH_MIDI_InterfaceInit: IN=0x%02X OUT=0x%02X", handle->InEp, handle->OutEp);
#endif

  return USBH_OK;
}

static USBH_StatusTypeDef USBH_MIDI_InterfaceDeInit(USBH_HandleTypeDef *phost)
{
#if USBH_MIDI_DEBUG
  USBH_UsrLog("USBH_MIDI_InterfaceDeInit (disconnect)");
#endif

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
#if USBH_MIDI_DEBUG
  USBH_UsrLog("USBH_MIDI_ClassRequest");
#endif
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
  USBH_MIDI_RxStateTypeDef prev_state = handle->rx_state;
  usbh_midi_debug_stats.rx_process_calls++;
  switch (handle->rx_state)
  {
    case USBH_MIDI_RX_IDLE:
      handle->rx_state = USBH_MIDI_RX_RECEIVE;
      break;

    case USBH_MIDI_RX_RECEIVE:
      if (!handle->rx_busy)
      {
        (void)USBH_BulkReceiveData(phost, handle->rx_buffer, handle->rx_buffer_size, handle->InPipe);
        usbh_midi_debug_stats.bulk_rx_arms++;
        handle->rx_busy = true;
        handle->rx_state = USBH_MIDI_RX_POLL;
#if USBH_MIDI_DEBUG
        USBH_UsrLog("USBH_MIDI_RX_RECEIVE -> RX_POLL (arm)");
#endif
      }
      break;

    case USBH_MIDI_RX_POLL:
    {
      USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->InPipe);
      usbh_midi_last_urb_rx = urb_state;
      usbh_midi_rx_poll_calls++;
#if USBH_MIDI_DEBUG
      if ((usbh_midi_rx_poll_calls % 1000U) == 0U)
      {
        USBH_UsrLog("USBH_MIDI_RX_URB: state=%u rx_state=%u rx_busy=%u rx_last=%u",
                    (unsigned int)urb_state,
                    (unsigned int)handle->rx_state,
                    (unsigned int)handle->rx_busy,
                    (unsigned int)handle->rx_last_count);
      }
#endif
      if (urb_state == USBH_URB_DONE)
      {
        usbh_midi_debug_stats.bulk_rx_done++;
        handle->rx_last_count = USBH_LL_GetLastXferSize(phost, handle->InPipe);
        handle->rx_busy = false;
        handle->rx_state = USBH_MIDI_RX_DONE;
#if USBH_MIDI_DEBUG
        USBH_UsrLog("USBH_MIDI_RX_DONE: %u bytes", (unsigned int)handle->rx_last_count);
#endif
      }
      else if (urb_state == USBH_URB_NOTREADY)
      {
        usbh_midi_debug_stats.bulk_rx_notready++;
        handle->rx_busy = false;
        handle->rx_state = USBH_MIDI_RX_RECEIVE;   // 🔴 RÉARMER !
      }

      else if ((urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
      {
        usbh_midi_debug_stats.bulk_rx_error++;
#if USBH_MIDI_DEBUG
        USBH_ErrLog("USBH_MIDI_RX_URB_ERROR: %u", (unsigned int)urb_state);
#endif
        (void)USBH_ClrFeature(phost, handle->InEp);
        handle->rx_busy = false;
        handle->rx_state = USBH_MIDI_RX_RECEIVE;
      }
      break;
    }

    case USBH_MIDI_RX_DONE:
      handle->rx_state = USBH_MIDI_RX_DISPATCH;
#if USBH_MIDI_DEBUG
      USBH_UsrLog("USBH_MIDI_RX_DONE -> RX_DISPATCH");
#endif
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
#if USBH_MIDI_DEBUG
  if (handle->rx_state != prev_state)
  {
    USBH_UsrLog("USBH_MIDI_RX_STATE: %u -> %u state=%u rx_busy=%u tx_busy=%u urb_rx=%u rx_last=%u rx_count=%u",
                (unsigned int)prev_state,
                (unsigned int)handle->rx_state,
                (unsigned int)handle->state,
                (unsigned int)handle->rx_busy,
                (unsigned int)handle->tx_busy,
                (unsigned int)usbh_midi_last_urb_rx,
                (unsigned int)handle->rx_last_count,
                (unsigned int)handle->rx_count);
  }
#endif
}

static void USBH_MIDI_ProcessTx(USBH_HandleTypeDef *phost, USBH_MIDI_HandleTypeDef *handle)
{
  USBH_MIDI_TxStateTypeDef prev_state = handle->tx_state;
  usbh_midi_debug_stats.tx_process_calls++;
  switch (handle->tx_state)
  {
    case USBH_MIDI_TX_IDLE:
      if (!handle->tx_busy && (handle->tx_count > 0U))
      {
        if (USBH_MIDI_PopTx(handle, handle->tx_buffer))
        {
          (void)USBH_BulkSendData(phost, handle->tx_buffer, USBH_MIDI_PACKET_SIZE,
                                  handle->OutPipe, 1U);
          usbh_midi_debug_stats.bulk_tx_arms++;
          handle->tx_busy = true;
          handle->tx_state = USBH_MIDI_TX_POLL;
#if USBH_MIDI_DEBUG
          USBH_UsrLog("USBH_MIDI_TX_IDLE -> TX_SEND");
#endif
        }
      }
      break;

    case USBH_MIDI_TX_POLL:
    {
      USBH_URBStateTypeDef urb_state = USBH_LL_GetURBState(phost, handle->OutPipe);
      usbh_midi_last_urb_tx = urb_state;
      usbh_midi_tx_poll_calls++;
#if USBH_MIDI_DEBUG
      if ((usbh_midi_tx_poll_calls % 1000U) == 0U)
      {
        USBH_UsrLog("USBH_MIDI_TX_URB: state=%u tx_state=%u tx_busy=%u tx_count=%u",
                    (unsigned int)urb_state,
                    (unsigned int)handle->tx_state,
                    (unsigned int)handle->tx_busy,
                    (unsigned int)handle->tx_count);
      }
#endif
      if (urb_state == USBH_URB_DONE)
      {
        usbh_midi_debug_stats.bulk_tx_done++;
        handle->tx_busy = false;
        handle->tx_state = USBH_MIDI_TX_DONE;
#if USBH_MIDI_DEBUG
        USBH_UsrLog("USBH_MIDI_TX_POLL -> TX_DONE");
#endif
      }
      else if (urb_state == USBH_URB_NOTREADY)
      {
        usbh_midi_debug_stats.bulk_tx_notready++;
        handle->tx_busy = false;
        handle->tx_state = USBH_MIDI_TX_IDLE;   // 🔴 Réessayer plus tard
      }

      else if ((urb_state == USBH_URB_ERROR) || (urb_state == USBH_URB_STALL))
      {
        usbh_midi_debug_stats.bulk_tx_error++;
#if USBH_MIDI_DEBUG
        USBH_ErrLog("USBH_MIDI_TX_URB_ERROR: %u", (unsigned int)urb_state);
#endif
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
#if USBH_MIDI_DEBUG
  if (handle->tx_state != prev_state)
  {
    USBH_UsrLog("USBH_MIDI_TX_STATE: %u -> %u state=%u rx_busy=%u tx_busy=%u urb_tx=%u tx_count=%u",
                (unsigned int)prev_state,
                (unsigned int)handle->tx_state,
                (unsigned int)handle->state,
                (unsigned int)handle->rx_busy,
                (unsigned int)handle->tx_busy,
                (unsigned int)usbh_midi_last_urb_tx,
                (unsigned int)handle->tx_count);
  }
#endif
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

  usbh_midi_debug_stats.process_calls++;
#if USBH_MIDI_DEBUG
  if ((usbh_midi_debug_stats.process_calls % 1000U) == 0U)
  {
    USBH_UsrLog("USBH_MIDI_Process: state=%u rx_state=%u tx_state=%u rx_busy=%u tx_busy=%u urb_rx=%u urb_tx=%u rx_count=%u tx_count=%u",
                (unsigned int)handle->state,
                (unsigned int)handle->rx_state,
                (unsigned int)handle->tx_state,
                (unsigned int)handle->rx_busy,
                (unsigned int)handle->tx_busy,
                (unsigned int)usbh_midi_last_urb_rx,
                (unsigned int)usbh_midi_last_urb_tx,
                (unsigned int)handle->rx_count,
                (unsigned int)handle->tx_count);
  }
#endif
  USBH_MIDI_ProcessRx(phost, handle);
  USBH_MIDI_ProcessTx(phost, handle);

  return USBH_OK;
}

USBH_StatusTypeDef USBH_MIDI_ReadPacket(USBH_HandleTypeDef *phost,
                                        uint8_t packet[USBH_MIDI_PACKET_SIZE])
{
  static USBH_StatusTypeDef last_status = USBH_FAIL;
  static uint32_t read_calls = 0U;
  USBH_StatusTypeDef status = USBH_FAIL;
  if ((phost == NULL) || (phost->pActiveClass == NULL) || (phost->pActiveClass != &USBH_MIDI_Class))
  {
    status = USBH_FAIL;
  }
  else
  {
    USBH_MIDI_HandleTypeDef *handle = (USBH_MIDI_HandleTypeDef *)phost->pActiveClass->pData;

    if ((handle == NULL) || (handle->rx_count == 0U))
    {
      status = USBH_BUSY;
    }
    else
    {
      packet[0] = handle->rx_queue[handle->rx_tail].data[0];
      packet[1] = handle->rx_queue[handle->rx_tail].data[1];
      packet[2] = handle->rx_queue[handle->rx_tail].data[2];
      packet[3] = handle->rx_queue[handle->rx_tail].data[3];
      handle->rx_tail = (uint16_t)((handle->rx_tail + 1U) % USBH_MIDI_RX_QUEUE_LEN);
      handle->rx_count--;

      status = USBH_OK;
    }
  }

  read_calls++;
#if USBH_MIDI_DEBUG
  if ((status != last_status) || ((read_calls % 1000U) == 0U))
  {
    USBH_UsrLog("USBH_MIDI_ReadPacket: status=%u", (unsigned int)status);
  }
#endif
  last_status = status;
  return status;
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

void USBH_MIDI_DebugDump(void)
{
#if USBH_MIDI_DEBUG
  USBH_MIDI_HandleTypeDef *handle = &midi_handle;
  USBH_UsrLog("USBH_MIDI_DebugDump");
  USBH_UsrLog("  process_calls=%lu rx_process_calls=%lu tx_process_calls=%lu",
              (unsigned long)usbh_midi_debug_stats.process_calls,
              (unsigned long)usbh_midi_debug_stats.rx_process_calls,
              (unsigned long)usbh_midi_debug_stats.tx_process_calls);
  USBH_UsrLog("  bulk_rx_arms=%lu bulk_rx_done=%lu bulk_rx_notready=%lu bulk_rx_error=%lu",
              (unsigned long)usbh_midi_debug_stats.bulk_rx_arms,
              (unsigned long)usbh_midi_debug_stats.bulk_rx_done,
              (unsigned long)usbh_midi_debug_stats.bulk_rx_notready,
              (unsigned long)usbh_midi_debug_stats.bulk_rx_error);
  USBH_UsrLog("  bulk_tx_arms=%lu bulk_tx_done=%lu bulk_tx_notready=%lu bulk_tx_error=%lu",
              (unsigned long)usbh_midi_debug_stats.bulk_tx_arms,
              (unsigned long)usbh_midi_debug_stats.bulk_tx_done,
              (unsigned long)usbh_midi_debug_stats.bulk_tx_notready,
              (unsigned long)usbh_midi_debug_stats.bulk_tx_error);
  USBH_UsrLog("  state=%u rx_state=%u tx_state=%u rx_busy=%u tx_busy=%u urb_rx=%u urb_tx=%u",
              (unsigned int)handle->state,
              (unsigned int)handle->rx_state,
              (unsigned int)handle->tx_state,
              (unsigned int)handle->rx_busy,
              (unsigned int)handle->tx_busy,
              (unsigned int)usbh_midi_last_urb_rx,
              (unsigned int)usbh_midi_last_urb_tx);
  USBH_UsrLog("  rx_count=%u tx_count=%u rx_last_count=%u rx_buf_size=%u",
              (unsigned int)handle->rx_count,
              (unsigned int)handle->tx_count,
              (unsigned int)handle->rx_last_count,
              (unsigned int)handle->rx_buffer_size);
#endif
}
