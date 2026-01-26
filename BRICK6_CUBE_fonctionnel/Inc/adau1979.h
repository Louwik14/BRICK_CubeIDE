#ifndef ADAU1979_H
#define ADAU1979_H

#include <stdbool.h>
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

typedef enum
{
  ADAU1979_CH1 = 0,
  ADAU1979_CH2 = 1,
  ADAU1979_CH3 = 2,
  ADAU1979_CH4 = 3
} adau1979_channel_t;

bool adau1979_set_input_gain_db(uint8_t addr, adau1979_channel_t ch, float gain_db);
bool adau1979_set_all_inputs_gain_db(uint8_t addr, float gain_db);
bool adau1979_debug_read_reg(uint8_t addr, uint8_t reg, uint8_t *value);
float adau1979_gain_reg_to_db(uint8_t reg);

#endif /* ADAU1979_H */
