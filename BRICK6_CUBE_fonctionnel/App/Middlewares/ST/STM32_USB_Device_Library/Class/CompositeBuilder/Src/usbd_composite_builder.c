#include "usbd_composite_builder.h"
#include "usbd_audio.h"
#include "usbd_midi.h"
#include <string.h>

extern uint8_t USBD_AUDIO_CfgDesc[USB_AUDIO_CONFIG_DESC_SIZ];
extern uint8_t USBD_MIDI_CfgDesc[USB_MIDI_CONFIG_DESC_SIZE];

#define USB_COMPOSITE_CONFIG_DESC_SIZ (USB_LEN_CFG_DESC + (USB_AUDIO_CONFIG_DESC_SIZ - USB_LEN_CFG_DESC) + (USB_MIDI_CONFIG_DESC_SIZE - USB_LEN_CFG_DESC))

static uint8_t USBD_Composite_CfgDesc[USB_COMPOSITE_CONFIG_DESC_SIZ];

__ALIGN_BEGIN static uint8_t USBD_Composite_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC,
  USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00,
  0x02,
  0x00,
  0x00,
  0x00,
  0x40,
  0x01,
  0x00,
};

static void USBD_CMPSIT_BuildConfigDesc(void)
{
  uint16_t total_length = USB_COMPOSITE_CONFIG_DESC_SIZ;
  uint16_t offset = USB_LEN_CFG_DESC;

  USBD_Composite_CfgDesc[0] = USB_LEN_CFG_DESC;
  USBD_Composite_CfgDesc[1] = USB_DESC_TYPE_CONFIGURATION;
  USBD_Composite_CfgDesc[2] = LOBYTE(total_length);
  USBD_Composite_CfgDesc[3] = HIBYTE(total_length);
  USBD_Composite_CfgDesc[4] = 0x03; /* bNumInterfaces */
  USBD_Composite_CfgDesc[5] = 0x01;
  USBD_Composite_CfgDesc[6] = 0x00;
#if (USBD_SELF_POWERED == 1U)
  USBD_Composite_CfgDesc[7] = 0xC0;
#else
  USBD_Composite_CfgDesc[7] = 0x80;
#endif
  USBD_Composite_CfgDesc[8] = USBD_MAX_POWER;

  memcpy(&USBD_Composite_CfgDesc[offset],
         &USBD_AUDIO_CfgDesc[USB_LEN_CFG_DESC],
         USB_AUDIO_CONFIG_DESC_SIZ - USB_LEN_CFG_DESC);
  offset += USB_AUDIO_CONFIG_DESC_SIZ - USB_LEN_CFG_DESC;

  memcpy(&USBD_Composite_CfgDesc[offset],
         &USBD_MIDI_CfgDesc[USB_LEN_CFG_DESC],
         USB_MIDI_CONFIG_DESC_SIZE - USB_LEN_CFG_DESC);
}

static uint8_t *USBD_CMPSIT_GetFSConfigDesc(uint16_t *length)
{
  *length = sizeof(USBD_Composite_CfgDesc);
  return USBD_Composite_CfgDesc;
}

static uint8_t *USBD_CMPSIT_GetHSConfigDesc(uint16_t *length)
{
  return USBD_CMPSIT_GetFSConfigDesc(length);
}

static uint8_t *USBD_CMPSIT_GetOtherSpeedConfigDesc(uint16_t *length)
{
  return USBD_CMPSIT_GetFSConfigDesc(length);
}

static uint8_t *USBD_CMPSIT_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = sizeof(USBD_Composite_DeviceQualifierDesc);
  return USBD_Composite_DeviceQualifierDesc;
}

USBD_CompositeTypeDef USBD_CMPSIT =
{
  USBD_CMPSIT_GetFSConfigDesc,
  USBD_CMPSIT_GetHSConfigDesc,
  USBD_CMPSIT_GetOtherSpeedConfigDesc,
  USBD_CMPSIT_GetDeviceQualifierDesc,
};

uint8_t USBD_CMPSIT_AddClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass,
                             USBD_CompositeClassTypeDef classtype, uint8_t cfgidx)
{
  USBD_CompositeElementTypeDef *class_entry;
  UNUSED(pclass);
  UNUSED(cfgidx);

  if (pdev == NULL)
  {
    return 1U;
  }

  class_entry = &pdev->tclasslist[pdev->classId];
  class_entry->ClassType = classtype;
  class_entry->ClassId = pdev->classId;
  class_entry->Active = 1U;
  class_entry->NumEps = 0U;
  class_entry->NumIf = 0U;

  switch (classtype)
  {
    case CLASS_TYPE_AUDIO:
      class_entry->NumEps = 1U;
      class_entry->Eps[0].add = AUDIO_OUT_EP;
      class_entry->Eps[0].type = USBD_EP_TYPE_ISOC;
      class_entry->Eps[0].size = AUDIO_OUT_PACKET;
      class_entry->Eps[0].is_used = 1U;
      class_entry->NumIf = 2U;
      class_entry->Ifs[0] = 0U;
      class_entry->Ifs[1] = 1U;
      break;
    case CLASS_TYPE_MIDI:
      class_entry->NumEps = 2U;
      class_entry->Eps[0].add = MIDI_EPOUT_ADDR;
      class_entry->Eps[0].type = USBD_EP_TYPE_BULK;
      class_entry->Eps[0].size = MIDI_EPOUT_SIZE;
      class_entry->Eps[0].is_used = 1U;
      class_entry->Eps[1].add = MIDI_EPIN_ADDR;
      class_entry->Eps[1].type = USBD_EP_TYPE_BULK;
      class_entry->Eps[1].size = MIDI_EPIN_SIZE;
      class_entry->Eps[1].is_used = 1U;
      class_entry->NumIf = 1U;
      class_entry->Ifs[0] = 2U;
      break;
    default:
      break;
  }

  USBD_CMPSIT_BuildConfigDesc();

  return 0U;
}

uint8_t USBD_CMPSIT_SetClassID(USBD_HandleTypeDef *pdev, USBD_CompositeClassTypeDef classtype,
                               uint8_t instance)
{
  uint8_t found = 0U;

  if (pdev == NULL)
  {
    return 0xFFU;
  }

  for (uint32_t idx = 0U; idx < USBD_MAX_SUPPORTED_CLASS; idx++)
  {
    if ((pdev->tclasslist[idx].Active != 0U) && (pdev->tclasslist[idx].ClassType == classtype))
    {
      if (found == instance)
      {
        pdev->classId = idx;
        return (uint8_t)idx;
      }
      found++;
    }
  }

  return 0xFFU;
}

void USBD_CMPST_ClearConfDesc(USBD_HandleTypeDef *pdev)
{
  UNUSED(pdev);
  memset(USBD_Composite_CfgDesc, 0, sizeof(USBD_Composite_CfgDesc));
}
