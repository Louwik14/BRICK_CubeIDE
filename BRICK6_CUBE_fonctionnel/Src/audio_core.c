#include "audio_core.h"

#include "audio_io_usb.h"

#include <string.h>

static int32_t core_input_copy[AUDIO_CORE_FRAMES_PER_BLOCK * AUDIO_CORE_CHANNELS];

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
  audio_buffer_t *usb_buf = audio_io_usb_get_rx_buffer();
  if (usb_buf != NULL) {
    uint32_t available = audio_buffer_available(usb_buf);
    if (available >= sample_count) {
      (void)audio_buffer_read(usb_buf, out, (uint32_t)sample_count);
      return;
    }
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
