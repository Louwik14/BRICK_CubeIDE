/**
 * @file audio_core.c
 * @brief Moteur audio central (tasklet, DMA sync).
 */

#include "audio_core.h"
#include "audio_buffer.h"
#include "audio_io_sai.h"
#include "audio_io_sd.h"
#include "audio_io_usb.h"
#include "engine_tasklet.h"
#include "fx_chain.h"
#include "mixer.h"
#include "routing.h"
#include "sdram.h"
#include "sdram_alloc.h"
#include <string.h>

enum
{
  AUDIO_USB_RX_BLOCKS = 12U,
  AUDIO_USB_TX_BLOCKS = 8U,
  AUDIO_SD_BLOCKS = 16U
};

static int32_t audio_core_input[AUDIO_CORE_SAMPLES_PER_BLOCK]
    __attribute__((section(".ram_d1"), aligned(32)));
static int32_t audio_core_mix[AUDIO_CORE_SAMPLES_PER_BLOCK]
    __attribute__((section(".ram_d1"), aligned(32)));
static int32_t audio_core_output[AUDIO_CORE_SAMPLES_PER_BLOCK]
    __attribute__((section(".ram_d1"), aligned(32)));

static int32_t audio_usb_block[AUDIO_USB_CHANNELS * AUDIO_CORE_FRAMES_PER_BLOCK];
static int32_t audio_sd_block[AUDIO_SD_CHANNELS * AUDIO_CORE_FRAMES_PER_BLOCK];

static audio_buffer_t audio_usb_rx_ring;
static audio_buffer_t audio_usb_tx_ring;
static audio_buffer_t audio_sd_ring;

static fx_chain_t audio_fx_chain;

static int32_t audio_core_saturate(int64_t value)
{
  if (value > 0x7FFFFF00)
  {
    return 0x7FFFFF00;
  }
  if (value < (int32_t)0x80000000)
  {
    return (int32_t)0x80000000;
  }
  return (int32_t)value;
}

static void audio_core_prepare_rings(void)
{
  SDRAM_Alloc_Init(SDRAM_BANK_ADDR, SDRAM_ALLOC_DEFAULT_SIZE_BYTES);

  uint32_t usb_rx_samples = AUDIO_USB_RX_BLOCKS * AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_USB_CHANNELS;
  uint32_t usb_tx_samples = AUDIO_USB_TX_BLOCKS * AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_USB_CHANNELS;
  uint32_t sd_samples = AUDIO_SD_BLOCKS * AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_SD_CHANNELS;

  (void)audio_buffer_init(&audio_usb_rx_ring, usb_rx_samples, 4U);
  (void)audio_buffer_init(&audio_usb_tx_ring, usb_tx_samples, 4U);
  (void)audio_buffer_init(&audio_sd_ring, sd_samples, 4U);
}

void audio_core_init(SAI_HandleTypeDef *tx_sai, SAI_HandleTypeDef *rx_sai)
{
  memset(audio_core_input, 0, sizeof(audio_core_input));
  memset(audio_core_mix, 0, sizeof(audio_core_mix));
  memset(audio_core_output, 0, sizeof(audio_core_output));

  audio_core_prepare_rings();
  audio_io_usb_init(&audio_usb_rx_ring, &audio_usb_tx_ring, AUDIO_CORE_SAMPLE_RATE);
  audio_io_sd_init(&audio_sd_ring);
  audio_io_sai_init(tx_sai, rx_sai);

  fx_chain_init(&audio_fx_chain);
  (void)fx_chain_init_delay(&audio_fx_chain, AUDIO_CORE_SAMPLE_RATE / 2U);
}

void audio_core_start(void)
{
  audio_io_sai_start();
}

static void audio_core_process_block(uint32_t half_index)
{
  uint32_t rx_half = 0U;
  if (audio_io_sai_take_rx_half(&rx_half))
  {
    audio_io_sai_copy_rx_half(rx_half, audio_core_input, AUDIO_CORE_FRAMES_PER_BLOCK);
  }
  else
  {
    memset(audio_core_input, 0, sizeof(audio_core_input));
  }

  mixer_clear(audio_core_mix, AUDIO_CORE_SAMPLES_PER_BLOCK);
  mixer_add(audio_core_mix, audio_core_input, AUDIO_CORE_SAMPLES_PER_BLOCK, 1.0f);

  uint32_t usb_frames = audio_io_usb_pop_rx(audio_usb_block, AUDIO_CORE_FRAMES_PER_BLOCK);
  if (usb_frames < AUDIO_CORE_FRAMES_PER_BLOCK)
  {
    uint32_t missing = (AUDIO_CORE_FRAMES_PER_BLOCK - usb_frames) * AUDIO_USB_CHANNELS;
    memset(&audio_usb_block[usb_frames * AUDIO_USB_CHANNELS], 0, missing * sizeof(int32_t));
  }

  for (uint32_t frame = 0; frame < AUDIO_CORE_FRAMES_PER_BLOCK; ++frame)
  {
    uint32_t mix_base = frame * AUDIO_CORE_CHANNELS;
    uint32_t usb_base = frame * AUDIO_USB_CHANNELS;
    audio_core_mix[mix_base + 0] =
        audio_core_saturate((int64_t)audio_core_mix[mix_base + 0] + audio_usb_block[usb_base + 0]);
    audio_core_mix[mix_base + 1] =
        audio_core_saturate((int64_t)audio_core_mix[mix_base + 1] + audio_usb_block[usb_base + 1]);
  }

  uint32_t sd_frames = audio_io_sd_pop(audio_sd_block, AUDIO_CORE_FRAMES_PER_BLOCK);
  if (sd_frames < AUDIO_CORE_FRAMES_PER_BLOCK)
  {
    uint32_t missing = (AUDIO_CORE_FRAMES_PER_BLOCK - sd_frames) * AUDIO_SD_CHANNELS;
    memset(&audio_sd_block[sd_frames * AUDIO_SD_CHANNELS], 0, missing * sizeof(int32_t));
  }

  for (uint32_t frame = 0; frame < AUDIO_CORE_FRAMES_PER_BLOCK; ++frame)
  {
    uint32_t mix_base = frame * AUDIO_CORE_CHANNELS;
    uint32_t sd_base = frame * AUDIO_SD_CHANNELS;
    audio_core_mix[mix_base + 2] =
        audio_core_saturate((int64_t)audio_core_mix[mix_base + 2] + audio_sd_block[sd_base + 0]);
    audio_core_mix[mix_base + 3] =
        audio_core_saturate((int64_t)audio_core_mix[mix_base + 3] + audio_sd_block[sd_base + 1]);
  }

  fx_chain_process(&audio_fx_chain, audio_core_mix, AUDIO_CORE_FRAMES_PER_BLOCK, AUDIO_CORE_CHANNELS);
  routing_apply(audio_core_mix, audio_core_output, AUDIO_CORE_FRAMES_PER_BLOCK, AUDIO_CORE_CHANNELS);

  audio_io_sai_copy_tx_half(half_index, audio_core_output, AUDIO_CORE_FRAMES_PER_BLOCK);

  audio_io_usb_push_tx(audio_core_output, AUDIO_CORE_FRAMES_PER_BLOCK);

  engine_tasklet_notify_frames(AUDIO_CORE_FRAMES_PER_BLOCK);
}

void audio_tasklet_poll(void)
{
  uint32_t half_index = 0U;
  while (audio_io_sai_take_tx_half(&half_index))
  {
    audio_core_process_block(half_index);
  }
}
