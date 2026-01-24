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
#include "codec_pcm5100a.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define AUDIO_SAMPLE_RATE_HZ 44100U
#define AUDIO_TONE_HZ 1000U
#define AUDIO_FRAMES_PER_BUFFER 1024U
#define AUDIO_CHANNELS 2U
#define AUDIO_BUFFER_SAMPLES (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS)
#define UART_LOG_TIMEOUT_MS 10U
#define AUDIO_LOG_DECIMATION 200U
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
static int16_t audio_buffer[AUDIO_BUFFER_SAMPLES];
static volatile bool audio_half_ready = false;
static volatile bool audio_full_ready = false;
static volatile uint32_t audio_half_count = 0;
static volatile uint32_t audio_full_count = 0;
static float audio_phase = 0.0f;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void UART_Log(const char *message);
static void UART_Logf(const char *format, ...);
static void FillAudioBuffer(int16_t *buffer, uint32_t frames);
static void StartAudioPlayback(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void UART_Log(const char *message)
{
  if (message == NULL)
  {
    return;
  }

  HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), UART_LOG_TIMEOUT_MS);
}

static void UART_Logf(const char *format, ...)
{
  char buffer[128];
  va_list args;

  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  UART_Log(buffer);
}

static void FillAudioBuffer(int16_t *buffer, uint32_t frames)
{
  const float phase_increment = 2.0f * (float)M_PI * ((float)AUDIO_TONE_HZ / (float)AUDIO_SAMPLE_RATE_HZ);
  const float amplitude = 0.7f * (float)INT16_MAX;

  for (uint32_t i = 0; i < frames; i++)
  {
    int16_t sample = (int16_t)(sinf(audio_phase) * amplitude);

    buffer[i * 2U] = sample;
    buffer[(i * 2U) + 1U] = sample;

    audio_phase += phase_increment;
    if (audio_phase >= 2.0f * (float)M_PI)
    {
      audio_phase -= 2.0f * (float)M_PI;
    }
  }
}

static void StartAudioPlayback(void)
{
  FillAudioBuffer(audio_buffer, AUDIO_FRAMES_PER_BUFFER);

  if (HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audio_buffer, AUDIO_BUFFER_SAMPLES) != HAL_OK)
  {
    UART_Log("ERROR: SAI DMA start failed\r\n");
    Error_Handler();
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
  MX_USART1_UART_Init();
  UART_Log("Clock init OK\r\n");
  UART_Log("GPIO init OK\r\n");
  MX_DMA_Init();
  UART_Log("DMA init OK\r\n");
  MX_SAI1_Init();
  UART_Log("SAI1 init OK\r\n");
  /* USER CODE BEGIN 2 */
  UART_Log("UART1 init OK\r\n");
  PCM5100A_Init();
  UART_Log("PCM5100A init (mute)\r\n");
  PCM5100A_Mute(false);
  UART_Log("PCM5100A unmute\r\n");
  StartAudioPlayback();
  UART_Log("Audio start\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    if (audio_half_ready)
    {
      audio_half_ready = false;
      FillAudioBuffer(audio_buffer, AUDIO_FRAMES_PER_BUFFER / 2U);
      if ((audio_half_count % AUDIO_LOG_DECIMATION) == 0U)
      {
        UART_Logf("DMA half callback: %lu\r\n", (unsigned long)audio_half_count);
      }
    }

    if (audio_full_ready)
    {
      audio_full_ready = false;
      FillAudioBuffer(&audio_buffer[AUDIO_FRAMES_PER_BUFFER], AUDIO_FRAMES_PER_BUFFER / 2U);
      if ((audio_full_count % AUDIO_LOG_DECIMATION) == 0U)
      {
        UART_Logf("DMA full callback: %lu\r\n", (unsigned long)audio_full_count);
      }
    }

    HAL_GPIO_TogglePin(LED_DEBUG_GPIO_Port, LED_DEBUG_Pin);
    HAL_Delay(200);
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
    audio_half_ready = true;
    audio_half_count++;
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
    audio_full_ready = true;
    audio_full_count++;
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
  UART_Log("ERROR: Error_Handler entered\r\n");
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
