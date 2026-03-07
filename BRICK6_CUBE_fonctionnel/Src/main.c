#include "audio_debug_log.h"
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "fatfs.h"
#include "i2c.h"
#include "sai.h"
#include "sdmmc.h"
#include "spi.h"
#include "usart.h"
#include "usb_otg.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "usb_device.h"
#include "usb_host.h"
#include "cs42448.h"
#include "midi.h"
#include "midi_host.h"
#include "sdram.h"
#include "engine_tasklet.h"
#include "ui_tasklet.h"
#include "brick6_app_init.h"
#include "audio.h"
#include "audio_float.h"
#include "Storage/audio_streamer.h"
#include "Streaming/stream_manager.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */
/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PHASE0_DEBUG_LOG 1
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */
void MX_USB_HOST_Process(void);
void MX_USB_HOST_Init(void);
void MX_USB_DEVICE_Init(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SAI1_Init();
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  //MX_USB_OTG_FS_PCD_Init();
  MX_FMC_Init();
  MX_SDMMC1_SD_Init();
  MX_SPI5_Init();
  MX_FATFS_Init();
  /* USER CODE BEGIN 2 */
  brick6_app_init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint32_t last_tick = 0;

  static uint32_t last_log_time = 0;
  static uint32_t last_sd_bytes = 0;
  static uint32_t last_sd_frames = 0;
  static uint32_t last_refills = 0;

  while (1)
  {
      engine_tasklet_poll();
      brick6_app_process();

      MX_USB_HOST_Process();
      usb_host_tasklet_poll_bounded(4);
      midi_host_poll_bounded(8);

      if(engine_tick_count != last_tick)
      {
          last_tick = engine_tick_count;
          ui_tasklet_poll();
      }

#if PHASE0_DEBUG_LOG
      if((HAL_GetTick() - last_log_time) >= 1000U)
      {
          const uint32_t now = HAL_GetTick();
          const uint32_t dt_ms = (last_log_time == 0U) ? 1000U : (now - last_log_time);

          audio_streamer_stats_t streamer_stats;
          stream_manager_stats_t manager_stats;
          audio_debug_stats_t audio_stats;
          brick6_app_stats_t app_stats;

          audio_streamer_get_stats(0U, &streamer_stats);
          stream_manager_get_stats(&manager_stats);
          audio_debug_get_stats(&audio_stats);
          brick6_app_get_stats(&app_stats);

          const uint32_t sd_bytes_delta = streamer_stats.total_bytes_read_from_sd - last_sd_bytes;
          const uint32_t sd_frames_delta = streamer_stats.total_frames_filled_from_sd - last_sd_frames;
          const uint32_t refill_delta = streamer_stats.total_refills - last_refills;

          last_sd_bytes = streamer_stats.total_bytes_read_from_sd;
          last_sd_frames = streamer_stats.total_frames_filled_from_sd;
          last_refills = streamer_stats.total_refills;
          last_log_time = now;

          const uint32_t bytes_per_s = (sd_bytes_delta * 1000U) / (dt_ms ? dt_ms : 1U);
          const uint32_t frames_per_s = (sd_frames_delta * 1000U) / (dt_ms ? dt_ms : 1U);
          const uint32_t refills_per_s = (refill_delta * 1000U) / (dt_ms ? dt_ms : 1U);
          const uint32_t refill_avg_ms = (streamer_stats.total_refills > 0U)
                                       ? (streamer_stats.total_refill_time_ms / streamer_stats.total_refills)
                                       : 0U;
          const uint32_t manager_dt_avg_ms = (manager_stats.process_dt_samples > 0U)
                                           ? (manager_stats.process_dt_acc_ms / manager_stats.process_dt_samples)
                                           : 0U;

          AUDIO_DEBUG_LOG("[P0] ring=%lu/%u min=%lu max=%lu underrun=%lu restart=%lu partial=%lu\n",
                 (unsigned long)streamer_stats.ring_used_frames,
                 16384U,
                 (unsigned long)streamer_stats.ring_level_min_frames,
                 (unsigned long)streamer_stats.ring_level_max_frames,
                 (unsigned long)streamer_stats.underrun_count,
                 (unsigned long)streamer_stats.file_restart_count,
                 (unsigned long)streamer_stats.partial_read_count);

          AUDIO_DEBUG_LOG("[P0] sd B/s=%lu frames/s=%lu refills/s=%lu chunk_last=%luB(%luf) read_max=%lums refill_max=%lums refill_avg=%lums\n",
                 (unsigned long)bytes_per_s,
                 (unsigned long)frames_per_s,
                 (unsigned long)refills_per_s,
                 (unsigned long)streamer_stats.last_refill_bytes,
                 (unsigned long)streamer_stats.last_refill_frames,
                 (unsigned long)streamer_stats.sd_read_time_max_ms,
                 (unsigned long)streamer_stats.refill_time_max_ms,
                 (unsigned long)refill_avg_ms);

          AUDIO_DEBUG_LOG("[P0] loop stream_calls=%lu app_calls=%lu dt_stream max=%lums avg=%lums wd=%lu audio_blocks=%lu dsp_frames=%lu\n",
                 (unsigned long)manager_stats.process_call_count,
                 (unsigned long)app_stats.app_process_call_count,
                 (unsigned long)manager_stats.process_dt_max_ms,
                 (unsigned long)manager_dt_avg_ms,
                 (unsigned long)manager_stats.process_watchdog_count,
                 (unsigned long)audio_stats.audio_block_counter,
                 (unsigned long)audio_stats.dsp_frames_counter);

          AUDIO_DEBUG_LOG("[P0] ring_checks overflow=%lu underflow_logic=%lu incoherence=%lu pos_oob=%lu\n",
                 (unsigned long)streamer_stats.ring_overflow_detect_count,
                 (unsigned long)streamer_stats.ring_underflow_logic_count,
                 (unsigned long)streamer_stats.ring_incoherence_count,
                 (unsigned long)streamer_stats.pos_oob_count);
      }
#endif
  }

  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 160;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
  PeriphClkInitStruct.PLL3.PLL3M = 25;
  PeriphClkInitStruct.PLL3.PLL3N = 491;
  PeriphClkInitStruct.PLL3.PLL3P = 40;
  PeriphClkInitStruct.PLL3.PLL3Q = 2;
  PeriphClkInitStruct.PLL3.PLL3R = 2;
  PeriphClkInitStruct.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
  PeriphClkInitStruct.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
  PeriphClkInitStruct.PLL3.PLL3FRACN = 4260;
  PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL3;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  __BKPT(0);   // 🔥 force un break debugger ICI
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: AUDIO_DEBUG_LOG("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
