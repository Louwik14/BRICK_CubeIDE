#ifndef NOTE_FX_EUCLID_H
#define NOTE_FX_EUCLID_H

#include <stdint.h>

#define NOTE_FX_EUCLID_MASK_BITS 64U

/* Bit zero is the first pulse; bits above the normalized length are zero. */
uint64_t euclid_build_mask(uint8_t length, uint8_t pulse);

#endif
