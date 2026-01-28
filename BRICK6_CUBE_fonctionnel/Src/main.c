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
#include "i2c.h"
#include "sai.h"
#include "sdmmc.h"
#include "usart.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "brick6_refactor.h"

#include "usb_device.h"
#include "usb_host.h"
#include "cs42448.h"
#include "audio_in.h"
#include "audio_out.h"
#include "midi.h"
#include "midi_host.h"
#include "sdram.h"
#include "sdram_alloc.h"
#include "sd_stream.h"
#include "engine_tasklet.h"
#include "diagnostics_tasklet.h"
#include "ui_tasklet.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
#if !BRICK6_REFACTOR_STEP_7
typedef enum
{
  SD_TEST_STATE_IDLE = 0,
  SD_TEST_STATE_MEMCHECK,
  SD_TEST_STATE_SECTOR0_WAIT,
  SD_TEST_STATE_KNOWN_WAIT,
  SD_TEST_STATE_LONG_RUN
} sd_test_state_t;
#endif

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#if !BRICK6_REFACTOR_STEP_7
#define SD_TEST_ENABLE                 1U
#define SD_TEST_MEMORY_PLACEMENT       1U
#define SD_TEST_SECTOR0                1U
#define SD_TEST_KNOWN_REGION           0U
#define SD_TEST_LONG_RUN               0U
#define SD_TEST_LOG_CALLBACKS          1U
#define SD_TEST_LONG_BLOCKS            (128U)
#define SD_TEST_KNOWN_START_BLOCK      2048U
#define SD_TEST_KNOWN_BLOCKS           SD_STREAM_BLOCKS_PER_BUFFER
#define SD_TEST_KNOWN_CRC32            0x00000000U
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
#if !BRICK6_REFACTOR_STEP_7
static uint8_t sd_test_running = 0U;
static uint8_t sd_test_done_logged = 0U;
static sd_test_state_t sd_test_state = SD_TEST_STATE_IDLE;
static uint8_t sd_test_failed = 0U;
static uint32_t sd_test_timeout = 0U;
static uint32_t sd_last_stats_tick = 0U;
static uint32_t sd_last_buf0_count = 0U;
static uint32_t sd_last_buf1_count = 0U;
static uint8_t sd_test_sector0_done = 0U;
static uint8_t sd_test_known_done = 0U;
#endif

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
#if !BRICK6_REFACTOR_STEP_7

static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 10);
}
#define LOG(msg) uart_log(msg)
#define LOGF(fmt, ...) \
  do { \
    char __buf[256]; \
    snprintf(__buf, sizeof(__buf), fmt, __VA_ARGS__); \
    uart_log(__buf); \
  } while(0)

static void SDRAM_Alloc_Test_Stop(uint32_t index, uint32_t got, uint32_t expected)
{
  LOGF("SDRAM alloc test FAILED idx=%lu got=0x%08lX expected=0x%08lX\r\n",
       (unsigned long)index,
       (unsigned long)got,
       (unsigned long)expected);
  while (1)
  {
    HAL_Delay(100);
  }
}

