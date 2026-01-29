#include "usbd_brick6_composite.h"

#include "usbd_ctlreq.h"
#include "usbd_desc.h"
#include "usbd_midi.h"
#include "usbd_conf.h"
#include "audio/usb_audio_backend.h"

#define BRICK6_AUDIO_CHANNELS           2U
#define BRICK6_AUDIO_SAMPLES_PER_PACKET (BRICK6_AUDIO_FRAMES_PER_PACKET * BRICK6_AUDIO_CHANNELS)

static uint8_t USBD_BRICK6_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BRICK6_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BRICK6_COMPOSITE_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t *USBD_BRICK6_COMPOSITE_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length);
static uint8_t USBD_BRICK6_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_BRICK6_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);

static void BRICK6_Audio_QueuePacket(USBD_HandleTypeDef *pdev);

__ALIGN_BEGIN static uint8_t usb_rx_buffer[MIDI_EPOUT_SIZE] __ALIGN_END;
__ALIGN_BEGIN static int32_t audio_tx_frames[BRICK6_AUDIO_SAMPLES_PER_PACKET] __ALIGN_END;

static uint8_t brick6_audio_alt_setting = 0U;
static uint8_t brick6_audio_ep_opened = 0U;

USBD_ClassTypeDef USBD_BRICK6_COMPOSITE =
{
  USBD_BRICK6_COMPOSITE_Init,
  USBD_BRICK6_COMPOSITE_DeInit,
  USBD_BRICK6_COMPOSITE_Setup,
  NULL, /* EP0_TxSent */
  NULL, /* EP0_RxReady */
  USBD_BRICK6_COMPOSITE_DataIn,
  USBD_BRICK6_COMPOSITE_DataOut,
  NULL, /* SOF */
  NULL,
  NULL,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc,
};

__ALIGN_BEGIN static uint8_t USBD_BRICK6_COMPOSITE_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  USB_MAX_EP0_SIZE,
  0x01,
  0x00,
};

static uint8_t USBD_BRICK6_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  uint8_t ret = USBD_OK;

  UNUSED(cfgidx);

  USBD_LL_OpenEP(pdev, MIDI_EPIN_ADDR, USBD_EP_TYPE_BULK, MIDI_EPIN_SIZE);
  USBD_LL_OpenEP(pdev, MIDI_EPOUT_ADDR, USBD_EP_TYPE_BULK, MIDI_EPOUT_SIZE);

  USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, usb_rx_buffer, MIDI_EPOUT_SIZE);

  pdev->pClassData = USBD_malloc(sizeof(USBD_MIDI_HandleTypeDef));
  if (pdev->pClassData == NULL)
  {
    ret = USBD_FAIL;
  }
  else
  {
    USBD_MIDI_HandleTypeDef *hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;
    hmidi->state = MIDI_IDLE;
    hmidi->AltSetting = 0U;
  }

  brick6_audio_alt_setting = 0U;
  brick6_audio_ep_opened = 0U;
  usb_audio_backend_init();

  return ret;
}

static uint8_t USBD_BRICK6_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  USBD_LL_CloseEP(pdev, MIDI_EPIN_ADDR);
  USBD_LL_CloseEP(pdev, MIDI_EPOUT_ADDR);

  if (brick6_audio_ep_opened != 0U)
  {
    USBD_LL_CloseEP(pdev, BRICK6_AUDIO_EP_IN_ADDR);
    brick6_audio_ep_opened = 0U;
  }

  if (pdev->pClassData != NULL)
  {
    USBD_free(pdev->pClassData);
    pdev->pClassData = NULL;
  }

  return USBD_OK;
}

