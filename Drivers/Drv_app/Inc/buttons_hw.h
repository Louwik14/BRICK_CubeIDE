#ifndef BUTTONS_HW_H
#define BUTTONS_HW_H

#include <stdint.h>

#include "buttons_ids.h"

void buttons_hw_init(void);
void buttons_hw_read(void);
uint8_t buttons_hw_get(button_id_t btn);

#endif
