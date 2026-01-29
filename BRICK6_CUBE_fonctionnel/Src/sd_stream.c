/**
 * @file sd_stream.c
 * @brief Streaming SD non bloquant (DMA + FSM) pour BRICK6.
 *
 * Ce module encapsule l'accès SD en lecture/écriture par chunks DMA,
 * expose des buffers double pour les diagnostics et alimente le ring
 * buffer audio lors du streaming.
 *
 * Rôle dans le système:
 * - Fournit un backend SD asynchrone pour l'audio et les tests.
 * - Découple les callbacks IRQ de la logique de transfert.
 *
 * Contraintes temps réel:
 * - Critique audio: non (mais alimente la chaîne audio).
 * - IRQ: callbacks SD posent des flags/compteurs uniquement.
 * - Tasklet: oui (sd_tasklet_poll / bounded).
 * - Borné: oui avec budgets (STEP 6) en boucle principale.
 *
 * Architecture:
 * - Appelé par: brick6_app_init (init), diagnostics_tasklet, main loop.
 * - Appelle: HAL SD, sd_audio_block_ring, diagnostics logging.
 *
 * Règles:
 * - Pas de malloc.
 * - Pas de blocage en IRQ.
 *
 * @note L’API publique est déclarée dans sd_stream.h.
 */

#include "sd_stream.h"
#include "brick6_refactor.h"
#include "sd_audio_block_ring.h"

#include <stdio.h>
#include <string.h>

#include "sdmmc.h"

#define SD_STREAM_DATA_PATTERN_STEP 0x00010000U
#define SD_STREAM_READY_TIMEOUT_MS  1000U
#define SD_STREAM_MAX_BLOCKS_PER_TRANSFER 65535U
#define SD_STREAM_CHUNK_BLOCKS_MAX \
  ((SD_STREAM_MAX_BLOCKS_PER_TRANSFER / SD_STREAM_BLOCKS_PER_BUFFER) * SD_STREAM_BLOCKS_PER_BUFFER)

typedef enum
{
  SD_STREAM_OP_NONE = 0,
  SD_STREAM_OP_READ,
  SD_STREAM_OP_WRITE
} sd_stream_operation_t;

typedef enum
{
  SD_FSM_IDLE,
  SD_FSM_START_CHUNK,
  SD_FSM_WAIT_TRANSFER,
  SD_FSM_DONE,
  SD_FSM_ERROR
} sd_fsm_state_t;

static SD_HandleTypeDef *sd_handle = NULL;
static void (*sd_logger)(const char *message) = NULL;
static __IO uint8_t sd_log_callbacks = 0U;
static __IO uint8_t sd_rx_complete = 0U;
static __IO uint8_t sd_tx_complete = 0U;
static __IO uint8_t sd_error = 0U;
static __IO uint8_t sd_rx_done_flag = 0U;
static __IO uint8_t sd_tx_done_flag = 0U;
static __IO uint8_t sd_error_flag = 0U;
static sd_fsm_state_t sd_fsm_state = SD_FSM_IDLE;
__IO uint32_t read_buf0_count = 0U;
__IO uint32_t read_buf1_count = 0U;
static __IO uint32_t sd_write_count_buffer0 = 0U;
static __IO uint32_t sd_write_count_buffer1 = 0U;
static __IO uint32_t sd_total_blocks = 0U;
static __IO uint32_t sd_start_block = 0U;
static __IO uint32_t sd_current_block = 0U;
static __IO uint32_t sd_remaining_blocks = 0U;
static __IO uint32_t sd_active_chunk_blocks = 0U;
static uint32_t sd_write_pattern0 = 0U;
static uint32_t sd_write_pattern1 = 0U;
static sd_stream_stats_t sd_stats;
static sd_stream_operation_t sd_operation = SD_STREAM_OP_NONE;
static __IO uint8_t sd_buf0_ready = 0U;
static __IO uint8_t sd_buf1_ready = 0U;
static uint32_t sd_prefill_target = 0U;
static uint32_t sd_prefill_count = 0U;
static __IO uint8_t sd_prefill_done = 0U;

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

static void sd_stream_logf2(const char *format, uint32_t value0, uint32_t value1)
{
  if (sd_logger != NULL)
  {
    char buffer[80];
    (void)snprintf(buffer, sizeof(buffer), format, (unsigned long)value0,
                   (unsigned long)value1);
    sd_logger(buffer);
  }
}

