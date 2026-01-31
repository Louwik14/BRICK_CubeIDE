#include "audio_io_usb.h"

#include "tusb.h"

#define USB_RX_CAPACITY ((4U * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) / sizeof(int32_t))
#define USB_TX_CAPACITY ((4U * CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX) / sizeof(int32_t))

static audio_buffer_t usb_rx_buf;
static int32_t usb_rx_mem[USB_RX_CAPACITY];
static audio_buffer_t usb_tx_buf;
static int32_t usb_tx_mem[USB_TX_CAPACITY];

void audio_io_usb_init(void)
{
  audio_buffer_init(&usb_rx_buf, usb_rx_mem, USB_RX_CAPACITY);
  audio_buffer_init(&usb_tx_buf, usb_tx_mem, USB_TX_CAPACITY);
}

audio_buffer_t *audio_io_usb_get_rx_buffer(void)
{
  return &usb_rx_buf;
}

void audio_io_usb_on_rx_samples(const int32_t *samples, uint32_t count)
{
  (void)audio_buffer_write(&usb_rx_buf, samples, count);
}

uint32_t audio_io_usb_prepare_tx(int32_t *dst, uint32_t max_samples)
{
  return audio_buffer_read(&usb_tx_buf, dst, max_samples);
}
