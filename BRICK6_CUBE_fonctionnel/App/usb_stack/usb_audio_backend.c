#include "usb_audio_backend.h"

void usb_audio_backend_init(void)
{
}

uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames)
{
  (void)dst;
  (void)max_frames;
  return 0U;
}

void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames)
{
  (void)src;
  (void)frames;
}