static void sd_stream_logf3(const char *format, uint32_t value0, uint32_t value1,
                            uint32_t value2)
{
  if (sd_logger != NULL)
  {
    char buffer[96];
    (void)snprintf(buffer, sizeof(buffer), format, (unsigned long)value0,
                   (unsigned long)value1, (unsigned long)value2);
    sd_logger(buffer);
  }
}

static void sd_stream_reset_prefill(uint32_t total_blocks)
{
  uint32_t total_buffers = total_blocks / SD_STREAM_BLOCKS_PER_BUFFER;
  sd_prefill_target = (total_buffers < AUDIO_BLOCK_COUNT) ? total_buffers : AUDIO_BLOCK_COUNT;
  sd_prefill_count = 0U;
  sd_prefill_done = (sd_prefill_target == 0U) ? 1U : 0U;
}

static void sd_stream_note_prefill(void)
{
  if (sd_prefill_done == 0U)
  {
    sd_prefill_count++;
    if (sd_prefill_count >= sd_prefill_target)
    {
      sd_prefill_done = 1U;
    }
  }
}

static void sd_stream_try_produce_block(const uint8_t *source)
{
  uint8_t *write_ptr = audio_block_ring_get_write_ptr(&sd_audio_block_ring);

  if (write_ptr == NULL)
  {
    return;
  }

  memcpy(write_ptr, source, AUDIO_BLOCK_SIZE);
  audio_block_ring_produce(&sd_audio_block_ring);
  sd_stream_note_prefill();
}

static void sd_stream_process_ready_buffers(void)
{
  if (sd_operation != SD_STREAM_OP_READ)
  {
    return;
  }

  if (sd_buf0_ready != 0U)
  {
    uint32_t before = audio_block_ring_fill_level(&sd_audio_block_ring);
    sd_stream_try_produce_block((const uint8_t *)Buffer0);
    if (audio_block_ring_fill_level(&sd_audio_block_ring) != before)
    {
      sd_buf0_ready = 0U;
    }
  }

  if (sd_buf1_ready != 0U)
  {
    uint32_t before = audio_block_ring_fill_level(&sd_audio_block_ring);
    sd_stream_try_produce_block((const uint8_t *)Buffer1);
    if (audio_block_ring_fill_level(&sd_audio_block_ring) != before)
    {
      sd_buf1_ready = 0U;
    }
  }
}

