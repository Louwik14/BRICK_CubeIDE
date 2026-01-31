#include "audio_core.h"

#include "audio_io_sd.h"
#include "audio_io_usb.h"
#include "mixer.h"

#include <string.h>

static int32_t core_input_copy[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static int32_t core_usb_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];
static int32_t core_sd_block[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];

void audio_core_init(void) {
}

void audio_core_process_block(int32_t *out, uint32_t frames) {
  if (out == NULL) {
    return;
  }

  if (frames > AUDIO_CORE_FRAMES_PER_BLOCK) {
    frames = AUDIO_CORE_FRAMES_PER_BLOCK;
  }

  size_t sample_count = (size_t)frames * AUDIO_CORE_CHANNELS;
  uint32_t usb_samples = 0U;
  uint32_t sd_samples = 0U;

  audio_buffer_t *usb_buf = audio_io_usb_get_rx_buffer();
  if (usb_buf != NULL)
  {
    uint32_t available = audio_buffer_available(usb_buf);
    if (available >= sample_count)
    {
      usb_samples = audio_buffer_read(usb_buf, core_usb_block, (uint32_t)sample_count);
    }
  }

  if (audio_io_sd_has_block())
  {
    sd_samples = audio_io_sd_read_block(core_sd_block, (uint32_t)sample_count);
  }

  if ((usb_samples == sample_count) && (sd_samples == sample_count))
  {
    mixer_mix_2_to_1(core_usb_block, core_sd_block, out, (uint32_t)sample_count);
    return;
  }

  if (usb_samples == sample_count)
  {
    memcpy(out, core_usb_block, sample_count * sizeof(int32_t));
    return;
  }

  if (sd_samples == sample_count)
  {
    memcpy(out, core_sd_block, sample_count * sizeof(int32_t));
    return;
  }

  memcpy(out, core_input_copy, sample_count * sizeof(int32_t));
}

void audio_core_on_input_block(const int32_t *data, uint32_t frames) {
  if (data == NULL) {
    return;
  }

  if (frames > AUDIO_CORE_FRAMES_PER_BLOCK) {
    frames = AUDIO_CORE_FRAMES_PER_BLOCK;
  }

  size_t sample_count = (size_t)frames * AUDIO_CORE_CHANNELS;
  memcpy(core_input_copy, data, sample_count * sizeof(int32_t));
}
