#include "sd_stream.h"

#include <stdio.h>
#include <string.h>

#include "sdmmc.h"

#define SD_STREAM_DATA_PATTERN_STEP 0x00010000U

static SD_HandleTypeDef *sd_handle = NULL;
static void (*sd_logger)(const char *message) = NULL;
static __IO uint8_t sd_log_callbacks = 0U;
static __IO uint8_t sd_rx_complete = 0U;
static __IO uint8_t sd_tx_complete = 0U;
static __IO uint8_t sd_error = 0U;
__IO uint32_t read_buf0_count = 0U;
__IO uint32_t read_buf1_count = 0U;
static __IO uint32_t sd_write_count_buffer0 = 0U;
static __IO uint32_t sd_write_count_buffer1 = 0U;
static __IO uint32_t sd_total_blocks = 0U;
static __IO uint32_t sd_start_block = 0U;
static uint32_t sd_write_pattern0 = 0U;
static uint32_t sd_write_pattern1 = 0U;
static sd_stream_stats_t sd_stats;

static uint32_t Buffer0[SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t)]
  __attribute__((section(".ram_d1"), aligned(32)));
static uint32_t Buffer1[SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t)]
  __attribute__((section(".ram_d1"), aligned(32)));

static void Fill_Buffer(uint32_t *pBuffer, uint32_t buffer_length, uint32_t offset)
{
  for (uint32_t index = 0; index < buffer_length; index++)
  {
    pBuffer[index] = index + offset;
  }
}

static void sd_stream_log(const char *message)
{
  if (sd_logger != NULL)
  {
    sd_logger(message);
  }
}

static void sd_stream_logf(const char *format, uint32_t value)
{
  if (sd_logger != NULL)
  {
    char buffer[64];
    (void)snprintf(buffer, sizeof(buffer), format, (unsigned long)value);
    sd_logger(buffer);
  }
}

static HAL_StatusTypeDef Wait_SDCARD_Ready(void)
{
  uint32_t timeout = HAL_GetTick() + 1000U;

  while (HAL_SD_GetCardState(sd_handle) != HAL_SD_CARD_TRANSFER)
  {
    if (HAL_GetTick() > timeout)
    {
      return HAL_TIMEOUT;
    }
  }
  return HAL_OK;
}

HAL_StatusTypeDef sd_stream_init(SD_HandleTypeDef *hsd)
{
  if (hsd == NULL)
  {
    return HAL_ERROR;
  }

  sd_handle = hsd;
  sd_error = 0U;
  sd_rx_complete = 0U;
  sd_tx_complete = 0U;
  read_buf0_count = 0U;
  read_buf1_count = 0U;
  sd_write_count_buffer0 = 0U;
  sd_write_count_buffer1 = 0U;
  sd_total_blocks = 0U;
  sd_start_block = 0U;
  memset(&sd_stats, 0, sizeof(sd_stats));

  return HAL_SDEx_ConfigDMAMultiBuffer(sd_handle, Buffer0, Buffer1,
                                       SD_STREAM_BLOCKS_PER_BUFFER);
}

void sd_stream_set_logger(void (*logger)(const char *message))
{
  sd_logger = logger;
}

void sd_stream_set_callback_logging(bool enable)
{
  sd_log_callbacks = enable ? 1U : 0U;
}

HAL_StatusTypeDef sd_stream_start_read(uint32_t start_block, uint32_t total_blocks)
{
  if (sd_handle == NULL)
  {
    return HAL_ERROR;
  }

  if ((total_blocks == 0U) || ((total_blocks % SD_STREAM_BLOCKS_PER_BUFFER) != 0U))
  {
    return HAL_ERROR;
  }

  sd_rx_complete = 0U;
  sd_error = 0U;
  sd_total_blocks = total_blocks;
  sd_start_block = start_block;
  read_buf0_count = 0U;
  read_buf1_count = 0U;
  sd_stats.start_block = start_block;
  sd_stats.total_blocks = total_blocks;
  sd_stats.buffer0_count = 0U;
  sd_stats.buffer1_count = 0U;

  if (Wait_SDCARD_Ready() != HAL_OK)
  {
    sd_error = 1U;
    return HAL_ERROR;
  }

  return HAL_SDEx_ReadBlocksDMAMultiBuffer(sd_handle, sd_start_block, sd_total_blocks);
}