static HAL_StatusTypeDef sd_stream_start_chunk(sd_stream_operation_t operation)
{
  HAL_StatusTypeDef hal_status;
  uint32_t chunk_blocks = sd_remaining_blocks;

  if ((operation == SD_STREAM_OP_NONE) || (sd_handle == NULL))
  {
    return HAL_ERROR;
  }

  if (chunk_blocks == 0U)
  {
    return HAL_ERROR;
  }

  if (chunk_blocks > SD_STREAM_CHUNK_BLOCKS_MAX)
  {
    chunk_blocks = SD_STREAM_CHUNK_BLOCKS_MAX;
  }

  if (operation == SD_STREAM_OP_READ)
  {
    uint32_t fill_level = audio_block_ring_fill_level(&sd_audio_block_ring);
    uint32_t free_blocks = (fill_level >= AUDIO_BLOCK_COUNT)
                               ? 0U
                               : (AUDIO_BLOCK_COUNT - fill_level);
    uint32_t max_chunk_blocks = free_blocks * SD_STREAM_BLOCKS_PER_BUFFER;

    if (max_chunk_blocks == 0U)
    {
      return HAL_BUSY;
    }

    if (chunk_blocks > max_chunk_blocks)
    {
      chunk_blocks = max_chunk_blocks;
    }
  }

  if (chunk_blocks == 0U)
  {
    return HAL_BUSY;
  }

  sd_active_chunk_blocks = chunk_blocks;

  if (operation == SD_STREAM_OP_READ)
  {
    hal_status = HAL_SDEx_ReadBlocksDMAMultiBuffer(sd_handle, sd_current_block, chunk_blocks);
  }
  else
  {
    hal_status = HAL_SDEx_WriteBlocksDMAMultiBuffer(sd_handle, sd_current_block, chunk_blocks);
  }

  if (hal_status != HAL_OK)
  {
    sd_stream_logf3("SD chunk start failed: hal=%lu err=%lu state=%lu\r\n",
                    (uint32_t)hal_status, (uint32_t)HAL_SD_GetError(sd_handle),
                    (uint32_t)HAL_SD_GetCardState(sd_handle));
    sd_error = 1U;
  }

  return hal_status;
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
  sd_rx_done_flag = 0U;
  sd_tx_done_flag = 0U;
  sd_error_flag = 0U;
  sd_fsm_state = SD_FSM_IDLE;
  read_buf0_count = 0U;
  read_buf1_count = 0U;
  sd_write_count_buffer0 = 0U;
  sd_write_count_buffer1 = 0U;
  sd_total_blocks = 0U;
  sd_start_block = 0U;
  sd_current_block = 0U;
  sd_remaining_blocks = 0U;
  sd_active_chunk_blocks = 0U;
  sd_operation = SD_STREAM_OP_NONE;
  memset(&sd_stats, 0, sizeof(sd_stats));
  sd_buf0_ready = 0U;
  sd_buf1_ready = 0U;
  sd_stream_reset_prefill(0U);
  audio_block_ring_init(&sd_audio_block_ring);

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
	HAL_SD_CardInfoTypeDef card_info;
	HAL_StatusTypeDef hal_status;
  uint32_t available_blocks;

  if (sd_handle == NULL)
  {
    return HAL_ERROR;
  }

  if (total_blocks == 0U)
  {
    return HAL_ERROR;
  }

  hal_status = HAL_SD_GetCardInfo(sd_handle, &card_info);
  if (hal_status != HAL_OK)
  {
    sd_stream_logf3("SD card info failed: hal=%lu err=%lu state=%lu\r\n",
                    (uint32_t)hal_status, (uint32_t)HAL_SD_GetError(sd_handle),
                    (uint32_t)HAL_SD_GetCardState(sd_handle));
    return HAL_ERROR;
  }

  if (start_block >= card_info.LogBlockNbr)
  {
    sd_stream_logf3("SD start beyond card: start=%lu total=%lu card=%lu\r\n",
                    start_block, total_blocks, card_info.LogBlockNbr);
    return HAL_ERROR;
  }

  available_blocks = card_info.LogBlockNbr - start_block;
  if (total_blocks > available_blocks)
  {
    total_blocks = available_blocks;
  }

  if (total_blocks == 0U)
  {
    sd_stream_log("SD start read rejected: zero blocks after clamp\r\n");
    return HAL_ERROR;
  }

  if ((total_blocks % SD_STREAM_BLOCKS_PER_BUFFER) != 0U)
  {
    total_blocks = (total_blocks / SD_STREAM_BLOCKS_PER_BUFFER) * SD_STREAM_BLOCKS_PER_BUFFER;
    if (total_blocks == 0U)
    {
      sd_stream_log("SD start read rejected: block count too small\r\n");
      return HAL_ERROR;
    }
  }

  sd_rx_complete = 0U;
  sd_error = 0U;
  sd_total_blocks = total_blocks;
  sd_start_block = start_block;
  sd_current_block = start_block;
  sd_remaining_blocks = total_blocks;
  sd_active_chunk_blocks = 0U;
  read_buf0_count = 0U;
  read_buf1_count = 0U;
  sd_stats.start_block = start_block;
  sd_stats.total_blocks = total_blocks;
  sd_stats.buffer0_count = 0U;
  sd_stats.buffer1_count = 0U;
  sd_operation = SD_STREAM_OP_READ;

  sd_rx_done_flag = 0U;
  sd_tx_done_flag = 0U;
  sd_error_flag = 0U;
  sd_fsm_state = SD_FSM_START_CHUNK;
  sd_buf0_ready = 0U;
  sd_buf1_ready = 0U;
  sd_stream_reset_prefill(sd_total_blocks);
  audio_block_ring_init(&sd_audio_block_ring);
  return HAL_OK;
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
  sd_current_block = start_block;
  sd_remaining_blocks = total_blocks;
  sd_active_chunk_blocks = 0U;
  sd_write_count_buffer0 = 0U;
  sd_write_count_buffer1 = 0U;
  sd_write_pattern0 = pattern0;
  sd_write_pattern1 = pattern1;
  sd_stats.start_block = start_block;
  sd_stats.total_blocks = total_blocks;
  sd_stats.buffer0_count = 0U;
  sd_stats.buffer1_count = 0U;
  sd_operation = SD_STREAM_OP_WRITE;

  Fill_Buffer(Buffer0, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t), sd_write_pattern0);
  Fill_Buffer(Buffer1, SD_STREAM_BUFFER_SIZE_BYTES / sizeof(uint32_t), sd_write_pattern1);

  sd_rx_done_flag = 0U;
  sd_tx_done_flag = 0U;
  sd_error_flag = 0U;
  sd_fsm_state = SD_FSM_START_CHUNK;
  return HAL_OK;

  return sd_stream_start_chunk(SD_STREAM_OP_WRITE);
#endif
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

