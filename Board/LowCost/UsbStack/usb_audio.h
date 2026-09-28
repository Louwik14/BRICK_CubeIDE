#ifndef BRICK6_USB_AUDIO_H_
#define BRICK6_USB_AUDIO_H_

#include <stdint.h>

void usb_audio_transport_reset(void);
void usb_audio_audio_boundary(void);
void usb_audio_feedback_sof(uint16_t usb_frame);
void usb_audio_transport_set_interface(uint8_t interface_number,
                                       uint8_t alternate_setting);
void usb_audio_transport_close_interface(uint8_t interface_number);

uint32_t usb_audio_audio_read(float *left, float *right, uint32_t frames);

#endif /* BRICK6_USB_AUDIO_H */
