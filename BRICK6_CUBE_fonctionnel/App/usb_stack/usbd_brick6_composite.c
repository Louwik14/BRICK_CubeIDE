#include "usbd_brick6_composite.h"

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
  (void)pdev;
  (void)cfgidx;
  return USBD_OK;
}

static uint8_t USBD_BRICK6_COMPOSITE_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  (void)pdev;
  (void)cfgidx;
  return USBD_OK;
}

static uint8_t USBD_BRICK6_COMPOSITE_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  (void)pdev;
  (void)req;
  return USBD_OK;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetCfgDesc(uint16_t *length)
{
  (void)length;
  return NULL;
}

static uint8_t *USBD_BRICK6_COMPOSITE_GetDeviceQualifierDesc(uint16_t *length)
{
  (void)length;
  return NULL;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  (void)pdev;
  (void)epnum;
  return USBD_OK;
}

static uint8_t USBD_BRICK6_COMPOSITE_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  (void)pdev;
  (void)epnum;
  return USBD_OK;
}
