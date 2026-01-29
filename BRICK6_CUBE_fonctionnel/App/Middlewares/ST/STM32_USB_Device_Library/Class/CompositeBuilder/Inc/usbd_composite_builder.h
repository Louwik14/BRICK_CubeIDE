#ifndef USBD_COMPOSITE_BUILDER_H
#define USBD_COMPOSITE_BUILDER_H

#include "usbd_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
  uint8_t *(*GetFSConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetHSConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetOtherSpeedConfigDescriptor)(uint16_t *length);
  uint8_t *(*GetDeviceQualifierDescriptor)(uint16_t *length);
} USBD_CompositeTypeDef;

extern USBD_CompositeTypeDef USBD_CMPSIT;

uint8_t USBD_CMPSIT_AddClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass,
                             USBD_CompositeClassTypeDef classtype, uint8_t cfgidx);
uint8_t USBD_CMPSIT_SetClassID(USBD_HandleTypeDef *pdev, USBD_CompositeClassTypeDef classtype,
                               uint8_t instance);
void USBD_CMPST_ClearConfDesc(USBD_HandleTypeDef *pdev);

#ifdef __cplusplus
}
#endif

#endif /* USBD_COMPOSITE_BUILDER_H */
