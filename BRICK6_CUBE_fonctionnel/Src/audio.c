#include "audio.h"
#include <string.h>

/* ===== CONFIG ===== */

/* TDM8 = 8 slots x 32-bit */
#define AUDIO_TDM_SLOTS          8
#define AUDIO_WORD_SIZE_BYTES    4

/* Nombre de frames par demi-buffer */
#define AUDIO_FRAMES_PER_HALF    64

/* Total frames dans buffer DMA = 2 halves */
#define AUDIO_FRAMES_TOTAL       (AUDIO_FRAMES_PER_HALF * 2)

/* Un frame = 8 slots */
#define AUDIO_WORDS_PER_FRAME    AUDIO_TDM_SLOTS

/* Taille totale du buffer en int32 */
#define AUDIO_BUFFER_WORDS       (AUDIO_FRAMES_TOTAL * AUDIO_WORDS_PER_FRAME)

/* ===== BUFFERS DMA ===== */

static int32_t rx_buffer[AUDIO_BUFFER_WORDS];
static int32_t tx_buffer[AUDIO_BUFFER_WORDS];

/* ===== HANDLES ===== */

static SAI_HandleTypeDef *sai_tx = NULL;
static SAI_HandleTypeDef *sai_rx = NULL;

/* ===== FLAGS ===== */

static volatile uint8_t half_ready = 0;
static volatile uint8_t full_ready = 0;

static volatile uint32_t half_events = 0;
static volatile uint32_t full_events = 0;

/* ===== INTERNAL ===== */

static void copy_half(uint32_t half_index)
{
    uint32_t offset = half_index * AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;
    uint32_t count  = AUDIO_FRAMES_PER_HALF * AUDIO_WORDS_PER_FRAME;

    /* Loopback brut : RX -> TX */
    memcpy(&tx_buffer[offset], &rx_buffer[offset], count * sizeof(int32_t));
}

/* ===== API ===== */

void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx)
{
    sai_tx = hsai_tx;
    sai_rx = hsai_rx;

    memset(rx_buffer, 0, sizeof(rx_buffer));
    memset(tx_buffer, 0, sizeof(tx_buffer));

    half_ready = 0;
    full_ready = 0;
    half_events = 0;
    full_events = 0;
}

void audio_start(void)
{
    if (!sai_tx || !sai_rx)
        return;

    /* Start RX first */
    HAL_SAI_Receive_DMA(sai_rx,
                       (uint8_t *)rx_buffer,
                       AUDIO_BUFFER_WORDS);

    /* Start TX */
    HAL_SAI_Transmit_DMA(sai_tx,
                        (uint8_t *)tx_buffer,
                        AUDIO_BUFFER_WORDS);
}

void audio_poll(void)
{
    if (half_ready)
    {
        half_ready = 0;
        copy_half(0);
    }

    if (full_ready)
    {
        full_ready = 0;
        copy_half(1);
    }
}

/* ===== CALLBACKS IRQ ===== */

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == sai_rx)
    {
        half_ready = 1;
        half_events++;
    }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == sai_rx)
    {
        full_ready = 1;
        full_events++;
    }
}

/* ===== DEBUG ===== */

uint32_t audio_get_half_events(void)
{
    return half_events;
}

uint32_t audio_get_full_events(void)
{
    return full_events;
}
