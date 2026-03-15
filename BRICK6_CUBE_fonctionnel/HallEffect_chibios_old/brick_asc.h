#ifndef BRICK_ASC_H
#define BRICK_ASC_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define BRICK_ASC_MAX_FACTOR 16U

struct brick_asc {
  uint32_t sum;
  uint16_t buffer[BRICK_ASC_MAX_FACTOR];
  uint8_t head;
  uint8_t count;
  uint8_t factor;
};

void brick_asc_array_set_factors(struct brick_asc* asc, size_t capacity, uint8_t start, uint8_t length, uint8_t factor);
bool brick_asc_process(struct brick_asc* asc, uint16_t rx, uint16_t* tx);

#endif /* BRICK_ASC_H */
