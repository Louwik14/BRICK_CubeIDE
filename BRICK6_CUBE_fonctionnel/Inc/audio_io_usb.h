#ifndef AUDIO_IO_USB_H
#define AUDIO_IO_USB_H

#include <stdint.h>

#include "audio_buffer.h"

void audio_io_usb_init(void);
audio_buffer_t *audio_io_usb_get_rx_buffer(void);
void audio_io_usb_on_rx_samples(const int32_t *samples, uint32_t count);
uint32_t audio_io_usb_read_rx_samples(int32_t *dst, uint32_t count);
uint32_t audio_io_usb_prepare_tx(int32_t *dst, uint32_t max_samples);
uint32_t audio_io_usb_get_rx_available(void);
uint32_t audio_io_usb_get_rx_free(void);
uint32_t audio_io_usb_get_rx_capacity(void);
uint32_t audio_io_usb_get_rx_written_total(void);
uint32_t audio_io_usb_get_rx_read_total(void);
uint32_t audio_io_usb_get_rx_dropped_total(void);
uint32_t audio_io_usb_get_rx_avail_min(void);
uint32_t audio_io_usb_get_rx_avail_max(void);
uint32_t audio_io_usb_get_rx_free_min(void);
uint32_t audio_io_usb_get_rx_free_max(void);

#endif /* AUDIO_IO_USB_H */
