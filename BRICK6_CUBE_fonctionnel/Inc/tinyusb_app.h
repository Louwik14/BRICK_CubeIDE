#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tinyusb_app_init(void);
void tinyusb_app_task(void);
uint32_t tinyusb_app_get_rx_done_count(void);
uint32_t tinyusb_app_get_rx_bytes_total(void);
uint32_t tinyusb_app_get_rx_samples_total(void);
uint32_t tinyusb_app_get_rx_zero_reads(void);

#ifdef __cplusplus
}
#endif
