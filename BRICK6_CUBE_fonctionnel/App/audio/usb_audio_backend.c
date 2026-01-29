#include "usb_audio_backend.h"

#define USB_AUDIO_BACKEND_CHANNELS       2U
#define USB_AUDIO_BACKEND_BUFFER_FRAMES  512U

static int32_t usb_audio_backend_buffer[USB_AUDIO_BACKEND_BUFFER_FRAMES * USB_AUDIO_BACKEND_CHANNELS];
static uint32_t usb_audio_backend_read_idx;
static uint32_t usb_audio_backend_write_idx;
static uint32_t usb_audio_backend_fill_frames;

void usb_audio_backend_init(void)
{
  usb_audio_backend_read_idx = 0U;
  usb_audio_backend_write_idx = 0U;
  usb_audio_backend_fill_frames = 0U;
}

uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames)
{
  uint32_t frames;
  uint32_t frame_idx;
  uint32_t channel;

  if ((dst == NULL) || (max_frames == 0U))
  {
    return 0U;
  }

  frames = usb_audio_backend_fill_frames;
  if (frames > max_frames)
  {
    frames = max_frames;
  }

  for (frame_idx = 0U; frame_idx < frames; frame_idx++)
  {
    uint32_t read_pos = usb_audio_backend_read_idx * USB_AUDIO_BACKEND_CHANNELS;
    uint32_t dst_pos = frame_idx * USB_AUDIO_BACKEND_CHANNELS;
    for (channel = 0U; channel < USB_AUDIO_BACKEND_CHANNELS; channel++)
    {
      dst[dst_pos + channel] = usb_audio_backend_buffer[read_pos + channel];
    }
    usb_audio_backend_read_idx++;
    if (usb_audio_backend_read_idx >= USB_AUDIO_BACKEND_BUFFER_FRAMES)
    {
      usb_audio_backend_read_idx = 0U;
    }
  }

  usb_audio_backend_fill_frames -= frames;
  return frames;
}

void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames)
{
  uint32_t frame_idx;
  uint32_t channel;

  if ((src == NULL) || (frames == 0U))
  {
    return;
  }

  for (frame_idx = 0U; frame_idx < frames; frame_idx++)
  {
    if (usb_audio_backend_fill_frames >= USB_AUDIO_BACKEND_BUFFER_FRAMES)
    {
      return;
    }

    uint32_t write_pos = usb_audio_backend_write_idx * USB_AUDIO_BACKEND_CHANNELS;
    uint32_t src_pos = frame_idx * USB_AUDIO_BACKEND_CHANNELS;
    for (channel = 0U; channel < USB_AUDIO_BACKEND_CHANNELS; channel++)
    {
      usb_audio_backend_buffer[write_pos + channel] = src[src_pos + channel];
    }

    usb_audio_backend_write_idx++;
    if (usb_audio_backend_write_idx >= USB_AUDIO_BACKEND_BUFFER_FRAMES)
    {
      usb_audio_backend_write_idx = 0U;
    }
    usb_audio_backend_fill_frames++;
  }
}
