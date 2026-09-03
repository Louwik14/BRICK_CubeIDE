#ifndef APP_HALL_HALL_ADC_H
#define APP_HALL_HALL_ADC_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

void hall_adc_init(void);

uint16_t hall_adc_get_raw(uint8_t key);
uint8_t hall_adc_get_mux_index(void);
uint16_t hall_adc_get_mux_raw(uint8_t mux_adc, uint8_t mux_channel);
uint32_t hall_adc_get_sample_count(uint8_t key);

#endif /* APP_HALL_HALL_ADC_H */
