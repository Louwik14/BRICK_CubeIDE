#include "brick_asc.h"

void brick_asc_array_set_factors(struct brick_asc* asc, size_t capacity, uint8_t start, uint8_t length, uint8_t factor) {
  if (asc == NULL || capacity == 0U || start >= capacity || start + length > capacity) {
    return;
  }

#if defined(DEBUG)
  /* Defensive programming in debug builds to catch configuration mistakes early. */
  if (start >= capacity || start + length > capacity) {
    __builtin_trap();
  }
#endif

  if (factor == 0U) {
    factor = 1U;
  }
  if (factor > BRICK_ASC_MAX_FACTOR) {
    factor = BRICK_ASC_MAX_FACTOR;
  }

  for (uint8_t i = start; i < start + length; ++i) {
    asc[i].sum = 0;
    asc[i].head = 0;
    asc[i].count = 0;
    asc[i].factor = factor;
    for (uint8_t j = 0; j < BRICK_ASC_MAX_FACTOR; ++j) {
      asc[i].buffer[j] = 0;
    }
  }
}

bool brick_asc_process(struct brick_asc* asc, uint16_t rx, uint16_t* tx) {
  if (asc == NULL || tx == NULL) {
    return false;
  }

  if (asc->factor == 0U || asc->factor > BRICK_ASC_MAX_FACTOR) {
    *tx = rx;
    asc->factor = (asc->factor == 0U) ? 1U : BRICK_ASC_MAX_FACTOR;
    asc->count = 1U;
    asc->head = 1U % asc->factor;
    asc->sum = rx;
    return asc->factor == 1U;
  }

  asc->sum -= asc->buffer[asc->head];
  asc->buffer[asc->head] = rx;
  asc->sum += rx;

  if (asc->count < asc->factor) {
    asc->count++;
  }

  asc->head = (asc->head + 1U) % asc->factor;
  *tx = (uint16_t)(asc->sum / asc->count);

  return asc->count == asc->factor;
}
