#ifndef APP_HALL_HALL_ADC_H
#define APP_HALL_HALL_ADC_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

typedef struct
{
    uint8_t  key;
    uint16_t raw;
    uint32_t sample_count;
} hall_adc_sample_t;

void hall_adc_init(void);

uint8_t hall_adc_pop_sample(hall_adc_sample_t *sample);

uint16_t hall_adc_get_raw(uint8_t key);
uint8_t hall_adc_get_mux_index(void);
uint32_t hall_adc_get_sample_count(uint8_t key);

uint32_t hall_adc_get_fifo_push_count(void);
uint32_t hall_adc_get_fifo_drop_count(void);
uint16_t hall_adc_get_fifo_depth(void);
uint16_t hall_adc_get_fifo_max_depth(void);

#endif /* APP_HALL_HALL_ADC_H */
