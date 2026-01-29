#ifndef USB_AUDIO_BACKEND_H
#define USB_AUDIO_BACKEND_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_audio_backend_init(void);
uint32_t usb_audio_backend_pop_frames(int32_t *dst, uint32_t max_frames);
void usb_audio_backend_push_frames(const int32_t *src, uint32_t frames);

#ifdef __cplusplus
}
#endif

#endif /* USB_AUDIO_BACKEND_H */