static void SDRAM_Alloc_Test(void)
{
  const uint32_t block1_size = 1024U * 1024U;
  const uint32_t block2_size = 512U * 1024U;

  SDRAM_Alloc_Reset();

  uint16_t *block16 = (uint16_t *)SDRAM_Alloc(block1_size, 2U);
  uint32_t *block32 = (uint32_t *)SDRAM_Alloc(block2_size, 4U);

  LOGF("SDRAM alloc block1=%p size=%lu\r\n", (void *)block16, (unsigned long)block1_size);
  LOGF("SDRAM alloc block2=%p size=%lu\r\n", (void *)block32, (unsigned long)block2_size);

  if ((block16 == NULL) || (block32 == NULL))
  {
    LOG("SDRAM alloc test FAILED: out of memory\r\n");
    while (1)
    {
      HAL_Delay(100);
    }
  }

  /* 16-bit pattern test (safe on x16 bus). */
  uint32_t count16 = block1_size / sizeof(uint16_t);
  for (uint32_t i = 0; i < count16; i++)
  {
    block16[i] = (uint16_t)(0xA500U ^ (uint16_t)i);
  }

  for (uint32_t i = 0; i < count16; i++)
  {
    uint16_t expected = (uint16_t)(0xA500U ^ (uint16_t)i);
    if (block16[i] != expected)
    {
      SDRAM_Alloc_Test_Stop(i, block16[i], expected);
    }
  }

  /* 32-bit pattern test using swap-safe helpers. */
  uint32_t count32 = block2_size / sizeof(uint32_t);
  uint32_t base_index = ((uint32_t)(uintptr_t)block32 - SDRAM_BANK_ADDR) / sizeof(uint32_t);

  for (uint32_t i = 0; i < count32; i++)
  {
    uint32_t value = 0x5A5A0000U | (i & 0xFFFFU);
    sdram_write32(base_index + i, value);
  }

  for (uint32_t i = 0; i < count32; i++)
  {
    uint32_t expected = 0x5A5A0000U | (i & 0xFFFFU);
    uint32_t read_value = sdram_read32(base_index + i);
    if (read_value != expected)
    {
      SDRAM_Alloc_Test_Stop(i, read_value, expected);
    }
  }

  LOG("SDRAM alloc test OK\r\n");
}

static const char *SD_MemoryRegion(const void *address)
{
  uintptr_t addr = (uintptr_t)address;
  if (addr >= 0x20000000U && addr < 0x20020000U)
  {
    return "DTCM";
  }
  if (addr >= 0x24000000U && addr < 0x24080000U)
  {
    return "AXI SRAM";
  }
  if (addr >= 0x30000000U && addr < 0x30048000U)
  {
    return "D2 SRAM";
  }
  if (addr >= 0x38000000U && addr < 0x38010000U)
  {
    return "D3 SRAM";
  }
  if (addr >= 0xC0000000U && addr < 0xD0000000U)
  {
    return "SDRAM";
  }
  return "UNKNOWN";
}

static void SD_LogHex(const uint8_t *data, uint32_t length)
{
  char line[80];
  uint32_t offset = 0U;
  while (offset < length)
  {
    uint32_t count = (length - offset > 16U) ? 16U : (length - offset);
    int pos = snprintf(line, sizeof(line), "%04lX:", (unsigned long)offset);
    for (uint32_t i = 0; i < count && pos < (int)sizeof(line) - 4; i++)
    {
      pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, " %02X", data[offset + i]);
    }
    snprintf(&line[pos], sizeof(line) - (size_t)pos, "\r\n");
    uart_log(line);
    offset += count;
  }
}

static uint32_t SD_CRC32(const uint8_t *data, uint32_t length)
{
  uint32_t crc = 0xFFFFFFFFU;
  for (uint32_t i = 0U; i < length; i++)
  {
    crc ^= data[i];
    for (uint32_t bit = 0U; bit < 8U; bit++)
    {
      uint32_t mask = 0U - (crc & 1U);
      crc = (crc >> 1U) ^ (0xEDB88320U & mask);
    }
  }
  return ~crc;
}

#endif
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
  MX_FMC_Init();
  MX_SDMMC1_SD_Init();
  /* USER CODE BEGIN 2 */
#if BRICK6_REFACTOR_STEP_7
  diagnostics_log("FMC init OK\r\n");
  diagnostics_log("Starting SDRAM init...\r\n");
#else
  LOG("FMC init OK\r\n");
  LOG("Starting SDRAM init...\r\n");
#endif
  SDRAM_Init();
#if BRICK6_REFACTOR_STEP_7
  diagnostics_log("SDRAM init done\r\n");
  diagnostics_log("Starting SDRAM test...\r\n");
#else
  LOG("SDRAM init done\r\n");
  LOG("Starting SDRAM test...\r\n");
#endif
  SDRAM_Test();
