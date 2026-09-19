/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    bsp_driver_sd.c for H7 (based on stm32h743i_eval_sd.c)
 * @brief   This file includes a generic uSD card driver.
 *          To be completed by the user according to the board used for the project.
 * @note    Some functions generated as weak: they can be overridden by
 *          - code in user files
 *          - or BSP code from the FW pack files
 *          if such files are added to the generated project (by the user).
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

/* USER CODE BEGIN FirstSection */
/* can be used to modify / undefine following code or add new definitions */
#include "Platform/memory_layout.h"
/* USER CODE END FirstSection */
/* Includes ------------------------------------------------------------------*/
#include "bsp_driver_sd.h"
#include "Storage/sd_access_gate.h"

/* Extern variables ---------------------------------------------------------*/

extern SD_HandleTypeDef hsd1;
static uint8_t g_bsp_sd_initialized;

/* USER CODE BEGIN BeforeInitSection */
/* can be used to modify / undefine following code or add code */
#define BSP_SD_SAFE_CLOCK_DIVIDER       (4U)
#define BSP_SD_HIGH_SPEED_CLOCK_DIVIDER (2U)

UI_HOT_DTCM volatile bsp_sd_high_speed_diag_t g_bsp_sd_high_speed_diag
  __attribute__((used, aligned(32)));

static uint32_t BSP_SD_ClockHz(uint32_t kernel_hz, uint32_t divider)
{
  return (divider != 0U) ? kernel_hz / (2U * divider) : kernel_hz;
}

static void BSP_SD_RecordRuntimeConfig(uint8_t high_speed_succeeded)
{
  PLL2_ClocksTypeDef pll2 = {0};
  const uint32_t kernel_hz =
    HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
  const uint32_t clkcr = hsd1.Instance->CLKCR;
  const uint32_t divider = clkcr & SDMMC_CLKCR_CLKDIV;
  const uint32_t width = clkcr & SDMMC_CLKCR_WIDBUS;
  HAL_RCCEx_GetPLL2ClockFreq(&pll2);

  g_bsp_sd_high_speed_diag.card_supports_high_speed =
    (high_speed_succeeded != 0U) ? 1U : 0U;
  g_bsp_sd_high_speed_diag.high_speed_switch_succeeded =
    (high_speed_succeeded != 0U) ? 1U : 0U;
  g_bsp_sd_high_speed_diag.final_clock_divider = divider;
  g_bsp_sd_high_speed_diag.final_sdclk_hz =
    BSP_SD_ClockHz(kernel_hz, divider);
  g_bsp_sd_high_speed_diag.final_bus_width_bits =
    (width == SDMMC_BUS_WIDE_4B) ? 4U : 1U;
  g_bsp_sd_high_speed_diag.pll2r_hz = pll2.PLL2_R_Frequency;
  g_bsp_sd_high_speed_diag.fmc_kernel_hz = HAL_RCC_GetHCLKFreq();
  g_bsp_sd_high_speed_diag.sdram_clock_hz = HAL_RCC_GetHCLKFreq() / 2U;
  g_bsp_sd_high_speed_diag.adc_clock_hz =
    HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_ADC) / 2U;
}

void BSP_SD_ConfigureHighSpeed(void)
{
  uint8_t high_speed_succeeded = 0U;

  g_bsp_sd_high_speed_diag.card_supports_high_speed = 0U;
  g_bsp_sd_high_speed_diag.high_speed_switch_succeeded = 0U;
  g_bsp_sd_high_speed_diag.final_clock_divider = 0U;
  g_bsp_sd_high_speed_diag.final_sdclk_hz = 0U;
  g_bsp_sd_high_speed_diag.final_bus_width_bits = 0U;
  g_bsp_sd_high_speed_diag.pll2r_hz = 0U;
  g_bsp_sd_high_speed_diag.fmc_kernel_hz = 0U;
  g_bsp_sd_high_speed_diag.sdram_clock_hz = 0U;
  g_bsp_sd_high_speed_diag.adc_clock_hz = 0U;

  if (HAL_SD_ConfigSpeedBusOperation(
        &hsd1, SDMMC_SPEED_MODE_HIGH) == HAL_OK)
  {
    hsd1.Init.ClockDiv = BSP_SD_HIGH_SPEED_CLOCK_DIVIDER;
    (void)SDMMC_Init(hsd1.Instance, hsd1.Init);
    high_speed_succeeded = 1U;
  }
  else
  {
    /* CMD6 mode-switch failure leaves a non-HS card in Default Speed.  Keep
       the known-safe 25 MHz clock and do not turn lack of HS into a boot
       failure. */
    hsd1.ErrorCode = HAL_SD_ERROR_NONE;
    hsd1.State = HAL_SD_STATE_READY;
    hsd1.Init.ClockDiv = BSP_SD_SAFE_CLOCK_DIVIDER;
    (void)SDMMC_Init(hsd1.Instance, hsd1.Init);
  }

  BSP_SD_RecordRuntimeConfig(high_speed_succeeded);
}
/* USER CODE END BeforeInitSection */
/**
  * @brief  Initializes the SD card device.
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_Init.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_Init.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_Init(void)
{
  uint8_t sd_state = MSD_OK;
  g_bsp_sd_initialized = 0U;
  /* HAL SD initialization */
  sd_state = HAL_SD_Init(&hsd1);
  /* Configure SD Bus width (4 bits mode selected) */
  if (sd_state == MSD_OK)
  {
    /* Enable wide operation */
    if (HAL_SD_ConfigWideBusOperation(&hsd1, SDMMC_BUS_WIDE_4B) != HAL_OK)
    {
      sd_state = MSD_ERROR;
    }
  }

  if (sd_state == MSD_OK)
  {
    g_bsp_sd_initialized = 1U;
  }

  return sd_state;
}
/* USER CODE BEGIN AfterInitSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END AfterInitSection */

