/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : SAI.c
  * Description        : This file provides code for the configuration
  *                      of the SAI instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "sai.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

SAI_HandleTypeDef hsai_BlockA1;
SAI_HandleTypeDef hsai_BlockB1;
SAI_HandleTypeDef hsai_BlockA2;
SAI_HandleTypeDef hsai_BlockB2;
DMA_HandleTypeDef hdma_sai1_a;
DMA_HandleTypeDef hdma_sai1_b;
DMA_HandleTypeDef hdma_sai2_a;
DMA_HandleTypeDef hdma_sai2_b;

static void SAI_TDM8_Config(SAI_HandleTypeDef *hsai)
{
  hsai->Init.Protocol = SAI_FREE_PROTOCOL;
  hsai->Init.DataSize = SAI_DATASIZE_24;
  hsai->Init.FirstBit = SAI_FIRSTBIT_MSB;
  hsai->Init.ClockStrobing = SAI_CLOCKSTROBING_FALLINGEDGE;
  hsai->Init.OutputDrive = SAI_OUTPUTDRIVE_DISABLE;
  hsai->Init.NoDivider = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai->Init.MckOverSampling = SAI_MCK_OVERSAMPLING_DISABLE;
  hsai->Init.FIFOThreshold = SAI_FIFOTHRESHOLD_FULL;
  hsai->Init.AudioFrequency = SAI_AUDIO_FREQUENCY_48K;
  hsai->Init.MonoStereoMode = SAI_STEREOMODE;
  hsai->Init.CompandingMode = SAI_NOCOMPANDING;
  hsai->Init.TriState = SAI_OUTPUT_NOTRELEASED;
  hsai->Init.PdmInit.Activation = DISABLE;
  hsai->Init.PdmInit.MicPairsNbr = 1;
  hsai->Init.PdmInit.ClockEnable = SAI_PDM_CLOCK1_ENABLE;
  hsai->FrameInit.FrameLength = 256;
  hsai->FrameInit.ActiveFrameLength = 1;
  hsai->FrameInit.FSDefinition = SAI_FS_STARTFRAME;
  hsai->FrameInit.FSPolarity = SAI_FS_ACTIVE_HIGH;
  hsai->FrameInit.FSOffset = SAI_FS_BEFOREFIRSTBIT;
  hsai->SlotInit.FirstBitOffset = 0;
  hsai->SlotInit.SlotSize = SAI_SLOTSIZE_32B;
  hsai->SlotInit.SlotNumber = 8;
  hsai->SlotInit.SlotActive = 0x000000FF;
}

/* SAI1 init function */
void MX_SAI1_Init(void)
{
  hsai_BlockB1.Instance = SAI1_Block_B;
  SAI_TDM8_Config(&hsai_BlockB1);
  hsai_BlockB1.Init.AudioMode = SAI_MODEMASTER_RX;
  hsai_BlockB1.Init.Synchro = SAI_ASYNCHRONOUS;
  hsai_BlockB1.Init.SynchroExt = SAI_SYNCEXT_OUTBLOCKB_ENABLE;
  if (HAL_SAI_Init(&hsai_BlockB1) != HAL_OK)
  {
    Error_Handler();
  }

  hsai_BlockA1.Instance = SAI1_Block_A;
  SAI_TDM8_Config(&hsai_BlockA1);
  hsai_BlockA1.Init.AudioMode = SAI_MODESLAVE_TX;
  hsai_BlockA1.Init.Synchro = SAI_SYNCHRONOUS;
  hsai_BlockA1.Init.SynchroExt = SAI_SYNCEXT_OUTBLOCKB_ENABLE;
  if (HAL_SAI_Init(&hsai_BlockA1) != HAL_OK)
  {
    Error_Handler();
  }
}

/* SAI2 init function */
void MX_SAI2_Init(void)
{
  hsai_BlockA2.Instance = SAI2_Block_A;
  SAI_TDM8_Config(&hsai_BlockA2);
  hsai_BlockA2.Init.AudioMode = SAI_MODESLAVE_TX;
  hsai_BlockA2.Init.Synchro = SAI_SYNCHRONOUS_EXT_SAI1;
  hsai_BlockA2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  if (HAL_SAI_Init(&hsai_BlockA2) != HAL_OK)
  {
    Error_Handler();
  }

  hsai_BlockB2.Instance = SAI2_Block_B;
  SAI_TDM8_Config(&hsai_BlockB2);
  hsai_BlockB2.Init.AudioMode = SAI_MODESLAVE_RX;
  hsai_BlockB2.Init.Synchro = SAI_SYNCHRONOUS_EXT_SAI1;
  hsai_BlockB2.Init.SynchroExt = SAI_SYNCEXT_DISABLE;
  if (HAL_SAI_Init(&hsai_BlockB2) != HAL_OK)
  {
    Error_Handler();
  }
}

static uint32_t SAI1_client = 0;
static uint32_t SAI2_client = 0;

