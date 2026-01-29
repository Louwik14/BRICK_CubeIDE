/**
  ******************************************************************************
  * @file    usbd_midi.c
  * @author  Illia Pikin, MCD Application Team
  * @brief   This file provides the MIDI core functions.
  *
  * @verbatim
  *      
  *          ===================================================================      
  *                                MIDI Class  Description
  *          =================================================================== 
  *           This module manages the MIDI class V1.0 following the "Universal Serial Bus
  *           Device Class Definition for MIDI Devices. Release 1.0 Nov 1, 1999".
  *      
  * @note     In HS mode and when the DMA is used, all variables and data structures
  *           dealing with the DMA during the transaction process should be 32-bit aligned.
  *           
  *      
  *  @endverbatim
  *
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2025 Illia Pikin</center></h2>
  * <h2><center>&copy; COPYRIGHT 2014 STMicroelectronics</center></h2>
  *
  * Licensed under MCD-ST Liberty SW License Agreement V2, (the "License");
  * You may not use this file except in compliance with the License.
  * You may obtain a copy of the License at:
  *
  *        http://www.st.com/software_license_agreement_liberty_v2
  *
  * Unless required by applicable law or agreed to in writing, software 
  * distributed under the License is distributed on an "AS IS" BASIS, 
  * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  * See the License for the specific language governing permissions and
  * limitations under the License.
  *
  * Licensed under BSD 2-Clause License
  *
  * Copyright (c) 2025, Illia Pikin a.k.a Hypnotriod
  *
  * Redistribution and use in source and binary forms, with or without
  * modification, are permitted provided that the following conditions are met:
  *
  * 1. Redistributions of source code must retain the above copyright notice, this
  *    list of conditions and the following disclaimer.
  *
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */ 

/* Includes ------------------------------------------------------------------*/
#include "usbd_midi.h"
#include "usbd_ctlreq.h"