#if BRICK6_REFACTOR_STEP_7
  diagnostics_sdram_alloc_test();
#else
  SDRAM_Alloc_Test();
#endif

#if BRICK6_REFACTOR_STEP_7
  diagnostics_on_sd_stream_init(sd_stream_init(&hsd1));
#else
  if (sd_stream_init(&hsd1) == HAL_OK)
  {
    LOG("SD stream init OK\r\n");
    sd_stream_set_logger(uart_log);
    sd_stream_set_callback_logging(SD_TEST_LOG_CALLBACKS != 0U);
    sd_test_running = (SD_TEST_ENABLE != 0U);
    sd_test_state = SD_TEST_STATE_MEMCHECK;
    sd_test_sector0_done = 0U;
    sd_test_known_done = 0U;
  }
  else
  {
    LOG("SD stream init FAILED\r\n");
  }
#endif

  MX_USB_DEVICE_Init();
  MX_USB_HOST_Init();
  /* Init audio */
  AudioOut_Init(&hsai_BlockA1);
  AudioIn_Init(&hsai_BlockB1);

#if BRICK6_REFACTOR_STEP_3
  engine_tasklet_init(AUDIO_OUT_SAMPLE_RATE);
#endif


  AudioOut_Start();
  (void)HAL_SAI_Receive_DMA(&hsai_BlockB1,
                            (uint8_t *)AudioIn_GetBuffer(),
                            AudioIn_GetBufferSamples());

  HAL_Delay(200);

  /* Init MIDI */
  midi_init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
#if BRICK6_REFACTOR_STEP_2
    audio_tasklet_poll(); // PRIORITÉ ABSOLUE
#endif
#if BRICK6_REFACTOR_STEP_3
    engine_tasklet_poll();
#endif
#if BRICK6_REFACTOR_STEP_4
#if BRICK6_REFACTOR_STEP_6
    sd_tasklet_poll_bounded(SD_BUDGET_STEPS);
#else
    sd_tasklet_poll();
#endif
#endif
#if BRICK6_REFACTOR_STEP_6
    usb_host_tasklet_poll_bounded(USB_BUDGET_PACKETS);
#else
    MX_USB_HOST_Process();
#endif
#if BRICK6_REFACTOR_STEP_1
    brick6_usb_host_poll_count++;
#endif
#if BRICK6_REFACTOR_STEP_6
    midi_host_poll_bounded(MIDI_BUDGET_MSGS);
#else
    midi_host_poll();
#endif
#if BRICK6_REFACTOR_STEP_1
    brick6_midi_host_poll_count++;
#endif

#if BRICK6_REFACTOR_STEP_7
    ui_tasklet_poll();
    diagnostics_tasklet_poll();
