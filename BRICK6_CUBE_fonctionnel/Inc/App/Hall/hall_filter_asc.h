#ifndef APP_HALL_HALL_FILTER_ASC_H
#define APP_HALL_HALL_FILTER_ASC_H

#include <stdint.h>

#include "App/Hall/hall_engine.h"

#define HALL_FILTER_ASC_FACTOR_DEFAULT  4U

void hall_filter_asc_init(void);
void hall_filter_asc_reset_all(void);
void hall_filter_asc_reset_key(uint8_t key);

void hall_filter_asc_set_factor(uint8_t key, uint8_t factor);
void hall_filter_asc_set_factor_range(uint8_t start, uint8_t length, uint8_t factor);

uint8_t hall_filter_asc_process(uint8_t key, uint16_t raw, uint16_t *filtered_raw);

uint8_t hall_filter_asc_get_factor(uint8_t key);
uint8_t hall_filter_asc_get_count(uint8_t key);
uint16_t hall_filter_asc_get_last(uint8_t key);

#endif /* APP_HALL_HALL_FILTER_ASC_H */
