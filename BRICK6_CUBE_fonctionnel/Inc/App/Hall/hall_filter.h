#ifndef HALL_FILTER_H
#define HALL_FILTER_H

#include <stdint.h>

#define HALL_KEY_COUNT 16U

void hall_filter_init(void);
void hall_filter_process(void);

uint16_t hall_filter_get(uint8_t key);

#endif