#else
    static uint32_t last_led_tick = 0;
    static uint32_t last_log_tick = 0;
    static uint32_t last_error = 0;

    uint32_t now = HAL_GetTick();

    if ((now - last_led_tick) >= 500U)
    {
      HAL_GPIO_TogglePin(LED_DEBUG_GPIO_Port, LED_DEBUG_Pin);
      last_led_tick = now;
    }


    /* ======================================================== */

    if ((now - last_log_tick) >= 1000U)
    {
      uint32_t error = HAL_SAI_GetError(&hsai_BlockA1);
      uint32_t half = AudioOut_GetHalfEvents();
      uint32_t full = AudioOut_GetFullEvents();
      uint32_t rx_half = AudioIn_GetHalfEvents();
      uint32_t rx_full = AudioIn_GetFullEvents();

      uint32_t frames_per_sec = full * 512;  // 512 = AUDIO_BUFFER_FRAMES

#if BRICK6_REFACTOR_STEP_1
#if BRICK6_REFACTOR_STEP_6
#if BRICK6_REFACTOR_STEP_3
      LOGF("REF1 audio tx_half=%lu tx_full=%lu rx_half=%lu rx_full=%lu sd_rx=%lu sd_tx=%lu "
           "sd_buf0=%lu sd_buf1=%lu sd_err=%lu usb_poll=%lu midi_poll=%lu "
           "engine_ticks=%lu usb_budget=%lu midi_budget=%lu sd_budget=%lu\r\n",
           (unsigned long)brick6_audio_tx_half_count,
           (unsigned long)brick6_audio_tx_full_count,
           (unsigned long)brick6_audio_rx_half_count,
           (unsigned long)brick6_audio_rx_full_count,
           (unsigned long)brick6_sd_rx_cplt_count,
           (unsigned long)brick6_sd_tx_cplt_count,
           (unsigned long)brick6_sd_buf0_cplt_count,
           (unsigned long)brick6_sd_buf1_cplt_count,
           (unsigned long)brick6_sd_err_count,
           (unsigned long)brick6_usb_host_poll_count,
           (unsigned long)brick6_midi_host_poll_count,
           (unsigned long)engine_tick_count,
           (unsigned long)usb_budget_hit_count,
           (unsigned long)midi_budget_hit_count,
           (unsigned long)sd_budget_hit_count);
#else
      LOGF("REF1 audio tx_half=%lu tx_full=%lu rx_half=%lu rx_full=%lu sd_rx=%lu sd_tx=%lu "
           "sd_buf0=%lu sd_buf1=%lu sd_err=%lu usb_poll=%lu midi_poll=%lu "
           "usb_budget=%lu midi_budget=%lu sd_budget=%lu\r\n",
           (unsigned long)brick6_audio_tx_half_count,
           (unsigned long)brick6_audio_tx_full_count,
           (unsigned long)brick6_audio_rx_half_count,
           (unsigned long)brick6_audio_rx_full_count,
           (unsigned long)brick6_sd_rx_cplt_count,
           (unsigned long)brick6_sd_tx_cplt_count,
           (unsigned long)brick6_sd_buf0_cplt_count,
           (unsigned long)brick6_sd_buf1_cplt_count,
           (unsigned long)brick6_sd_err_count,
           (unsigned long)brick6_usb_host_poll_count,
           (unsigned long)brick6_midi_host_poll_count,
           (unsigned long)usb_budget_hit_count,
           (unsigned long)midi_budget_hit_count,
           (unsigned long)sd_budget_hit_count);
#endif
#else
#if BRICK6_REFACTOR_STEP_3
      LOGF("REF1 audio tx_half=%lu tx_full=%lu rx_half=%lu rx_full=%lu sd_rx=%lu sd_tx=%lu "
           "sd_buf0=%lu sd_buf1=%lu sd_err=%lu usb_poll=%lu midi_poll=%lu engine_ticks=%lu\r\n",
           (unsigned long)brick6_audio_tx_half_count,
           (unsigned long)brick6_audio_tx_full_count,
           (unsigned long)brick6_audio_rx_half_count,
           (unsigned long)brick6_audio_rx_full_count,
           (unsigned long)brick6_sd_rx_cplt_count,
           (unsigned long)brick6_sd_tx_cplt_count,
           (unsigned long)brick6_sd_buf0_cplt_count,
           (unsigned long)brick6_sd_buf1_cplt_count,
           (unsigned long)brick6_sd_err_count,
           (unsigned long)brick6_usb_host_poll_count,
           (unsigned long)brick6_midi_host_poll_count,
           (unsigned long)engine_tick_count);
#else
      LOGF("REF1 audio tx_half=%lu tx_full=%lu rx_half=%lu rx_full=%lu sd_rx=%lu sd_tx=%lu "
           "sd_buf0=%lu sd_buf1=%lu sd_err=%lu usb_poll=%lu midi_poll=%lu\r\n",
           (unsigned long)brick6_audio_tx_half_count,
           (unsigned long)brick6_audio_tx_full_count,
           (unsigned long)brick6_audio_rx_half_count,
           (unsigned long)brick6_audio_rx_full_count,
           (unsigned long)brick6_sd_rx_cplt_count,
           (unsigned long)brick6_sd_tx_cplt_count,
           (unsigned long)brick6_sd_buf0_cplt_count,
           (unsigned long)brick6_sd_buf1_cplt_count,
           (unsigned long)brick6_sd_err_count,
           (unsigned long)brick6_usb_host_poll_count,
           (unsigned long)brick6_midi_host_poll_count);
#endif
#endif
#endif

      snprintf(log_buffer, sizeof(log_buffer),
               "SAI TX state=%lu err=0x%08lX tx_half=%lu tx_full=%lu rx_half=%lu rx_full=%lu frames/s=%lu\r\n",
               (unsigned long)hsai_BlockA1.State,
               (unsigned long)error,
               (unsigned long)half,
               (unsigned long)full,
               (unsigned long)rx_half,
               (unsigned long)rx_full,
               (unsigned long)frames_per_sec);


      if (error != 0U && error != last_error)
      {
        snprintf(log_buffer, sizeof(log_buffer),
                 "SAI error detected: 0x%08lX\r\n", (unsigned long)error);
        uart_log(log_buffer);
        last_error = error;
      }

      last_log_tick = now;
    }

    if ((sd_test_running != 0U) && (sd_test_state == SD_TEST_STATE_MEMCHECK))
    {
      if (sd_test_failed != 0U)
      {
        if (sd_test_done_logged == 0U)
        {
          LOG("SD test FAILED\r\n");
          sd_test_done_logged = 1U;
        }
      }
      else if (SD_TEST_MEMORY_PLACEMENT != 0U)
      {
        const uint32_t *buf0 = sd_stream_get_buffer0();
        const uint32_t *buf1 = sd_stream_get_buffer1();
        const char *region0 = SD_MemoryRegion(buf0);
        const char *region1 = SD_MemoryRegion(buf1);

        LOGF("SD buf0 addr=%p region=%s\r\n", (void *)buf0, region0);
        LOGF("SD buf1 addr=%p region=%s\r\n", (void *)buf1, region1);

        if ((strcmp(region0, "DTCM") == 0) || (strcmp(region1, "DTCM") == 0))
        {
          LOG("SD buffer placement ERROR: DTCM not allowed\r\n");
          sd_test_failed = 1U;
        }

        sd_test_running = (sd_test_failed == 0U);
        sd_test_state = SD_TEST_STATE_IDLE;
      }
      else
      {
        sd_test_state = SD_TEST_STATE_IDLE;
      }
    }

    if ((sd_test_running != 0U) && (sd_test_state == SD_TEST_STATE_IDLE) && (sd_test_failed == 0U))
    {
      if ((SD_TEST_SECTOR0 != 0U) && (sd_test_sector0_done == 0U))
      {
        if (sd_stream_start_read(0U, SD_STREAM_BLOCKS_PER_BUFFER) == HAL_OK)
        {
          sd_test_state = SD_TEST_STATE_SECTOR0_WAIT;
          sd_test_timeout = now + 2000U;
          LOG("SD test sector0 start\r\n");
        }
        else
        {
          LOG("SD test sector0 FAILED to start\r\n");
          sd_test_failed = 1U;
        }
      }
      else if ((SD_TEST_KNOWN_REGION != 0U) && (sd_test_known_done == 0U))
      {
        if (sd_stream_start_read(SD_TEST_KNOWN_START_BLOCK, SD_TEST_KNOWN_BLOCKS) == HAL_OK)
        {
          sd_test_state = SD_TEST_STATE_KNOWN_WAIT;
          sd_test_timeout = now + 2000U;
          LOG("SD test known region start\r\n");
        }
        else
        {
          LOG("SD test known region FAILED to start\r\n");
          sd_test_failed = 1U;
        }
      }
      else if (SD_TEST_LONG_RUN != 0U)
      {
        if (sd_stream_start_read(0U, SD_TEST_LONG_BLOCKS) == HAL_OK)
        {
          sd_test_state = SD_TEST_STATE_LONG_RUN;
          sd_last_stats_tick = now;
          sd_last_buf0_count = 0U;
          sd_last_buf1_count = 0U;
          LOG("SD test long run start\r\n");
        }
        else
        {
          LOG("SD test long run FAILED to start\r\n");
          sd_test_failed = 1U;
        }
      }
      else
      {
        sd_test_running = 0U;
      }
    }

    if ((sd_test_running != 0U) && (sd_test_failed == 0U))
    {
      if (sd_stream_has_error())
      {
        LOG("SD stream error\r\n");
        sd_test_failed = 1U;
      }
      else if ((sd_test_state == SD_TEST_STATE_SECTOR0_WAIT) && sd_stream_is_complete())
      {
        const uint8_t *bytes = (const uint8_t *)sd_stream_get_buffer0();
        if ((bytes[510] == 0x55U) && (bytes[511] == 0xAAU))
        {
          LOG("SD sector0 signature OK (0x55AA)\r\n");
        }
        else
        {
          LOG("SD sector0 signature missing, dump first 64 bytes:\r\n");
          SD_LogHex(bytes, 64U);
        }
        sd_test_sector0_done = 1U;
        sd_test_state = SD_TEST_STATE_IDLE;
      }
      else if ((sd_test_state == SD_TEST_STATE_KNOWN_WAIT) && sd_stream_is_complete())
      {
        const uint8_t *bytes = (const uint8_t *)sd_stream_get_buffer0();
        uint32_t crc = SD_CRC32(bytes, SD_TEST_KNOWN_BLOCKS * SD_STREAM_BLOCK_SIZE_BYTES);
        LOGF("SD known region CRC32=0x%08lX\r\n", (unsigned long)crc);
        if ((SD_TEST_KNOWN_CRC32 != 0U) && (crc != SD_TEST_KNOWN_CRC32))
        {
          LOG("SD known region CRC mismatch\r\n");
          sd_test_failed = 1U;
        }
        sd_test_known_done = 1U;
        sd_test_state = SD_TEST_STATE_IDLE;
      }
      else if (sd_test_state == SD_TEST_STATE_LONG_RUN)
      {
        if ((now - sd_last_stats_tick) >= 1000U)
        {
          uint32_t buf0 = sd_stream_get_read_buf0_count();
          uint32_t buf1 = sd_stream_get_read_buf1_count();
          uint32_t delta = (buf0 - sd_last_buf0_count) + (buf1 - sd_last_buf1_count);
          uint32_t bytes = delta * SD_STREAM_BUFFER_SIZE_BYTES;
          (void)bytes;
          sd_last_buf0_count = buf0;
          sd_last_buf1_count = buf1;
          sd_last_stats_tick = now;
        }

        if (sd_stream_is_complete())
        {
          if (sd_stream_start_read(0U, SD_TEST_LONG_BLOCKS) != HAL_OK)
          {
            sd_test_failed = 1U;
          }
        }
      }
      else if ((sd_test_state == SD_TEST_STATE_SECTOR0_WAIT ||
                sd_test_state == SD_TEST_STATE_KNOWN_WAIT) && (now > sd_test_timeout))
      {
        LOG("SD test timeout\r\n");
        sd_test_failed = 1U;
      }
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

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
#if BRICK6_REFACTOR_STEP_1
    brick6_audio_tx_half_count++;
#endif
    AudioOut_ProcessHalf();
  }
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_A)
  {
#if BRICK6_REFACTOR_STEP_1
    brick6_audio_tx_full_count++;
#endif
    AudioOut_ProcessFull();
  }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
#if BRICK6_REFACTOR_STEP_1
    brick6_audio_rx_half_count++;
#endif
    AudioIn_ProcessHalf();
  }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
  if (hsai->Instance == SAI1_Block_B)
  {
#if BRICK6_REFACTOR_STEP_1
    brick6_audio_rx_full_count++;
#endif
    AudioIn_ProcessFull();
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
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
