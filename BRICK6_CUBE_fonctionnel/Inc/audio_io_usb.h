#ifndef AUDIO_IO_USB_H
#define AUDIO_IO_USB_H

#include "audio_buffer.h"
#include <stdbool.h>
#include <stdint.h>

enum
{
  AUDIO_USB_CHANNELS = 2U,
  AUDIO_USB_BYTES_PER_SAMPLE = 2U
};

void audio_io_usb_init(audio_buffer_t *rx_ring, audio_buffer_t *tx_ring, uint32_t sample_rate);
void audio_io_usb_reset(void);
void audio_io_usb_task(void);
bool audio_io_usb_rx_done_isr(uint16_t n_bytes_received);
uint32_t audio_io_usb_pop_rx(int32_t *dest, uint32_t frames);
uint32_t audio_io_usb_push_tx(const int32_t *src, uint32_t frames);

#endif /* AUDIO_IO_USB_H */
