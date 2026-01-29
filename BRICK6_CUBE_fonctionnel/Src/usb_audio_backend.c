#include "usb_audio_backend.h"

#define USB_AUDIO_BACKEND_FRAME_CHANNELS 2U
#define USB_AUDIO_BACKEND_FRAMES 1024U

static int32_t usb_audio_backend_buffer[USB_AUDIO_BACKEND_FRAMES * USB_AUDIO_BACKEND_FRAME_CHANNELS];
static uint32_t usb_audio_backend_read_idx;
static uint32_t usb_audio_backend_write_idx;
static uint32_t usb_audio_backend_count;

void usb_audio_backend_init(void)
{
  usb_audio_backend_read_idx = 0U;
  usb_audio_backend_write_idx = 0U;
  usb_audio_backend_count = 0U;
}

uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames)
{
  uint32_t frames_to_pop = max_frames;
  uint32_t popped = 0U;

  if (dst == NULL || max_frames == 0U)
  {
    return 0U;
  }

  if (frames_to_pop > usb_audio_backend_count)
  {
    frames_to_pop = usb_audio_backend_count;
  }

  for (popped = 0U; popped < frames_to_pop; popped++)
  {
    uint32_t base = usb_audio_backend_read_idx * USB_AUDIO_BACKEND_FRAME_CHANNELS;
    for (uint32_t ch = 0U; ch < USB_AUDIO_BACKEND_FRAME_CHANNELS; ch++)
    {
      dst[popped * USB_AUDIO_BACKEND_FRAME_CHANNELS + ch] =
          usb_audio_backend_buffer[base + ch];
    }

    usb_audio_backend_read_idx++;
    if (usb_audio_backend_read_idx >= USB_AUDIO_BACKEND_FRAMES)
    {
      usb_audio_backend_read_idx = 0U;
    }
  }

  usb_audio_backend_count -= frames_to_pop;

  for (; popped < max_frames; popped++)
  {
    for (uint32_t ch = 0U; ch < USB_AUDIO_BACKEND_FRAME_CHANNELS; ch++)
    {
      dst[popped * USB_AUDIO_BACKEND_FRAME_CHANNELS + ch] = 0;
    }
  }

  return frames_to_pop;
}

void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames)
{
  uint32_t available;
  uint32_t frames_to_push;

  if (src == NULL || frames == 0U)
  {
    return;
  }

  available = USB_AUDIO_BACKEND_FRAMES - usb_audio_backend_count;
  frames_to_push = (frames > available) ? available : frames;

  for (uint32_t frame = 0U; frame < frames_to_push; frame++)
  {
    uint32_t base = usb_audio_backend_write_idx * USB_AUDIO_BACKEND_FRAME_CHANNELS;
    for (uint32_t ch = 0U; ch < USB_AUDIO_BACKEND_FRAME_CHANNELS; ch++)
    {
      usb_audio_backend_buffer[base + ch] =
          src[frame * USB_AUDIO_BACKEND_FRAME_CHANNELS + ch];
    }

    usb_audio_backend_write_idx++;
    if (usb_audio_backend_write_idx >= USB_AUDIO_BACKEND_FRAMES)
    {
      usb_audio_backend_write_idx = 0U;
    }
  }

  usb_audio_backend_count += frames_to_push;
}
