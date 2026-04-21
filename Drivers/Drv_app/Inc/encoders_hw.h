#ifndef ENCODERS_HW_H
#define ENCODERS_HW_H

#include <stdint.h>

#include "encoders.h"

void encoders_hw_init(void);
/**
 * @brief Initialise le timer dédié au polling rapide des encodeurs.
 */
void encoders_fast_poll_init(void);
/**
 * @brief Exécute la capture matérielle minimale des encodeurs en IRQ timer.
 */
void encoders_fast_poll_irq(void);
void encoders_hw_read(void);
int16_t encoders_hw_get_delta(uint8_t encoder);

#endif