/* USER CODE BEGIN InterruptMode */
/**
  * @brief  Configures Interrupt mode for SD detection pin.
  * @retval Returns 0
  */
/**
 * @brief Point d'entrée BSP_SD_ITConfig.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_ITConfig.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_ITConfig(void)
{
  /* Code to be updated by the user or replaced by one from the FW pack (in a stmxxxx_sd.c file) */

  return (uint8_t)0;
}

/* USER CODE END InterruptMode */

/* USER CODE BEGIN BeforeReadBlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeReadBlocksSection */
/**
  * @brief  Reads block(s) from a specified address in an SD card, in polling mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  ReadAddr: Address from where data is to be read
  * @param  NumOfBlocks: Number of SD blocks to read
  * @param  Timeout: Timeout for read operation
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_ReadBlocks.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_ReadBlocks.
 *
 * @param pData Paramètre d'entrée de l'API.
 * @param ReadAddr Paramètre d'entrée de l'API.
 * @param NumOfBlocks Paramètre d'entrée de l'API.
 * @param Timeout Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_ReadBlocks(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks, Timeout) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeWriteBlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeWriteBlocksSection */
/**
  * @brief  Writes block(s) to a specified address in an SD card, in polling mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  WriteAddr: Address from where data is to be written
  * @param  NumOfBlocks: Number of SD blocks to write
  * @param  Timeout: Timeout for write operation
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_WriteBlocks.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_WriteBlocks.
 *
 * @param pData Paramètre d'entrée de l'API.
 * @param WriteAddr Paramètre d'entrée de l'API.
 * @param NumOfBlocks Paramètre d'entrée de l'API.
 * @param Timeout Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_WriteBlocks(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks, Timeout) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeReadDMABlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeReadDMABlocksSection */
/**
  * @brief  Reads block(s) from a specified address in an SD card, in DMA mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  ReadAddr: Address from where data is to be read
  * @param  NumOfBlocks: Number of SD blocks to read
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_ReadBlocks_DMA.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_ReadBlocks_DMA.
 *
 * @param pData Paramètre d'entrée de l'API.
 * @param ReadAddr Paramètre d'entrée de l'API.
 * @param NumOfBlocks Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_ReadBlocks_DMA(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks)
{
  uint8_t sd_state = MSD_OK;

  /* Read block(s) in DMA transfer mode */
  if (HAL_SD_ReadBlocks_DMA(&hsd1, (uint8_t *)pData, ReadAddr, NumOfBlocks) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeWriteDMABlocksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeWriteDMABlocksSection */
/**
  * @brief  Writes block(s) to a specified address in an SD card, in DMA mode.
  * @param  pData: Pointer to the buffer that will contain the data to transmit
  * @param  WriteAddr: Address from where data is to be written
  * @param  NumOfBlocks: Number of SD blocks to write
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_WriteBlocks_DMA.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_WriteBlocks_DMA.
 *
 * @param pData Paramètre d'entrée de l'API.
 * @param WriteAddr Paramètre d'entrée de l'API.
 * @param NumOfBlocks Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_WriteBlocks_DMA(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks)
{
  uint8_t sd_state = MSD_OK;

  /* Write block(s) in DMA transfer mode */
  if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)pData, WriteAddr, NumOfBlocks) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeEraseSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeEraseSection */
/**
  * @brief  Erases the specified memory area of the given SD card.
  * @param  StartAddr: Start byte address
  * @param  EndAddr: End byte address
  * @retval SD status
  */
