#pragma once

#include "brick6_refactor.h"
#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SD_STREAM_BLOCK_SIZE_BYTES      512U
#define SD_STREAM_BUFFER_SIZE_BYTES     4096U
#define SD_STREAM_BLOCKS_PER_BUFFER     (SD_STREAM_BUFFER_SIZE_BYTES / SD_STREAM_BLOCK_SIZE_BYTES)

typedef struct
{
  uint32_t start_block;
  uint32_t total_blocks;
  uint32_t buffer0_count;
  uint32_t buffer1_count;
} sd_stream_stats_t;

extern __IO uint32_t read_buf0_count;
extern __IO uint32_t read_buf1_count;

HAL_StatusTypeDef sd_stream_init(SD_HandleTypeDef *hsd);
HAL_StatusTypeDef sd_stream_start_read(uint32_t start_block, uint32_t total_blocks);
HAL_StatusTypeDef sd_stream_start_write(uint32_t start_block, uint32_t total_blocks,
                                        uint32_t pattern0, uint32_t pattern1);

void sd_stream_set_logger(void (*logger)(const char *message));
void sd_stream_set_callback_logging(bool enable);

bool sd_stream_is_busy(void);
bool sd_stream_is_complete(void);
bool sd_stream_has_error(void);
const sd_stream_stats_t *sd_stream_get_stats(void);
uint32_t sd_stream_get_read_buf0_count(void);
uint32_t sd_stream_get_read_buf1_count(void);
const uint32_t *sd_stream_get_buffer0(void);
const uint32_t *sd_stream_get_buffer1(void);
void sd_tasklet_poll(void);
void sd_tasklet_poll_bounded(uint32_t max_steps);

#ifdef __cplusplus
}
#endif
