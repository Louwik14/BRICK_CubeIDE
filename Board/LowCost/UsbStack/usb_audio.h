#ifndef BRICK6_USB_AUDIO_H_
#define BRICK6_USB_AUDIO_H_

#include <stdint.h>

void usb_audio_transport_reset(void);
void usb_audio_transport_process(void);
void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting);
void usb_audio_transport_close_interface(uint8_t interface_number);

uint32_t usb_audio_audio_read(float *left, float *right, uint32_t frames);
uint32_t usb_audio_audio_write(const float *left,
                               const float *right,
                               uint32_t frames);

#endif /* BRICK6_USB_AUDIO_H */
