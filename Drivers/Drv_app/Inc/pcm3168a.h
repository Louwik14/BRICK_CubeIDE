#ifndef PCM3168A_H
#define PCM3168A_H

#include <stdbool.h>
#include <stdint.h>

#define PCM3168A1_ADDR_7BIT  0x44U
#define PCM3168A2_ADDR_7BIT  0x45U

bool PCM3168A_Init(uint8_t addr_7bit);

#endif /* PCM3168A_H */
