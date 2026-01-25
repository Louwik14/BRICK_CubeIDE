#ifndef ADAU1979_H
#define ADAU1979_H

#include <stdint.h>

enum
{
  ADAU1979_ADDR_0 = 0x11U,
  ADAU1979_ADDR_1 = 0x31U,
  ADAU1979_ADDR_2 = 0x51U,
  ADAU1979_ADDR_3 = 0x71U
};

void adau1979_init(uint8_t addr, uint8_t first_slot);
void adau1979_init_all(void);

#endif /* ADAU1979_H */
