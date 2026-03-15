#ifndef APP_HALL_MUX_TEST_H
#define APP_HALL_MUX_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HALL_MUX_TEST_KEY_COUNT 16U

void hall_mux_test_init(void);
void hall_mux_test_poll(void);
uint16_t hall_mux_test_get_raw(uint8_t key);

#ifdef __cplusplus
}
#endif

#endif /* APP_HALL_MUX_TEST_H */