static bool sd_tasklet_is_active(void)
{
  return (sd_fsm_state != SD_FSM_IDLE) &&
         (sd_fsm_state != SD_FSM_DONE) &&
         (sd_fsm_state != SD_FSM_ERROR);
}

static void sd_tasklet_step(void)
{
  sd_stream_process_ready_buffers();

  switch (sd_fsm_state)
  {
    case SD_FSM_IDLE:
      break;

    case SD_FSM_START_CHUNK:
      if ((sd_handle == NULL) || (sd_operation == SD_STREAM_OP_NONE))
      {
        sd_fsm_state = SD_FSM_IDLE;
        break;
      }
      if (HAL_SD_GetCardState(sd_handle) != HAL_SD_CARD_TRANSFER)
      {
        break;
      }
      sd_rx_done_flag = 0U;
      sd_tx_done_flag = 0U;
      {
        HAL_StatusTypeDef start_status = sd_stream_start_chunk(sd_operation);
        if (start_status == HAL_OK)
        {
          sd_fsm_state = SD_FSM_WAIT_TRANSFER;
        }
        else if (start_status == HAL_BUSY)
        {
          break;
        }
        else
        {
          sd_error_flag = 1U;
          sd_fsm_state = SD_FSM_ERROR;
        }
      }
      break;

    case SD_FSM_WAIT_TRANSFER:
      if (sd_error_flag != 0U)
      {
        sd_error_flag = 0U;
        sd_error = 1U;
        sd_operation = SD_STREAM_OP_NONE;
        sd_fsm_state = SD_FSM_ERROR;
        break;
      }

      if ((sd_operation == SD_STREAM_OP_READ && sd_rx_done_flag == 0U) ||
          (sd_operation == SD_STREAM_OP_WRITE && sd_tx_done_flag == 0U))
      {
        break;
      }

      if (sd_operation == SD_STREAM_OP_READ)
      {
        sd_rx_done_flag = 0U;
      }
      else
      {
        sd_tx_done_flag = 0U;
      }

      sd_current_block += sd_active_chunk_blocks;
      sd_remaining_blocks -= sd_active_chunk_blocks;

      if (sd_remaining_blocks > 0U)
      {
        sd_fsm_state = SD_FSM_START_CHUNK;
      }
      else
      {
        if (sd_operation == SD_STREAM_OP_READ)
        {
          sd_rx_complete = 1U;
        }
        else
        {
          sd_tx_complete = 1U;
        }
        sd_operation = SD_STREAM_OP_NONE;
        sd_fsm_state = SD_FSM_DONE;
      }
      break;

    case SD_FSM_DONE:
      break;

    case SD_FSM_ERROR:
      break;
  }
}

void sd_tasklet_poll(void)
{
  sd_tasklet_step();
}

void sd_tasklet_poll_bounded(uint32_t max_steps)
{
  uint32_t n = 0U;
  for (; n < max_steps; n++)
  {
    sd_tasklet_step();
    if (!sd_tasklet_is_active())
    {
      break;
    }
  }

  if ((max_steps > 0U) && (n >= max_steps) && sd_tasklet_is_active())
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    sd_budget_hit_count++;
#endif
  }
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_sd_rx_cplt_count++;
#endif
    sd_rx_done_flag = 1U;
  }
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_sd_tx_cplt_count++;
#endif
    sd_tx_done_flag = 1U;
  }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
  if (hsd == sd_handle)
  {
#if BRICK6_ENABLE_DIAGNOSTICS
    brick6_sd_err_count++;
#endif
    sd_error = 1U;
    sd_error_flag = 1U;
  }
}

void HAL_SDEx_Read_DMADoubleBuffer0CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

#if BRICK6_ENABLE_DIAGNOSTICS
  brick6_sd_buf0_cplt_count++;
#endif
  read_buf0_count++;
  sd_stats.buffer0_count = read_buf0_count;
  if (sd_operation == SD_STREAM_OP_READ)
  {
    sd_buf0_ready = 1U;
  }
  (void)sd_log_callbacks;
}

void HAL_SDEx_Read_DMADoubleBuffer1CpltCallback(SD_HandleTypeDef *hsd)
{
  if (hsd != sd_handle)
  {
    return;
  }

#if BRICK6_ENABLE_DIAGNOSTICS
  brick6_sd_buf1_cplt_count++;
#endif
  read_buf1_count++;
  sd_stats.buffer1_count = read_buf1_count;
  if (sd_operation == SD_STREAM_OP_READ)
  {
    sd_buf1_ready = 1U;
  }
  (void)sd_log_callbacks;
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
