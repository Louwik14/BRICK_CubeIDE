#ifndef APP_HALL_HALL_ADC_H
#define APP_HALL_HALL_ADC_H

#include <stdint.h>

#define HALL_KEY_COUNT 16U

void hall_adc_init(void);

uint16_t hall_adc_get_raw(uint8_t key);
uint8_t hall_adc_get_mux_index(void);
uint32_t hall_adc_get_sample_count(uint8_t key);

#endif
