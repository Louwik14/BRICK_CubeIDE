#pragma once
#include "stm32h7xx_hal.h"
#include <stdint.h>

/* Init + start */
void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx);

void audio_start(void);

/* User DSP callback */
typedef void (*audio_process_fn)(int32_t *rx,
                                int32_t *tx,
                                uint32_t frames);

void audio_set_process_callback(audio_process_fn cb);