HAL_StatusTypeDef sd_stream_start_write(uint32_t start_block, uint32_t total_blocks,
                                        uint32_t pattern0, uint32_t pattern1)
{
  if (sd_handle == NULL)
  {
    return HAL_ERROR;
  }

  if ((total_blocks == 0U) || ((total_blocks % SD_STREAM_BLOCKS_PER_BUFFER) != 0U))
  {
    return HAL_ERROR;
  }

  sd_tx_complete = 0U;
  sd_error = 0U;
  sd_total_blocks = total_blocks;
  sd_start_block = start_block;
  sd_write_count_buffer0 = 0U;
  sd_write_count_buffer1 = 0U;
  sd_write_pattern0 = pattern0;
  sd_write_pattern1 = pattern1;
  sd_stats.start_block = start_block;
  sd_stats.total_blocks = total_blocks;
  sd_stats.buffer0_count = 0U;
  sd_stats.buffer1_count = 0U;

  Fill_Buffer(Buffer0, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t), sd_write_pattern0);
  Fill_Buffer(Buffer1, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t), sd_write_pattern1);

  if (Wait_SDCARD_Ready() != HAL_OK)
  {
    sd_error = 1U;
    return HAL_ERROR;
  }

  return HAL_SDEx_WriteBlocksDMAMultiBuffer(sd_handle, sd_start_block, sd_total_blocks);
}

bool sd_stream_is_busy(void)
{
  if (sd_handle == NULL)
  {
    return false;
  }
  return (sd_handle->State != HAL_SD_STATE_READY);
}

bool sd_stream_is_complete(void)
{
  return ((sd_rx_complete != 0U) || (sd_tx_complete != 0U));
}

bool sd_stream_has_error(void)
{
  return (sd_error != 0U);
}

const sd_stream_stats_t *sd_stream_get_stats(void)
{
  return &sd_stats;
}

uint32_t sd_stream_get_read_buf0_count(void)
{
  return read_buf0_count;
}

uint32_t sd_stream_get_read_buf1_count(void)
{
  return read_buf1_count;
}

const uint32_t *sd_stream_get_buffer0(void)
{
  return Buffer0;
}

const uint32_t *sd_stream_get_buffer1(void)
{
  return Buffer1;
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
    sd_rx_complete = 1U;
  }
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
    sd_tx_complete = 1U;
  }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
    sd_error = 1U;
  }
}

void HAL_SDEx_Read_DMADoubleBuffer0CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

  read_buf0_count++;
  sd_stats.buffer0_count = read_buf0_count;
  if (sd_log_callbacks != 0U)
  {
    sd_stream_logf("SD buf0 done %lu\r\n", read_buf0_count);
  }
}

void HAL_SDEx_Read_DMADoubleBuffer1CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

  read_buf1_count++;
  sd_stats.buffer1_count = read_buf1_count;
  if (sd_log_callbacks != 0U)
  {
    sd_stream_logf("SD buf1 done %lu\r\n", read_buf1_count);
  }
}

void HAL_SDEx_Write_DMADoubleBuffer0CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

  sd_write_count_buffer0++;
  sd_stats.buffer0_count = sd_write_count_buffer0;
  Fill_Buffer(Buffer0, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t),
              sd_write_pattern0 + (sd_write_count_buffer0 * SD_STREAM_DATA_PATTERN_STEP));
}

void HAL_SDEx_Write_DMADoubleBuffer1CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

  sd_write_count_buffer1++;
  sd_stats.buffer1_count = sd_write_count_buffer1;
  Fill_Buffer(Buffer1, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t),
              sd_write_pattern1 + (sd_write_count_buffer1 * SD_STREAM_DATA_PATTERN_STEP));
}
