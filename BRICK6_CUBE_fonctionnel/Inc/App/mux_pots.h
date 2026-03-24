#ifndef APP_MUX_POTS_H
#define APP_MUX_POTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void mux_pots_init(void);
void mux_pots_scan(void);
uint16_t mux_pots_get(uint8_t pot);
uint8_t mux_pots_is_valid(uint8_t pot);

#ifdef __cplusplus
}
#endif

#endif /* APP_MUX_POTS_H */
