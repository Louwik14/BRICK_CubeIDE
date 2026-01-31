#ifndef AUDIO_IO_USB_H
#define AUDIO_IO_USB_H

#include <stdint.h>

#include "audio_buffer.h"

void audio_io_usb_init(void);
audio_buffer_t *audio_io_usb_get_rx_buffer(void);
void audio_io_usb_on_rx_samples(const int32_t *samples, uint32_t count);
uint32_t audio_io_usb_prepare_tx(int32_t *dst, uint32_t max_samples);

#endif /* AUDIO_IO_USB_H */
