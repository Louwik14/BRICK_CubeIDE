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
#include "sai.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

enum
{
  AUDIO_SAMPLE_RATE = 44100U,
  AUDIO_TONE_HZ = 1000U,
  AUDIO_TABLE_SIZE = 256U,
  AUDIO_CHANNELS = 2U,
  AUDIO_FRAMES_PER_HALF = 256U,
  AUDIO_BUFFER_FRAMES = (AUDIO_FRAMES_PER_HALF * 2U),
  AUDIO_BUFFER_SAMPLES = (AUDIO_BUFFER_FRAMES * AUDIO_CHANNELS)
};

static int16_t audio_buffer[AUDIO_BUFFER_SAMPLES];
static volatile uint32_t audio_half_events = 0;
static volatile uint32_t audio_full_events = 0;
static uint32_t audio_phase = 0;
static uint32_t audio_phase_inc = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static const int16_t audio_sine_table[AUDIO_TABLE_SIZE] = {
  0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278,
  11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204,
  18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
  25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621,
  29956, 30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285,
  32412, 32521, 32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609,
  32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
  30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319,
  25832, 25329, 24811, 24279, 23731, 23170, 22594, 22005, 21403, 20787, 20159,
  19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279, 12539,
  11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212,
  2410, 1608, 804, 0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393,
  -7179, -7962, -8739, -9512, -10278, -11039, -11793, -12539, -13279, -14010,
  -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
  -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329,
  -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268,
  -29621, -29956, -30273, -30571, -30852, -31113, -31356, -31580, -31785,
  -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
  -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137,
  -31971, -31785, -31580, -31356, -31113, -30852, -30571, -30273, -29956,
  -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319,
  -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
  -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446,
  -14732, -14010, -13279, -12539, -11793, -11039, -10278, -9512, -8739,
  -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804
};

static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 10);
}

static void audio_fill_samples(uint32_t sample_offset, uint32_t frame_count)
{
  uint32_t sample_index = sample_offset;

  for (uint32_t frame = 0; frame < frame_count; ++frame)
  {
    uint32_t table_index = (audio_phase >> 16) & (AUDIO_TABLE_SIZE - 1U);
    int16_t sample = audio_sine_table[table_index];

    audio_buffer[sample_index++] = sample;
    audio_buffer[sample_index++] = sample;

    audio_phase += audio_phase_inc;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SAI1_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  char log_buffer[128];
  HAL_StatusTypeDef sai_status;

  audio_phase = 0;
  audio_phase_inc = (AUDIO_TONE_HZ * AUDIO_TABLE_SIZE * 65536U) / AUDIO_SAMPLE_RATE;
  audio_fill_samples(0U, AUDIO_BUFFER_FRAMES);

  uart_log("SAI1 PCM5100A audio start\r\n");
  sai_status = HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_buffer, AUDIO_BUFFER_SAMPLES);
  if (sai_status != HAL_OK)
  {
    snprintf(log_buffer, sizeof(log_buffer),
             "HAL_SAI_Transmit_DMA failed: %lu\r\n", (unsigned long)sai_status);
    uart_log(log_buffer);
    Error_Handler();
  }
  uart_log("SAI1 DMA started\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    static uint32_t last_led_tick = 0;
    static uint32_t last_log_tick = 0;
    static uint32_t last_error = 0;
    uint32_t now = HAL_GetTick();

    if ((now - last_led_tick) >= 500U)
    {
      HAL_GPIO_TogglePin(LED_DEBUG_GPIO_Port, LED_DEBUG_Pin);
      last_led_tick = now;
    }

    if ((now - last_log_tick) >= 1000U)
    {
      uint32_t error = HAL_SAI_GetError(&hsai_BlockA1);
      snprintf(log_buffer, sizeof(log_buffer),
               "SAI state=%lu err=0x%08lX half=%lu full=%lu\r\n",
               (unsigned long)hsai_BlockA1.State,
               (unsigned long)error,
               (unsigned long)audio_half_events,
               (unsigned long)audio_full_events);
      uart_log(log_buffer);

      if (error != 0U && error != last_error)
      {
        snprintf(log_buffer, sizeof(log_buffer),
                 "SAI error detected: 0x%08lX\r\n", (unsigned long)error);
        uart_log(log_buffer);
        last_error = error;
      }

      last_log_tick = now;
    }
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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
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

/* USER CODE BEGIN 4 */

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_fill_samples(0U, AUDIO_FRAMES_PER_HALF);
    audio_half_events++;
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_fill_samples(AUDIO_FRAMES_PER_HALF * AUDIO_CHANNELS, AUDIO_FRAMES_PER_HALF);
    audio_full_events++;
  }
}

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
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