/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @{
  */


/** @defgroup USBD_MIDI 
  * @brief usbd core module
  * @{
  */ 

/** @defgroup USBD_MIDI_Private_TypesDefinitions
  * @{
  */ 
/**
  * @}
  */ 


/** @defgroup USBD_MIDI_Private_Defines
  * @{
  */ 

/**
  * @}
  */ 


/** @defgroup USBD_MIDI_Private_Macros
  * @{
  */ 
/**
  * @}
  */ 




/** @defgroup USBD_MIDI_Private_FunctionPrototypes
  * @{
  */

static uint8_t  USBD_MIDI_Init (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_MIDI_DeInit (USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_MIDI_Setup (USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t  USBD_MIDI_DataIn (USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t  USBD_MIDI_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum);
/**
  * @}
  */ 

/** @defgroup USBD_MIDI_Private_Variables
  * @{
  */ 

static uint8_t usb_rx_buffer[MIDI_EPOUT_SIZE] = {0};


/* USB MIDI class type definition */
USBD_ClassTypeDef  USBD_MIDI = 
{
  USBD_MIDI_Init,
  USBD_MIDI_DeInit,
  USBD_MIDI_Setup,
  NULL, /*EP0_TxSent*/  
  NULL, /*EP0_RxReady*/
  USBD_MIDI_DataIn, /*DataIn*/
  USBD_MIDI_DataOut, /*DataOut*/
  NULL, /*SOF */
  NULL,
  NULL,      
  NULL,
  NULL,
  NULL,
  NULL,
};

/**
  * @}
  */ 

/** @defgroup USBD_MIDI_Private_Functions
  * @{
  */ 

/**
  * @brief  USBD_MIDI_Init
  *         Initialize the MIDI interface
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_MIDI_Init (USBD_HandleTypeDef *pdev, 
                               uint8_t cfgidx)
{
  uint8_t ret = 0;
  uint8_t midi_ep_in;
  uint8_t midi_ep_out;

  USBD_MIDI_HandleTypeDef *hmidi;

  UNUSED(cfgidx);

  hmidi = (USBD_MIDI_HandleTypeDef *)USBD_malloc(sizeof(USBD_MIDI_HandleTypeDef));

  if (hmidi == NULL)
  {
#ifdef USE_USBD_COMPOSITE
    pdev->pClassDataCmsit[pdev->classId] = NULL;
#endif
    return (uint8_t)USBD_EMEM;
  }

#ifdef USE_USBD_COMPOSITE
  pdev->pClassDataCmsit[pdev->classId] = (void *)hmidi;
  pdev->pClassData = pdev->pClassDataCmsit[pdev->classId];
#else
  pdev->pClassData = (void *)hmidi;
#endif

  midi_ep_in = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
  midi_ep_out = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);

  (void)USBD_LL_OpenEP(pdev, midi_ep_in, USBD_EP_TYPE_BULK, MIDI_EPIN_SIZE);
  (void)USBD_LL_OpenEP(pdev, midi_ep_out, USBD_EP_TYPE_BULK, MIDI_EPOUT_SIZE);

  (void)USBD_LL_PrepareReceive(pdev,
                               midi_ep_out,
                               usb_rx_buffer,
                               MIDI_EPOUT_SIZE);

  hmidi->state = MIDI_IDLE;

  return ret;
}

/**
  * @brief  USBD_MIDI_Init
  *         DeInitialize the MIDI layer
  * @param  pdev: device instance
  * @param  cfgidx: Configuration index
  * @retval status
  */
static uint8_t  USBD_MIDI_DeInit (USBD_HandleTypeDef *pdev, 
                                 uint8_t cfgidx)
{
  uint8_t midi_ep_in;
  uint8_t midi_ep_out;

  UNUSED(cfgidx);

  midi_ep_in = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
  midi_ep_out = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);

  /* Close MIDI EPs */
  (void)USBD_LL_CloseEP(pdev, midi_ep_in);
  (void)USBD_LL_CloseEP(pdev, midi_ep_out);

#ifdef USE_USBD_COMPOSITE
  if (pdev->pClassDataCmsit[pdev->classId] != NULL)
  {
    (void)USBD_free(pdev->pClassDataCmsit[pdev->classId]);
    pdev->pClassDataCmsit[pdev->classId] = NULL;
  }
#else
  if (pdev->pClassData != NULL)
  {
    USBD_free(pdev->pClassData);
    pdev->pClassData = NULL;
  }
#endif

  return USBD_OK;
}

/**
  * @brief  USBD_MIDI_Setup
  *         Handle the MIDI specific requests
  * @param  pdev: instance
  * @param  req: usb requests
  * @retval status
  */
static uint8_t  USBD_MIDI_Setup (USBD_HandleTypeDef *pdev, 
                                USBD_SetupReqTypedef *req)
{
  USBD_MIDI_HandleTypeDef     *hmidi;

#ifdef USE_USBD_COMPOSITE
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#else
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;
#endif

  if (hmidi == NULL)
  {
    return USBD_FAIL;
  }
  
  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
  case USB_REQ_TYPE_CLASS :  
    switch (req->bRequest)
    {
      case MIDI_REQ_SET_PROTOCOL:
        hmidi->Protocol = (uint8_t)(req->wValue);
        break;
        
      case MIDI_REQ_GET_PROTOCOL:
        USBD_CtlSendData (pdev, 
                          (uint8_t *)&hmidi->Protocol,
                          1);    
        break;
        
      case MIDI_REQ_SET_IDLE:
        hmidi->IdleState = (uint8_t)(req->wValue >> 8);
        break;
        
      case MIDI_REQ_GET_IDLE:
        USBD_CtlSendData (pdev, 
                          (uint8_t *)&hmidi->IdleState,
                          1);        
        break;      
        
      default:
        USBD_CtlError (pdev, req);
        return USBD_FAIL; 
    }
    break;
    
  case USB_REQ_TYPE_STANDARD:
    switch (req->bRequest)
    {
      case USB_REQ_GET_DESCRIPTOR:
        USBD_CtlError(pdev, req);
        return USBD_FAIL;

      case USB_REQ_GET_INTERFACE :
        USBD_CtlSendData (pdev,
                          (uint8_t *)&hmidi->AltSetting,
                          1);
        break;
        
      case USB_REQ_SET_INTERFACE :
        hmidi->AltSetting = (uint8_t)(req->wValue);
        break;
    }
  }
  return USBD_OK;
}

/**
  * @brief  USBD_MIDI_GetDeviceState 
  *         Get MIDI State
  * @param  pdev: device instance
  * @retval usb device state  (USBD_STATE_DEFAULT, USBD_STATE_ADDRESSED, USBD_STATE_CONFIGURED, USBD_STATE_SUSPENDED)
  */
uint8_t USBD_MIDI_GetDeviceState(USBD_HandleTypeDef  *pdev)
{
  return pdev->dev_state;
}

/**
  * @brief  USBD_MIDI_GetState 
  *         Get MIDI State
  * @param  pdev: device instance
  * @retval usb state  (MIDI_IDLE, MIDI_BUSY)
  */
uint8_t USBD_MIDI_GetState(USBD_HandleTypeDef  *pdev)
{
  USBD_MIDI_HandleTypeDef *hmidi;

#ifdef USE_USBD_COMPOSITE
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#else
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;
#endif

  if (hmidi == NULL)
  {
    return (uint8_t)MIDI_IDLE;
  }

  return hmidi->state;
}

/**
  * @brief  USBD_MIDI_SendPackets 
  *         Send MIDI event packets to the host
  * @param  pdev: device instance
  * @param  data: pointer to the packets data
  * @param  len: size of the data
  * @retval status
  */
uint8_t USBD_MIDI_SendPackets(USBD_HandleTypeDef  *pdev, 
                                 uint8_t *data,
                                 uint16_t len)
{
  uint8_t midi_ep_in;
  USBD_MIDI_HandleTypeDef *hmidi;

#ifdef USE_USBD_COMPOSITE
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#else
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;
#endif

  if (hmidi == NULL)
  {
    return (uint8_t)USBD_FAIL;
  }

  if (pdev->dev_state == USBD_STATE_CONFIGURED)
  {
    if(hmidi->state == MIDI_IDLE)
    {
      hmidi->state = MIDI_BUSY;
      midi_ep_in = USBD_CoreGetEPAdd(pdev, USBD_EP_IN, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);
      (void)USBD_LL_Transmit(pdev, midi_ep_in, data, len);
    }
  }
  return USBD_OK;
}


/**
  * @brief  USBD_MIDI_DataIn
  *         handle data IN Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t  USBD_MIDI_DataIn (USBD_HandleTypeDef *pdev, 
                              uint8_t epnum)
{
  USBD_MIDI_HandleTypeDef *hmidi;
  UNUSED(epnum);
  /* Ensure that the FIFO is empty before a new transfer, this condition could 
  be caused by  a new transfer before the end of the previous transfer */

#ifdef USE_USBD_COMPOSITE
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassDataCmsit[pdev->classId];
#else
  hmidi = (USBD_MIDI_HandleTypeDef *)pdev->pClassData;
#endif

  if (hmidi != NULL)
  {
    hmidi->state = MIDI_IDLE;
  }

  USBD_MIDI_OnPacketsSent();

  return USBD_OK;
}

/**
  * @brief  USBD_MIDI_OnPacketsSent
  *         on usb midi packets sent to the host callback
  */
__weak extern void USBD_MIDI_OnPacketsSent(void)
{
}

/**
  * @brief  USBD_MIDI_DataOut
  *         handle data OUT Stage
  * @param  pdev: device instance
  * @param  epnum: endpoint index
  * @retval status
  */
static uint8_t  USBD_MIDI_DataOut (USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint8_t midi_ep_out;
  uint8_t len;

  midi_ep_out = USBD_CoreGetEPAdd(pdev, USBD_EP_OUT, USBD_EP_TYPE_BULK, (uint8_t)pdev->classId);

  if (epnum != (midi_ep_out & 0x0F))
  {
    return USBD_FAIL;
  }
  
  len = (uint8_t)HAL_PCD_EP_GetRxCount((PCD_HandleTypeDef*) pdev->pData, epnum);

  USBD_MIDI_OnPacketsReceived(usb_rx_buffer, len);
  
  (void)USBD_LL_PrepareReceive(pdev, midi_ep_out, usb_rx_buffer, MIDI_EPOUT_SIZE);  
  
  return USBD_OK;
}

/**
  * @brief  USBD_MIDI_OnPacketsReceived
  *         on usb midi packets received from the host callback
  * @param  data: pointer to the data packet
  * @param  len: size of the data
  */
__weak extern void USBD_MIDI_OnPacketsReceived(uint8_t *data, uint8_t len)
{
  UNUSED(data);
  UNUSED(len);
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

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
