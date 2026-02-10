#include "audio.h"
#include "audio_float.h"   /* <-- NEW : float engine boundary */

#include <string.h>
#include <stdint.h>

/* ============================================================
   CONFIG AUDIO : STM32H743 + CS42448 TDM8
   ============================================================ */

/* TDM8 = 8 slots x 32-bit */
#define AUDIO_TDM_SLOTS          8

/* Block size : frames per half-buffer
   (Must match AUDIO_BLOCK_SIZE in audio_float.c)
*/
#define AUDIO_FRAMES_PER_HALF    32

/* Double buffer DMA */
#define AUDIO_FRAMES_TOTAL       (AUDIO_FRAMES_PER_HALF * 2)

/* One frame = 8 slots */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Total DMA buffer size (int32 words) */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)

/* ============================================================
   DMA BUFFERS
   ============================================================ */

static int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static int32_t tx_buffer[AUDIO_BUFFER_WORDS];

/* ============================================================
   SAI HANDLES
   ============================================================ */

static SAI_HandleTypeDef *sai_tx = NULL;
static SAI_HandleTypeDef *sai_rx = NULL;

/* ============================================================
   INTERNAL PROCESSING
   Hardware layer only: calls float engine
   ============================================================ */

static void process_half(uint32_t half_index)
{
    uint32_t offset =
        half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;

    int32_t *rx = &rx_buffer[offset];
    int32_t *tx = &tx_buffer[offset];

    /* Always run float engine boundary */
    audio_process_block_int32(rx, tx, AUDIO_FRAMES_PER_HALF);
}

/* ============================================================
   API
   ============================================================ */

void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx)
{
    sai_tx = hsai_tx;
    sai_rx = hsai_rx;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));
}

void audio_start(void)
{
    if (!sai_tx || !sai_rx)
        return;

    /* Start RX first */
    HAL_SAI_Receive_DMA(sai_rx,
                       (uint8_t *)rx_buffer,
                       AUDIO_BUFFER_WORDS);

    /* Then start TX */
    HAL_SAI_Transmit_DMA(sai_tx,
                        (uint8_t *)tx_buffer,
                        AUDIO_BUFFER_WORDS);
}

/* ============================================================
   DMA IRQ CALLBACKS : AUDIO RUNS HERE
   ============================================================ */

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == sai_rx)
    {
        process_half(0);
    }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == sai_rx)
    {
        process_half(1);
    }
}
