#ifndef HALL_ADC_H
#define HALL_ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void hall_adc_init(void);
void hall_mux_select(uint8_t index);
uint16_t hall_adc_get_raw(uint8_t key);
uint8_t hall_adc_get_mux_index(void);

#ifdef __cplusplus
}
#endif

#endif /* HALL_ADC_H */
