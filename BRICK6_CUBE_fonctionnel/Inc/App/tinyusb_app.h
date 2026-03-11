#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tinyusb_app_init(void);
void tinyusb_app_task(void);

uint32_t speaker_ring_read(uint8_t *data, uint32_t len);
uint32_t tinyusb_capture_write_stereo_s16(const int16_t *interleaved, uint32_t frames);

#ifdef __cplusplus
}
#endif
