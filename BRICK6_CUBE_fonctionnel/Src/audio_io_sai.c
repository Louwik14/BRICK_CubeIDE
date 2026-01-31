#include "audio_io_sai.h"

#include "audio_in.h"
#include "audio_out.h"

const int32_t *audio_io_sai_get_rx_block(void)
{
  return AudioIn_GetLatestBlock();
}

int32_t *audio_io_sai_get_tx_block(void)
{
  return AudioOut_GetBuffer();
}

uint32_t audio_io_sai_get_frames(void)
{
  return AUDIO_OUT_FRAMES_PER_HALF;
}