static uint8_t USBD_BRICK6_COMPOSITE_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  uint16_t len = 0U;
  uint8_t *pbuf = NULL;
  USBD_MIDI_HandleTypeDef *hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_CLASS:
      switch (req->bRequest)
      {
        case MIDI_REQ_SET_PROTOCOL:
          hmidi->Protocol = (uint8_t)(req->wValue);
          break;
        case MIDI_REQ_GET_PROTOCOL:
          USBD_CtlSendData(pdev, (uint8_t *)&hmidi->Protocol, 1U);
          break;
        case MIDI_REQ_SET_IDLE:
          hmidi->IdleState = (uint8_t)(req->wValue >> 8);
          break;
        case MIDI_REQ_GET_IDLE:
          USBD_CtlSendData(pdev, (uint8_t *)&hmidi->IdleState, 1U);
          break;
        default:
          USBD_CtlError(pdev, req);
          return USBD_FAIL;
      }
      break;

    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_DESCRIPTOR:
          if ((req->wValue >> 8) == MIDI_DESCRIPTOR_TYPE)
          {
            pbuf = USBD_Brick6_Composite_CfgDesc + BRICK6_MIDI_CLASS_DESC_OFFSET;
            len = MIN(USB_MIDI_DESC_SIZE, req->wLength);
            USBD_CtlSendData(pdev, pbuf, len);
          }
          break;

        case USB_REQ_GET_INTERFACE:
          if (req->wIndex == BRICK6_AUDIO_STREAMING_IF)
          {
            USBD_CtlSendData(pdev, &brick6_audio_alt_setting, 1U);
          }
          else if (req->wIndex == BRICK6_MIDI_STREAMING_IF)
          {
            USBD_CtlSendData(pdev, (uint8_t *)&hmidi->AltSetting, 1U);
          }
          else
          {
            USBD_CtlSendData(pdev, &brick6_audio_alt_setting, 1U);
          }
          break;

        case USB_REQ_SET_INTERFACE:
          if (req->wIndex == BRICK6_AUDIO_STREAMING_IF)
          {
            brick6_audio_alt_setting = (uint8_t)(req->wValue);
            if ((brick6_audio_alt_setting == 1U) && (brick6_audio_ep_opened == 0U))
            {
              USBD_LL_OpenEP(pdev, BRICK6_AUDIO_EP_IN_ADDR, USBD_EP_TYPE_ISOC, BRICK6_AUDIO_EP_IN_SIZE);
              brick6_audio_ep_opened = 1U;
              BRICK6_Audio_QueuePacket(pdev);
            }
            else if ((brick6_audio_alt_setting == 0U) && (brick6_audio_ep_opened != 0U))
            {
              USBD_LL_CloseEP(pdev, BRICK6_AUDIO_EP_IN_ADDR);
              brick6_audio_ep_opened = 0U;
            }
          }
          else if (req->wIndex == BRICK6_MIDI_STREAMING_IF)
          {
            hmidi->AltSetting = (uint8_t)(req->wValue);
          }
          break;

        default:
          break;
      }
      break;

    default:
      break;
  }

  return USBD_OK;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetCfgDesc(uint16_t *length)
{
  *length = BRICK6_COMPOSITE_CONFIG_DESC_SIZE;
  return USBD_Brick6_Composite_CfgDesc;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = sizeof(USBD_BRICK6_COMPOSITE_DeviceQualifierDesc);
  return USBD_BRICK6_COMPOSITE_DeviceQualifierDesc;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  if (epnum == (MIDI_EPIN_ADDR & 0x0FU))
  {
    ((USBD_MIDI_HandleTypeDef *)pdev->pClassData)->state = MIDI_IDLE;
    USBD_MIDI_OnPacketsSent();
    return USBD_OK;
  }

  if (epnum == (BRICK6_AUDIO_EP_IN_ADDR & 0x0FU))
  {
    if (brick6_audio_ep_opened != 0U)
    {
      BRICK6_Audio_QueuePacket(pdev);
    }
    return USBD_OK;
  }

  return USBD_FAIL;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint8_t len;

  if (epnum != (MIDI_EPOUT_ADDR & 0x0FU))
  {
    return USBD_FAIL;
  }

  len = (uint8_t)HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef *)pdev->pData, epnum);
  USBD_MIDI_OnPacketsReceived(usb_rx_buffer, len);
  USBD_LL_PrepareReceive(pdev, MIDI_EPOUT_ADDR, usb_rx_buffer, MIDI_EPOUT_SIZE);

  return USBD_OK;
}

static void BRICK6_Audio_QueuePacket(USBD_HandleTypeDef *pdev)
{
  uint32_t frames;
  uint32_t fill_start;
  uint32_t idx;

  frames = usb_audio_backend_pop_frames(audio_tx_frames, BRICK6_AUDIO_FRAMES_PER_PACKET);
  if (frames < BRICK6_AUDIO_FRAMES_PER_PACKET)
  {
    fill_start = frames * BRICK6_AUDIO_CHANNELS;
    for (idx = fill_start; idx < BRICK6_AUDIO_SAMPLES_PER_PACKET; idx++)
    {
      audio_tx_frames[idx] = 0;
    }
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    USBD_LL_Transmit(pdev, BRICK6_AUDIO_EP_IN_ADDR, (uint8_t *)audio_tx_frames, BRICK6_AUDIO_EP_IN_SIZE);
  }
}
