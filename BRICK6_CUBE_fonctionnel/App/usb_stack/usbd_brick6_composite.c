#include "usbd_brick6_composite.h"
#include "usbd_midi.h"

static uint8_t USBD_BRICK6_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BRICK6_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t USBD_BRICK6_COMPOSITE_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t *USBD_BRICK6_COMPOSITE_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length);
static uint8_t USBD_BRICK6_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t USBD_BRICK6_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);

USBD_ClassTypeDef USBD_BRICK6_COMPOSITE = {
  USBD_BRICK6_COMPOSITE_Init,
  USBD_BRICK6_COMPOSITE_DeInit,
  USBD_BRICK6_COMPOSITE_Setup,
  NULL,
  NULL,
  USBD_BRICK6_COMPOSITE_DataIn,
  USBD_BRICK6_COMPOSITE_DataOut,
  NULL,
  NULL,
  NULL,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetCfgDesc,
  USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc,
};

static uint8_t USBD_BRICK6_COMPOSITE_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  if (USBD_MIDI.Init != NULL)
  {
    return USBD_MIDI.Init(pdev, cfgidx);
  }
  return USBD_FAIL;
}

static uint8_t USBD_BRICK6_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  if (USBD_MIDI.DeInit != NULL)
  {
    return USBD_MIDI.DeInit(pdev, cfgidx);
  }
  return USBD_FAIL;
}

static uint8_t USBD_BRICK6_COMPOSITE_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  if (USBD_MIDI.Setup != NULL)
  {
    return USBD_MIDI.Setup(pdev, req);
  }
  return USBD_FAIL;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetCfgDesc(uint16_t *length)
{
  if (USBD_MIDI.GetFSConfigDescriptor != NULL)
  {
    return USBD_MIDI.GetFSConfigDescriptor(length);
  }
  return NULL;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length)
{
  if (USBD_MIDI.GetDeviceQualifierDescriptor != NULL)
  {
    return USBD_MIDI.GetDeviceQualifierDescriptor(length);
  }
  return NULL;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  if (USBD_MIDI.DataIn != NULL)
  {
    return USBD_MIDI.DataIn(pdev, epnum);
  }
  return USBD_FAIL;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  if (USBD_MIDI.DataOut != NULL)
  {
    return USBD_MIDI.DataOut(pdev, epnum);
  }
  return USBD_FAIL;
}
