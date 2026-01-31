/**
 * @file diagnostics_tasklet.c
 * @brief Tasklet de diagnostics (logs 1 Hz, tests SDRAM/SD, stats).
 *
 * Ce module centralise les logs périodiques, l'affichage des compteurs
 * d'instrumentation et les tests SD/SDRAM hors IRQ.
 *
 * Rôle dans le système:
 * - Observabilité: logs de charge, erreurs et stats.
 * - Tests intégrés (SD, SDRAM alloc) pour validation système.
 *
 * Contraintes temps réel:
 * - Critique audio: non.
 * - Tasklet: oui (appelé dans la boucle principale).
 * - IRQ: non.
 * - Borné: partiellement (cadence 1 Hz, tests séquencés).
 *
 * Architecture:
 * - Appelé par: main loop (diagnostics_tasklet_poll), brick6_app_init (init SD).
 * - Appelle: sd_stream, audio_in/out stats, sdram_alloc, UART logs.
 *
 * Règles:
 * - Pas de malloc.
 * - Ne pas appeler depuis une IRQ.
 *
 * @note L’API publique est déclarée dans diagnostics_tasklet.h.
 */

#include "diagnostics_tasklet.h"

#include "audio_io_sai.h"
#include "brick6_refactor.h"
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

static void uart_log(const char *message)
{
  (void)HAL_UART_Transmit(&huart1, (uint8_t *)message, (uint16_t)strlen(message), 10);
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

static void SDRAM_Alloc_Test_Stop(uint32_t index, uint32_t got, uint32_t expected)
{
  LOGF("SDRAM alloc test FAILED idx=%lu got=0x%08lX expected=0x%08lX\r\n",
       (unsigned long)index,
       (unsigned long)got,
       (unsigned long)expected);
}

void diagnostics_sdram_alloc_test(void)
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
    return;
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
      return;
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
      return;
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

void diagnostics_on_sd_stream_init(HAL_StatusTypeDef status)
{
  sd_test_running = 0U;
  sd_test_done_logged = 0U;
  sd_test_failed = 0U;
  sd_test_state = SD_TEST_STATE_IDLE;
  sd_test_sector0_done = 0U;
  sd_test_known_done = 0U;

  if (status == HAL_OK)
  {
    LOG("SD stream init OK\r\n");
    sd_stream_set_logger(diagnostics_log);
    sd_stream_set_callback_logging(SD_TEST_LOG_CALLBACKS != 0U);
    sd_test_running = (SD_TEST_ENABLE != 0U);
    sd_test_state = SD_TEST_STATE_MEMCHECK;
  }
  else
  {
    LOG("SD stream init FAILED\r\n");
  }
}

void diagnostics_tasklet_poll(void)
{
  static uint32_t last_led_tick = 0U;
  static uint32_t last_log_tick = 0U;
  static uint32_t last_error = 0U;
  char log_buffer[128];

  uint32_t now = HAL_GetTick();

  if ((now - last_led_tick) >= 500U)
  {
    HAL_GPIO_TogglePin(LED_DEBUG_GPIO_Port, LED_DEBUG_Pin);
    last_led_tick = now;
  }

  if ((now - last_log_tick) >= 1000U)
  {
    uint32_t error = HAL_SAI_GetError(&hsai_BlockA1);
    uint32_t half = audio_io_sai_get_tx_half_events();
    uint32_t full = audio_io_sai_get_tx_full_events();
    uint32_t rx_half = audio_io_sai_get_rx_half_events();
    uint32_t rx_full = audio_io_sai_get_rx_full_events();

#if BRICK6_ENABLE_DIAGNOSTICS
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
#endif

    if (error != 0U && error != last_error)
    {
      snprintf(log_buffer, sizeof(log_buffer),
               "SAI error detected: 0x%08lX\r\n", (unsigned long)error);
      uart_log(log_buffer);
      last_error = error;
    }

    (void)half;
    (void)full;
    (void)rx_half;
    (void)rx_full;
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
}
