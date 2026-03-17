/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "sai.h"
#include "sdmmc.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

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
#include "fatfs.h"
#include "audio_debug_log.h"
#include "led_rgb.h"
#include "led_ids.h"
#include "led_anim.h"

#include "ui_display.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */

extern volatile uint32_t engine_tick_count;

/* USER CODE END PV */

void SystemClock_Config(void);
void PeriphCommonClock_Config(void);

int main(void)
{

  SCB_EnableICache();

  HAL_Init();

  SystemClock_Config();
  PeriphCommonClock_Config();

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_FMC_Init();
  MX_SDMMC1_SD_Init();
  MX_SPI5_Init();
  MX_I2C2_Init();
  MX_ADC2_Init();
  MX_SAI2_Init();
  MX_ADC1_Init();
  MX_ADC3_Init();
  MX_TIM2_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();

  /* =========================================================
     APP INIT
     ========================================================= */

  MX_FATFS_Init();

  brick6_app_init();

  led_init();

  uint32_t last_tick = 0;
  static uint32_t last_log_time = 0;
  static uint32_t last_display_flush = 0;

  led_anim_blink(LED_STEP_2,255,0,0,400);

  /* =========================================================
     SUPERLOOP
     ========================================================= */

  while (1)
  {

      brick6_app_process();

      MX_USB_HOST_Process();
      usb_host_tasklet_poll_bounded(4);
      midi_host_poll_bounded(8);

      if(engine_tick_count != last_tick)
      {
          last_tick = engine_tick_count;
          ui_tasklet_poll();
      }

      if((HAL_GetTick() - last_display_flush) >= 33U)
      {
          last_display_flush = HAL_GetTick();
          display_update();
      }

#if PHASE0_DEBUG_LOG
      if((HAL_GetTick() - last_log_time) >= 1000U)
      {
          audio_debug_stats_t audio_stats;
          brick6_app_stats_t app_stats;

          audio_debug_get_stats(&audio_stats);
          brick6_app_get_stats(&app_stats);

          last_log_time = HAL_GetTick();

          AUDIO_DEBUG_LOG("[P0] app_calls=%lu audio_blocks=%lu dsp_frames=%lu rec_state=%u\n",
                  (unsigned long)app_stats.app_process_call_count,
                  (unsigned long)audio_stats.audio_block_counter,
                  (unsigned long)audio_stats.dsp_frames_counter,
                  (unsigned)app_stats.recorder_state);
      }
#endif

  }

}

/* ========================================================= */

void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  RCC_OscInitStruct.OscillatorType =
      RCC_OSCILLATORTYPE_HSI48 |
      RCC_OSCILLATORTYPE_CSI |
      RCC_OSCILLATORTYPE_HSE;

  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.CSIState = RCC_CSI_ON;

  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 5;
  RCC_OscInitStruct.PLL.PLLN = 192;
  RCC_OscInitStruct.PLL.PLLP = 2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  RCC_OscInitStruct.PLL.PLLR = 2;

  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType =
      RCC_CLOCKTYPE_HCLK |
      RCC_CLOCKTYPE_SYSCLK |
      RCC_CLOCKTYPE_PCLK1 |
      RCC_CLOCKTYPE_PCLK2 |
      RCC_CLOCKTYPE_D3PCLK1 |
      RCC_CLOCKTYPE_D1PCLK1;

  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection =
      RCC_PERIPHCLK_ADC |
      RCC_PERIPHCLK_SAI2;

  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}
