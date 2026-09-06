#ifndef DISPLAY_FLUSH_SERVICE_H
#define DISPLAY_FLUSH_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void display_flush_service_poll(void);
void display_flush_service_frame_ready(void);
uint8_t display_flush_service_frame_pending(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_FLUSH_SERVICE_H */