/**
 * @brief Point d'entrée BSP_SD_Erase.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_Erase.
 *
 * @param StartAddr Paramètre d'entrée de l'API.
 * @param EndAddr Paramètre d'entrée de l'API.
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_Erase(uint32_t StartAddr, uint32_t EndAddr)
{
  uint8_t sd_state = MSD_OK;

  if (HAL_SD_Erase(&hsd1, StartAddr, EndAddr) != HAL_OK)
  {
    sd_state = MSD_ERROR;
  }

  return sd_state;
}

/* USER CODE BEGIN BeforeGetCardStateSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeGetCardStateSection */

/**
  * @brief  Gets the current SD card data status.
  * @param  None
  * @retval Data transfer state.
  *          This value can be one of the following values:
  *            @arg  SD_TRANSFER_OK: No data transfer is acting
  *            @arg  SD_TRANSFER_BUSY: Data transfer is acting
  */
/**
 * @brief Point d'entrée BSP_SD_GetCardState.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_GetCardState.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_GetCardState(void)
{
  const uint8_t result = (HAL_SD_GetCardState(&hsd1) == HAL_SD_CARD_TRANSFER)
      ? SD_TRANSFER_OK : SD_TRANSFER_BUSY;
  return result;
}

/**
  * @brief  Get SD information about specific SD card.
  * @param  CardInfo: Pointer to HAL_SD_CardInfoTypedef structure
  * @retval None
  */
/**
 * @brief Point d'entrée BSP_SD_GetCardInfo.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_GetCardInfo.
 *
 * @param CardInfo Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak void BSP_SD_GetCardInfo(HAL_SD_CardInfoTypeDef *CardInfo)
{
  /* Get SD card Information */
  HAL_SD_GetCardInfo(&hsd1, CardInfo);
}

/* USER CODE BEGIN BeforeCallBacksSection */
/* can be used to modify previous code / undefine following code / add code */
/* USER CODE END BeforeCallBacksSection */
/**
  * @brief SD Abort callbacks
  * @param hsd: SD handle
  * @retval None
  */
/**
 * @brief Point d'entrée HAL_SD_AbortCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_SD_AbortCallback.
 *
 * @param hsd Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_SD_AbortCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_AbortCallback();
}

/**
  * @brief Tx Transfer completed callback
  * @param hsd: SD handle
  * @retval None
  */
/**
 * @brief Point d'entrée HAL_SD_TxCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_SD_TxCpltCallback.
 *
 * @param hsd Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_WriteCpltCallback();
}

/**
  * @brief Rx Transfer completed callback
  * @param hsd: SD handle
  * @retval None
  */
/**
 * @brief Point d'entrée HAL_SD_RxCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à HAL_SD_RxCpltCallback.
 *
 * @param hsd Paramètre d'entrée de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  BSP_SD_ReadCpltCallback();
}

/* USER CODE BEGIN CallBacksSection_C */
void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  (void)hsd;
  BSP_SD_ErrorCallback();
}
/**
  * @brief BSP SD Abort callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
/**
 * @brief Point d'entrée BSP_SD_AbortCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_AbortCallback.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak void BSP_SD_AbortCallback(void)
{

}

/**
  * @brief BSP Tx Transfer completed callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
/**
 * @brief Point d'entrée BSP_SD_WriteCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_WriteCpltCallback.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak void BSP_SD_WriteCpltCallback(void)
{

}

/**
  * @brief BSP Rx Transfer completed callback
  * @retval None
  * @note empty (up to the user to fill it in or to remove it if useless)
  */
/**
 * @brief Point d'entrée BSP_SD_ReadCpltCallback.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_ReadCpltCallback.
 *
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak void BSP_SD_ReadCpltCallback(void)
{

}
/* USER CODE END CallBacksSection_C */

/**
 * @brief  Detects if SD card is correctly plugged in the memory slot or not.
 * @param  None
 * @retval Returns if SD is detected or not
 */
/**
 * @brief Point d'entrée BSP_SD_IsDetected.
 *
 * Rôle:
 * - Exécuter le traitement associé à BSP_SD_IsDetected.
 *
 *
 * @return Valeur de retour définie par le contrat de l'API.
 *
 * Contexte d'appel:
 * - init / main loop / tasklet selon le module.
 */
__weak uint8_t BSP_SD_IsDetected(void)
{
  /* LowCost has no card-detect GPIO. */
  return (g_bsp_sd_initialized != 0U) ? SD_PRESENT : SD_NOT_PRESENT;
}

/* USER CODE BEGIN AdditionalCode */
/* user code can be inserted here */
/* USER CODE END AdditionalCode */
