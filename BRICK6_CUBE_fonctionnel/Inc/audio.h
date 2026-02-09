#ifndef AUDIO_H
#define AUDIO_H

#include "stm32h7xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Audio minimal full-duplex:
 * - RX (SAI BlockB)
 * - TX (SAI BlockA)
 * - Loopback direct IN -> OUT
 *
 * Aucun moteur, aucun scheduler externe.
 */

void audio_init(SAI_HandleTypeDef *hsai_tx,
                SAI_HandleTypeDef *hsai_rx);

void audio_start(void);

/**
 * À appeler dans la boucle principale.
 * Remplit la moitié du buffer TX avec les données RX.
 */
void audio_poll(void);

/* Debug */
uint32_t audio_get_half_events(void);
uint32_t audio_get_full_events(void);

#endif
