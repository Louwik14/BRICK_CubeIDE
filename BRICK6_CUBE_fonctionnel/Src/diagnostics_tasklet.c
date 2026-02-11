/**
 * @file diagnostics_tasklet.c
 * @brief Tasklet de diagnostics (logs 1 Hz, tests SDRAM/SD, stats).
 *
 * Nettoyé : suppression des anciens hooks AudioIn/AudioOut obsolètes.
 */

#include "diagnostics_tasklet.h"

#include "brick6_refactor.h"
#include "cs42448.h"
#include "engine_tasklet.h"
#include "main.h"
#include "sai.h"
#include "sd_stream.h"
#include "sdram.h"
#include "sdram_alloc.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ============================================================
   SD test state machine
   ============================================================ */

typedef enum
{
  SD_TEST_STATE_IDLE = 0,
  SD_TEST_STATE_MEMCHECK,
  SD_TEST_STATE_SECTOR0_WAIT,
  SD_TEST_STATE_KNOWN_WAIT,
  SD_TEST_STATE_LONG_RUN
} sd_test_state_t;

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

#ifndef DIAGNOSTICS_CODEC_ENABLE
#define DIAGNOSTICS_CODEC_ENABLE 1U
#endif

/* ============================================================
   Internal state
   ============================================================ */

static uint8_t diagnostics_codec_dump_pending = 1U;

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

/* ============================================================
   UART logging
   ============================================================ */

static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1,
                         (uint8_t *)message,
                         (uint16_t)strlen(message),
                         10);
}

void diagnostics_log(const char *message)
{
  uart_log(message);
}

void diagnostics_logf(const char *fmt, ...)
{
  char buffer[256];
  va_list args;

  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  uart_log(buffer);
}

#define LOG(msg) diagnostics_log(msg)
#define LOGF(fmt, ...) diagnostics_logf(fmt, __VA_ARGS__)

/* ============================================================
   Codec dump trigger
   ============================================================ */

void diagnostics_request_codec_dump(void)
{
  diagnostics_codec_dump_pending = 1U;
}

/* ============================================================
   SDRAM alloc test
   ============================================================ */

static void SDRAM_Alloc_Test_Stop(uint32_t index,
                                 uint32_t got,
                                 uint32_t expected)
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

void diagnostics_sdram_alloc_test(void)
{
  const uint32_t block1_size = 1024U * 1024U;
  const uint32_t block2_size = 512U * 1024U;

  SDRAM_Alloc_Reset();

  uint16_t *block16 = (uint16_t *)SDRAM_Alloc(block1_size, 2U);
  uint32_t *block32 = (uint32_t *)SDRAM_Alloc(block2_size, 4U);

  LOGF("SDRAM alloc block1=%p size=%lu\r\n",
       (void *)block16,
       (unsigned long)block1_size);

  LOGF("SDRAM alloc block2=%p size=%lu\r\n",
       (void *)block32,
       (unsigned long)block2_size);

  if ((block16 == NULL) || (block32 == NULL))
  {
    LOG("SDRAM alloc test FAILED: out of memory\r\n");
    while (1)
      HAL_Delay(100);
  }

  uint32_t count16 = block1_size / sizeof(uint16_t);
  for (uint32_t i = 0; i < count16; i++)
    block16[i] = (uint16_t)(0xA500U ^ (uint16_t)i);

  for (uint32_t i = 0; i < count16; i++)
  {
    uint16_t expected = (uint16_t)(0xA500U ^ (uint16_t)i);
    if (block16[i] != expected)
      SDRAM_Alloc_Test_Stop(i, block16[i], expected);
  }

  uint32_t count32 = block2_size / sizeof(uint32_t);
  uint32_t base_index =
      ((uint32_t)(uintptr_t)block32 - SDRAM_BANK_ADDR) / sizeof(uint32_t);

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
      SDRAM_Alloc_Test_Stop(i, read_value, expected);
  }

  LOG("SDRAM alloc test OK\r\n");
}

/* ============================================================
   Diagnostics main poll
   ============================================================ */

void diagnostics_tasklet_poll(void)
{
  static uint32_t last_led_tick = 0U;
  static uint32_t last_log_tick = 0U;
  static uint32_t last_error = 0U;

  uint32_t now = HAL_GetTick();

  if ((now - last_led_tick) >= 500U)
  {
    HAL_GPIO_TogglePin(LED_DEBUG_GPIO_Port, LED_DEBUG_Pin);
    last_led_tick = now;
  }

  if ((now - last_log_tick) >= 1000U)
  {
    uint32_t error = HAL_SAI_GetError(&hsai_BlockA1);



    if (error != 0U && error != last_error)
    {
      char buf[80];
      snprintf(buf, sizeof(buf),
               "SAI error detected: 0x%08lX\r\n",
               (unsigned long)error);
      uart_log(buf);
      last_error = error;
    }

    last_log_tick = now;
  }

#if DIAGNOSTICS_CODEC_ENABLE
  if (diagnostics_codec_dump_pending != 0U)
  {
    diagnostics_codec_dump_pending = 0U;
    CS42448_DiagnosticsDump(CS42448_I2C_ADDR);
  }
#endif
}