void HAL_SAI_MspInit(SAI_HandleTypeDef* saiHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if(saiHandle->Instance==SAI1_Block_A)
  {
    if (SAI1_client == 0)
    {
      __HAL_RCC_SAI1_CLK_ENABLE();
      HAL_NVIC_SetPriority(SAI1_IRQn, 1, 0);
      HAL_NVIC_EnableIRQ(SAI1_IRQn);
    }
    SAI1_client++;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**SAI1_A_Block_A GPIO Configuration
    PC1     ------> SAI1_SD_A
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    hdma_sai1_a.Instance = DMA1_Stream6;
    hdma_sai1_a.Init.Request = DMA_REQUEST_SAI1_A;
    hdma_sai1_a.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_sai1_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai1_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai1_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai1_a.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai1_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai1_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_sai1_a.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_sai1_a.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_sai1_a.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_sai1_a.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_sai1_a) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(saiHandle,hdmatx,hdma_sai1_a);
  }
  else if(saiHandle->Instance==SAI1_Block_B)
  {
    if (SAI1_client == 0)
    {
      __HAL_RCC_SAI1_CLK_ENABLE();
      HAL_NVIC_SetPriority(SAI1_IRQn, 1, 0);
      HAL_NVIC_EnableIRQ(SAI1_IRQn);
    }
    SAI1_client++;

    __HAL_RCC_GPIOF_CLK_ENABLE();
    /**SAI1_B_Block_B GPIO Configuration
    PF6     ------> SAI1_SD_B
    PF7     ------> SAI1_MCLK_B
    PF8     ------> SAI1_SCK_B
    PF9     ------> SAI1_FS_B
    */
    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF6_SAI1;
    HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

    hdma_sai1_b.Instance = DMA1_Stream7;
    hdma_sai1_b.Init.Request = DMA_REQUEST_SAI1_B;
    hdma_sai1_b.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai1_b.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai1_b.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai1_b.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai1_b.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai1_b.Init.Mode = DMA_CIRCULAR;
    hdma_sai1_b.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_sai1_b.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_sai1_b.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_sai1_b.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_sai1_b.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_sai1_b) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(saiHandle,hdmarx,hdma_sai1_b);
  }
  else if(saiHandle->Instance==SAI2_Block_A)
  {
    if (SAI2_client == 0)
    {
      __HAL_RCC_SAI2_CLK_ENABLE();
      HAL_NVIC_SetPriority(SAI2_IRQn, 1, 0);
      HAL_NVIC_EnableIRQ(SAI2_IRQn);
    }
    SAI2_client++;

    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**SAI2_A_Block_A GPIO Configuration
    PD11     ------> SAI2_SD_A
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF10_SAI2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    hdma_sai2_a.Instance = DMA1_Stream3;
    hdma_sai2_a.Init.Request = DMA_REQUEST_SAI2_A;
    hdma_sai2_a.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_sai2_a.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai2_a.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai2_a.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai2_a.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai2_a.Init.Mode = DMA_CIRCULAR;
    hdma_sai2_a.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_sai2_a.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_sai2_a.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_sai2_a.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_sai2_a.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_sai2_a) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(saiHandle,hdmatx,hdma_sai2_a);
  }
  else if(saiHandle->Instance==SAI2_Block_B)
  {
    if (SAI2_client == 0)
    {
      __HAL_RCC_SAI2_CLK_ENABLE();
      HAL_NVIC_SetPriority(SAI2_IRQn, 1, 0);
      HAL_NVIC_EnableIRQ(SAI2_IRQn);
    }
    SAI2_client++;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**SAI2_B_Block_B GPIO Configuration
    PA0     ------> SAI2_SD_B
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF10_SAI2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    hdma_sai2_b.Instance = DMA1_Stream4;
    hdma_sai2_b.Init.Request = DMA_REQUEST_SAI2_B;
    hdma_sai2_b.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_sai2_b.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_sai2_b.Init.MemInc = DMA_MINC_ENABLE;
    hdma_sai2_b.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_sai2_b.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_sai2_b.Init.Mode = DMA_CIRCULAR;
    hdma_sai2_b.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_sai2_b.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_sai2_b.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    hdma_sai2_b.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_sai2_b.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_sai2_b) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(saiHandle,hdmarx,hdma_sai2_b);
  }
}

void HAL_SAI_MspDeInit(SAI_HandleTypeDef* saiHandle)
{
  if(saiHandle->Instance==SAI1_Block_A)
  {
    SAI1_client--;
    if (SAI1_client == 0)
    {
      __HAL_RCC_SAI1_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI1_IRQn);
    }

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1);
    HAL_DMA_DeInit(saiHandle->hdmatx);
  }
  else if(saiHandle->Instance==SAI1_Block_B)
  {
    SAI1_client--;
    if (SAI1_client == 0)
    {
      __HAL_RCC_SAI1_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI1_IRQn);
    }

    HAL_GPIO_DeInit(GPIOF, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);
    HAL_DMA_DeInit(saiHandle->hdmarx);
  }
  else if(saiHandle->Instance==SAI2_Block_A)
  {
    SAI2_client--;
    if (SAI2_client == 0)
    {
      __HAL_RCC_SAI2_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI2_IRQn);
    }

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_11);
    HAL_DMA_DeInit(saiHandle->hdmatx);
  }
  else if(saiHandle->Instance==SAI2_Block_B)
  {
    SAI2_client--;
    if (SAI2_client == 0)
    {
      __HAL_RCC_SAI2_CLK_DISABLE();
      HAL_NVIC_DisableIRQ(SAI2_IRQn);
    }

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_0);
    HAL_DMA_DeInit(saiHandle->hdmarx);
  }
}
